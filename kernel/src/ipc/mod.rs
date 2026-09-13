//! Synchronous IPC channel policy for a rendezvous call/reply exchange.
//!
//! This module provides the allocation-free policy half of Phase 3 part 2:
//! bounded [`Channel`] rendezvous state machines paired by a [`ChannelTable`]
//! into caller/responder endpoints. It performs no allocation, executes no
//! privileged instructions, and performs no scheduler blocking, so it can be
//! exercised by host unit tests. It does not touch the `QEMU` harness or
//! syscall dispatch; wiring endpoints to syscalls is a later phase.
//!
//! # Rendezvous protocol
//!
//! Each channel is a single synchronous exchange owned by exactly two
//! endpoints:
//!
//! 1. The caller invokes [`ChannelTable::call`] while the channel is idle.
//! 2. The responder observes the pending call with [`ChannelTable::recv`],
//!    which moves the channel from call-pending to reply-pending.
//! 3. The responder completes the exchange with [`ChannelTable::reply`],
//!    which moves the channel to reply-ready.
//! 4. The caller retrieves the reply with [`ChannelTable::take_reply`],
//!    which returns the channel to idle.
//!
//! Roles are enforced by endpoint: only the caller may call or take a reply,
//! and only the responder may receive or reply. Using the wrong endpoint
//! reports [`IpcError::RightsMismatch`]. Operating on a closed or unknown
//! endpoint reports [`IpcError::InvalidChannel`]; operating on the surviving
//! peer after the other endpoint closed reports [`IpcError::PeerClosed`].
//!
//! # Bounds
//!
//! Message payloads are bounded by [`MAX_MESSAGE_BYTES`] and transferred
//! handle words by [`MAX_TRANSFERRED_HANDLES`]. The table holds at most
//! [`MAX_CHANNELS`] live channels. All copies are bounded and use fixed-size
//! arrays; no allocation occurs.

/// Maximum message payload in bytes per call or reply.
pub const MAX_MESSAGE_BYTES: usize = 256;
/// Maximum handle words transferred per call.
pub const MAX_TRANSFERRED_HANDLES: usize = 4;
/// Maximum live channels per [`ChannelTable`].
pub const MAX_CHANNELS: usize = 16;

/// Reserved handle word that is never a valid transferred handle.
///
/// [`Channel::call`] and [`ChannelTable::call`] reject any handle list
/// containing this value with [`IpcError::InvalidHandle`] so tests and peers
/// can present a forged word for rejection.
const RESERVED_HANDLE: u32 = u32::MAX;

/// Identifier for one endpoint of a channel pair.
///
/// Endpoint identifiers are allocated monotonically starting from one. Zero
/// and [`u32::MAX`] are never issued. Validity is always rechecked by the
/// owning [`ChannelTable`].
pub type EndpointId = u32;

/// Role of a channel endpoint.
///
/// Each channel pairs exactly one caller with exactly one responder. Roles
/// are fixed at [`ChannelTable::create_channel`] time and enforced on every
/// rendezvous operation.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum ChannelEndpoint {
    /// Initiates calls and retrieves replies.
    Caller,
    /// Receives calls and produces replies.
    Responder,
}

/// Observable lifecycle state of a [`Channel`].
///
/// The sender identity is enforced by [`ChannelTable`] endpoint roles rather
/// than stored in this state; `len` and `handle_count` describe the staged
/// payload so hosts can assert the state machine without reading buffers.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum ChannelState {
    /// No call is staged; [`Channel::call`] may start an exchange.
    Idle,
    /// A call is staged and awaits [`Channel::recv`].
    CallPending {
        /// Staged request payload length in bytes.
        len: usize,
        /// Staged request handle count.
        handle_count: usize,
    },
    /// The responder observed the call and owes a [`Channel::reply`].
    ReplyPending {
        /// Original request payload length in bytes.
        len: usize,
        /// Original request handle count.
        handle_count: usize,
    },
    /// A reply is staged and awaits [`Channel::take_reply`].
    ReplyReady {
        /// Staged reply payload length in bytes.
        len: usize,
    },
}

/// Failures from synchronous IPC channel bookkeeping.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum IpcError {
    /// Endpoint is unknown, closed, or no channel slot is free.
    InvalidChannel,
    /// Message payload exceeds [`MAX_MESSAGE_BYTES`] or an output buffer is
    /// too small for the staged payload.
    MessageTooLarge,
    /// Handle list exceeds [`MAX_TRANSFERRED_HANDLES`] or an output handle
    /// buffer is too small for the staged handles.
    TooManyHandles,
    /// No call or reply is staged for this transition.
    NoPendingCall,
    /// A call or reply is already staged; the peer must advance first.
    AlreadyPending,
    /// Endpoint role does not permit this operation.
    RightsMismatch,
    /// The peer endpoint closed; the exchange cannot complete.
    PeerClosed,
    /// A transferred handle word is reserved or malformed.
    InvalidHandle,
}

/// Single synchronous rendezvous between a caller and a responder.
///
/// The channel stores one staged request (payload plus handle words) and one
/// staged reply payload in fixed-size arrays. Transitions are idle (`call`),
/// call-pending (`recv`), reply-pending (`reply`), reply-ready
/// (`take_reply`), then back to idle. All copies are bounded; no allocation
/// occurs.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Channel {
    state: ChannelState,
    request_len: usize,
    request_data: [u8; MAX_MESSAGE_BYTES],
    request_handle_count: usize,
    request_handles: [u32; MAX_TRANSFERRED_HANDLES],
    reply_len: usize,
    reply_data: [u8; MAX_MESSAGE_BYTES],
}

impl Channel {
    /// Creates an idle channel with empty staged buffers.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            state: ChannelState::Idle,
            request_len: 0,
            request_data: [0u8; MAX_MESSAGE_BYTES],
            request_handle_count: 0,
            request_handles: [0u32; MAX_TRANSFERRED_HANDLES],
            reply_len: 0,
            reply_data: [0u8; MAX_MESSAGE_BYTES],
        }
    }

    /// Returns the current lifecycle state.
    #[must_use]
    pub const fn state(&self) -> ChannelState {
        self.state
    }

    /// Returns whether the channel is idle and may accept a call.
    #[must_use]
    pub const fn is_idle(&self) -> bool {
        matches!(self.state, ChannelState::Idle)
    }

    /// Stages a call payload and handle list.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::MessageTooLarge`] when `msg` exceeds
    /// [`MAX_MESSAGE_BYTES`], [`IpcError::TooManyHandles`] when `handles`
    /// exceeds [`MAX_TRANSFERRED_HANDLES`], [`IpcError::InvalidHandle`] when
    /// any handle equals the reserved word, or [`IpcError::AlreadyPending`]
    /// when the channel is not idle.
    pub fn call(&mut self, msg: &[u8], handles: &[u32]) -> Result<(), IpcError> {
        if msg.len() > MAX_MESSAGE_BYTES {
            return Err(IpcError::MessageTooLarge);
        }
        if handles.len() > MAX_TRANSFERRED_HANDLES {
            return Err(IpcError::TooManyHandles);
        }
        for handle in handles {
            if *handle == RESERVED_HANDLE {
                return Err(IpcError::InvalidHandle);
            }
        }
        if !matches!(self.state, ChannelState::Idle) {
            return Err(IpcError::AlreadyPending);
        }
        self.request_data[..msg.len()].copy_from_slice(msg);
        self.request_len = msg.len();
        self.request_handles[..handles.len()].copy_from_slice(handles);
        self.request_handle_count = handles.len();
        self.reply_len = 0;
        self.state = ChannelState::CallPending {
            len: msg.len(),
            handle_count: handles.len(),
        };
        Ok(())
    }

    /// Observes the pending call, copying it into caller-provided buffers.
    ///
    /// On success the channel moves from call-pending to reply-pending.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::NoPendingCall`] when no call is staged,
    /// [`IpcError::MessageTooLarge`] when `out_msg` is smaller than the
    /// staged payload, or [`IpcError::TooManyHandles`] when `out_handles`
    /// is smaller than the staged handle count.
    pub fn recv(
        &mut self,
        out_msg: &mut [u8],
        out_handles: &mut [u32],
    ) -> Result<(usize, usize), IpcError> {
        let ChannelState::CallPending { len, handle_count } = self.state else {
            return Err(IpcError::NoPendingCall);
        };
        if out_msg.len() < len {
            return Err(IpcError::MessageTooLarge);
        }
        if out_handles.len() < handle_count {
            return Err(IpcError::TooManyHandles);
        }
        out_msg[..len].copy_from_slice(&self.request_data[..len]);
        out_handles[..handle_count].copy_from_slice(&self.request_handles[..handle_count]);
        self.state = ChannelState::ReplyPending { len, handle_count };
        Ok((len, handle_count))
    }

    /// Stages a reply payload for a previously received call.
    ///
    /// On success the channel moves from reply-pending to reply-ready.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::MessageTooLarge`] when `resp` exceeds
    /// [`MAX_MESSAGE_BYTES`], or [`IpcError::NoPendingCall`] when the
    /// channel is not awaiting a reply.
    pub fn reply(&mut self, resp: &[u8]) -> Result<(), IpcError> {
        if resp.len() > MAX_MESSAGE_BYTES {
            return Err(IpcError::MessageTooLarge);
        }
        if !matches!(self.state, ChannelState::ReplyPending { .. }) {
            return Err(IpcError::NoPendingCall);
        }
        self.reply_data[..resp.len()].copy_from_slice(resp);
        self.reply_len = resp.len();
        self.state = ChannelState::ReplyReady { len: resp.len() };
        Ok(())
    }

    /// Retrieves the staged reply, copying it into `out`.
    ///
    /// On success the channel returns to idle and staged buffers are
    /// cleared.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::NoPendingCall`] when no reply is staged, or
    /// [`IpcError::MessageTooLarge`] when `out` is smaller than the staged
    /// reply.
    pub fn take_reply(&mut self, out: &mut [u8]) -> Result<usize, IpcError> {
        let ChannelState::ReplyReady { len } = self.state else {
            return Err(IpcError::NoPendingCall);
        };
        if out.len() < len {
            return Err(IpcError::MessageTooLarge);
        }
        out[..len].copy_from_slice(&self.reply_data[..len]);
        self.state = ChannelState::Idle;
        self.request_len = 0;
        self.request_handle_count = 0;
        self.reply_len = 0;
        Ok(len)
    }
}

impl Default for Channel {
    /// Creates an idle channel, mirroring [`Channel::new`].
    fn default() -> Self {
        Self::new()
    }
}

/// Stored channel plus its endpoint identities and open flags.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct ChannelSlot {
    channel: Channel,
    caller: EndpointId,
    responder: EndpointId,
    caller_open: bool,
    responder_open: bool,
}

/// Bounded table pairing channels into caller/responder endpoints.
///
/// The table owns at most [`MAX_CHANNELS`] live channels. Endpoint
/// identifiers are allocated monotonically and never reused while live; when
/// both endpoints of a slot close, the slot is freed for reuse. There is no
/// scheduler blocking: a caller and responder advance the rendezvous by
/// calling [`ChannelTable::call`], [`ChannelTable::recv`],
/// [`ChannelTable::reply`], and [`ChannelTable::take_reply`] in order.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChannelTable {
    slots: [Option<ChannelSlot>; MAX_CHANNELS],
    next_id: EndpointId,
}

impl ChannelTable {
    /// Creates an empty table with the first endpoint identifier set to one.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            slots: [None; MAX_CHANNELS],
            next_id: 1,
        }
    }

    /// Returns the number of live channel slots.
    #[must_use]
    pub const fn len(&self) -> usize {
        let mut count = 0;
        let mut index = 0;
        while index < MAX_CHANNELS {
            if self.slots[index].is_some() {
                count += 1;
            }
            index += 1;
        }
        count
    }

    /// Returns whether the table holds no live channels.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the maximum number of live channels.
    #[must_use]
    pub const fn capacity(&self) -> usize {
        MAX_CHANNELS
    }

    /// Creates a channel pair and returns `(caller_id, responder_id)`.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when the table already holds
    /// [`MAX_CHANNELS`] live channels.
    pub fn create_channel(&mut self) -> Result<(EndpointId, EndpointId), IpcError> {
        let slot_index = self
            .slots
            .iter()
            .position(Option::is_none)
            .ok_or(IpcError::InvalidChannel)?;
        let caller = self.alloc_id();
        let responder = self.alloc_id();
        let slot = ChannelSlot {
            channel: Channel::new(),
            caller,
            responder,
            caller_open: true,
            responder_open: true,
        };
        *self
            .slots
            .get_mut(slot_index)
            .ok_or(IpcError::InvalidChannel)? = Some(slot);
        Ok((caller, responder))
    }

    /// Returns whether `endpoint` names a currently open endpoint.
    #[must_use]
    pub const fn is_valid(&self, endpoint: EndpointId) -> bool {
        let mut index = 0;
        while index < MAX_CHANNELS {
            if let Some(slot) = self.slots[index] {
                if slot.caller == endpoint && slot.caller_open {
                    return true;
                }
                if slot.responder == endpoint && slot.responder_open {
                    return true;
                }
            }
            index += 1;
        }
        false
    }

    /// Returns the rendezvous state for an open endpoint.
    ///
    /// Peer closure does not affect introspection; use the rendezvous
    /// operations to observe [`IpcError::PeerClosed`].
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// already closed.
    pub fn channel_state(&self, endpoint: EndpointId) -> Result<ChannelState, IpcError> {
        let (index, _) = self.lookup(endpoint)?;
        let slot = self.slots.get(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_ref().ok_or(IpcError::InvalidChannel)?;
        if (slot.caller == endpoint && !slot.caller_open)
            || (slot.responder == endpoint && !slot.responder_open)
        {
            return Err(IpcError::InvalidChannel);
        }
        Ok(slot.channel.state())
    }

    /// Closes one endpoint of a channel pair.
    ///
    /// When only one endpoint closes, the slot is retained so the surviving
    /// peer observes [`IpcError::PeerClosed`]. When both endpoints close,
    /// the slot is freed for reuse.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// already closed.
    pub fn close(&mut self, endpoint: EndpointId) -> Result<(), IpcError> {
        let (index, role) = self.lookup(endpoint)?;
        let slot = self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_mut().ok_or(IpcError::InvalidChannel)?;
        match role {
            ChannelEndpoint::Caller => {
                if !slot.caller_open {
                    return Err(IpcError::InvalidChannel);
                }
                slot.caller_open = false;
            }
            ChannelEndpoint::Responder => {
                if !slot.responder_open {
                    return Err(IpcError::InvalidChannel);
                }
                slot.responder_open = false;
            }
        }
        if !slot.caller_open && !slot.responder_open {
            *self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)? = None;
        }
        Ok(())
    }

    /// Stages a call from a caller endpoint.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// closed, [`IpcError::RightsMismatch`] when `endpoint` is a responder,
    /// [`IpcError::PeerClosed`] when the responder already closed,
    /// [`IpcError::MessageTooLarge`], [`IpcError::TooManyHandles`], or
    /// [`IpcError::InvalidHandle`] for a malformed payload, or
    /// [`IpcError::AlreadyPending`] when the channel is not idle.
    pub fn call(
        &mut self,
        endpoint: EndpointId,
        msg: &[u8],
        handles: &[u32],
    ) -> Result<(), IpcError> {
        let (index, role) = self.lookup(endpoint)?;
        if role != ChannelEndpoint::Caller {
            return Err(IpcError::RightsMismatch);
        }
        let slot = self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_mut().ok_or(IpcError::InvalidChannel)?;
        if slot.caller != endpoint || !slot.caller_open {
            return Err(IpcError::InvalidChannel);
        }
        if !slot.responder_open {
            return Err(IpcError::PeerClosed);
        }
        slot.channel.call(msg, handles)
    }

    /// Receives a pending call on a responder endpoint.
    ///
    /// On success the channel moves from call-pending to reply-pending.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// closed, [`IpcError::RightsMismatch`] when `endpoint` is a caller,
    /// [`IpcError::PeerClosed`] when the caller already closed,
    /// [`IpcError::NoPendingCall`] when no call is staged,
    /// [`IpcError::MessageTooLarge`] when `out_msg` is too small, or
    /// [`IpcError::TooManyHandles`] when `out_handles` is too small.
    pub fn recv(
        &mut self,
        endpoint: EndpointId,
        out_msg: &mut [u8],
        out_handles: &mut [u32],
    ) -> Result<(usize, usize), IpcError> {
        let (index, role) = self.lookup(endpoint)?;
        if role != ChannelEndpoint::Responder {
            return Err(IpcError::RightsMismatch);
        }
        let slot = self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_mut().ok_or(IpcError::InvalidChannel)?;
        if slot.responder != endpoint || !slot.responder_open {
            return Err(IpcError::InvalidChannel);
        }
        if !slot.caller_open {
            return Err(IpcError::PeerClosed);
        }
        slot.channel.recv(out_msg, out_handles)
    }

    /// Replies to a received call from a responder endpoint.
    ///
    /// On success the channel moves from reply-pending to reply-ready.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// closed, [`IpcError::RightsMismatch`] when `endpoint` is a caller,
    /// [`IpcError::PeerClosed`] when the caller already closed,
    /// [`IpcError::MessageTooLarge`] when `resp` exceeds
    /// [`MAX_MESSAGE_BYTES`], or [`IpcError::NoPendingCall`] when the
    /// channel is not awaiting a reply.
    pub fn reply(&mut self, endpoint: EndpointId, resp: &[u8]) -> Result<(), IpcError> {
        let (index, role) = self.lookup(endpoint)?;
        if role != ChannelEndpoint::Responder {
            return Err(IpcError::RightsMismatch);
        }
        let slot = self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_mut().ok_or(IpcError::InvalidChannel)?;
        if slot.responder != endpoint || !slot.responder_open {
            return Err(IpcError::InvalidChannel);
        }
        if !slot.caller_open {
            return Err(IpcError::PeerClosed);
        }
        slot.channel.reply(resp)
    }

    /// Retrieves a staged reply on a caller endpoint.
    ///
    /// On success the channel returns to idle.
    ///
    /// # Errors
    ///
    /// Returns [`IpcError::InvalidChannel`] when `endpoint` is unknown or
    /// closed, [`IpcError::RightsMismatch`] when `endpoint` is a responder,
    /// [`IpcError::PeerClosed`] when the responder already closed,
    /// [`IpcError::NoPendingCall`] when no reply is staged, or
    /// [`IpcError::MessageTooLarge`] when `out` is too small.
    pub fn take_reply(&mut self, endpoint: EndpointId, out: &mut [u8]) -> Result<usize, IpcError> {
        let (index, role) = self.lookup(endpoint)?;
        if role != ChannelEndpoint::Caller {
            return Err(IpcError::RightsMismatch);
        }
        let slot = self.slots.get_mut(index).ok_or(IpcError::InvalidChannel)?;
        let slot = slot.as_mut().ok_or(IpcError::InvalidChannel)?;
        if slot.caller != endpoint || !slot.caller_open {
            return Err(IpcError::InvalidChannel);
        }
        if !slot.responder_open {
            return Err(IpcError::PeerClosed);
        }
        slot.channel.take_reply(out)
    }

    /// Finds the slot index and role for an endpoint identifier.
    ///
    /// Returns the slot even when the matching side already closed so
    /// callers can distinguish [`IpcError::InvalidChannel`] (self closed)
    /// from [`IpcError::PeerClosed`] (peer closed).
    fn lookup(&self, endpoint: EndpointId) -> Result<(usize, ChannelEndpoint), IpcError> {
        let mut index = 0;
        while index < MAX_CHANNELS {
            if let Some(slot) = self.slots.get(index).copied().flatten() {
                if slot.caller == endpoint {
                    return Ok((index, ChannelEndpoint::Caller));
                }
                if slot.responder == endpoint {
                    return Ok((index, ChannelEndpoint::Responder));
                }
            }
            index += 1;
        }
        Err(IpcError::InvalidChannel)
    }

    /// Returns whether an endpoint identifier is currently allocated.
    fn is_id_in_use(&self, id: EndpointId) -> bool {
        let mut index = 0;
        while index < MAX_CHANNELS {
            if let Some(slot) = self.slots.get(index).copied().flatten()
                && (slot.caller == id || slot.responder == id)
            {
                return true;
            }
            index += 1;
        }
        false
    }

    /// Allocates a fresh endpoint identifier.
    ///
    /// Zero and the reserved handle word are never issued. Identifiers that
    /// collide with live endpoints are skipped.
    fn alloc_id(&mut self) -> EndpointId {
        loop {
            let candidate = self.next_id;
            let mut next = candidate.wrapping_add(1);
            while next == 0 || next == RESERVED_HANDLE {
                next = next.wrapping_add(1);
            }
            self.next_id = next;
            if candidate == 0 || candidate == RESERVED_HANDLE {
                continue;
            }
            if self.is_id_in_use(candidate) {
                continue;
            }
            return candidate;
        }
    }
}

impl Default for ChannelTable {
    /// Creates an empty table, mirroring [`ChannelTable::new`].
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn drain_call(
        table: &mut ChannelTable,
        caller: EndpointId,
        responder: EndpointId,
        msg: &[u8],
        handles: &[u32],
    ) -> (usize, usize) {
        table.call(caller, msg, handles).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap()
    }

    #[test]
    fn create_pair_returns_distinct_valid_endpoints() {
        let mut table = ChannelTable::new();
        assert!(table.is_empty());
        let (caller, responder) = table.create_channel().unwrap();
        assert_ne!(caller, responder);
        assert!(table.is_valid(caller));
        assert!(table.is_valid(responder));
        assert_eq!(table.len(), 1);
        assert_eq!(table.capacity(), MAX_CHANNELS);
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));
        assert_eq!(table.channel_state(responder), Ok(ChannelState::Idle));
    }

    #[test]
    fn call_recv_reply_roundtrip_with_handles() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        let msg = b"hello ipc";
        let handles = [7u32, 8u32];
        table.call(caller, msg, &handles).unwrap();
        assert_eq!(
            table.channel_state(caller),
            Ok(ChannelState::CallPending {
                len: msg.len(),
                handle_count: handles.len()
            })
        );

        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let (len, handle_count) = table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(len, msg.len());
        assert_eq!(handle_count, handles.len());
        assert_eq!(&out_msg[..len], msg);
        assert_eq!(&out_handles[..handle_count], &handles);

        let resp = b"ack";
        table.reply(responder, resp).unwrap();
        assert_eq!(
            table.channel_state(caller),
            Ok(ChannelState::ReplyReady { len: resp.len() })
        );
        let mut reply_buf = [0u8; MAX_MESSAGE_BYTES];
        let reply_len = table.take_reply(caller, &mut reply_buf).unwrap();
        assert_eq!(&reply_buf[..reply_len], resp);
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));
    }

    #[test]
    fn message_too_large_rejected() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        let oversized = [0xABu8; MAX_MESSAGE_BYTES + 1];
        assert_eq!(
            table.call(caller, &oversized, &[]),
            Err(IpcError::MessageTooLarge)
        );
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));

        table.call(caller, b"ok", &[]).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(
            table.reply(responder, &oversized),
            Err(IpcError::MessageTooLarge)
        );

        // Output buffer smaller than the staged payload is also rejected.
        let mut tiny = [0u8; 1];
        let mut handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let (second_caller, second_responder) = table.create_channel().unwrap();
        table.call(second_caller, b"hello", &[]).unwrap();
        assert_eq!(
            table.recv(second_responder, &mut tiny, &mut handles),
            Err(IpcError::MessageTooLarge)
        );
    }

    #[test]
    fn too_many_handles_rejected() {
        let mut table = ChannelTable::new();
        let (caller, _) = table.create_channel().unwrap();
        let too_many = [1u32; MAX_TRANSFERRED_HANDLES + 1];
        assert_eq!(
            table.call(caller, b"hi", &too_many),
            Err(IpcError::TooManyHandles)
        );
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));
    }

    #[test]
    fn invalid_handle_rejected() {
        let mut table = ChannelTable::new();
        let (caller, _) = table.create_channel().unwrap();
        assert_eq!(
            table.call(caller, b"hi", &[u32::MAX]),
            Err(IpcError::InvalidHandle)
        );
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));
    }

    #[test]
    fn double_call_without_recv_fails() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        table.call(caller, b"first", &[]).unwrap();
        assert_eq!(
            table.call(caller, b"second", &[]),
            Err(IpcError::AlreadyPending)
        );
        // Drain to prove the first call is intact.
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let (len, _) = table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(&out_msg[..len], b"first");
    }

    #[test]
    fn reply_without_call_fails() {
        let mut table = ChannelTable::new();
        let (_, responder) = table.create_channel().unwrap();
        assert_eq!(
            table.reply(responder, b"orphan"),
            Err(IpcError::NoPendingCall)
        );
    }

    #[test]
    fn recv_twice_fails() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        table.call(caller, b"once", &[]).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(
            table.recv(responder, &mut out_msg, &mut out_handles),
            Err(IpcError::NoPendingCall)
        );
    }

    #[test]
    fn take_reply_without_reply_fails() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        // No call at all.
        let mut buf = [0u8; MAX_MESSAGE_BYTES];
        assert_eq!(
            table.take_reply(caller, &mut buf),
            Err(IpcError::NoPendingCall)
        );
        // Call received but no reply staged yet.
        table.call(caller, b"ping", &[]).unwrap();
        assert_eq!(
            table.take_reply(caller, &mut buf),
            Err(IpcError::NoPendingCall)
        );
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(
            table.take_reply(caller, &mut buf),
            Err(IpcError::NoPendingCall)
        );
    }

    #[test]
    fn peer_close_wakes_with_peer_closed() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        table.call(caller, b"ping", &[]).unwrap();
        table.close(responder).unwrap();
        assert!(!table.is_valid(responder));
        assert!(table.is_valid(caller));
        let mut buf = [0u8; MAX_MESSAGE_BYTES];
        assert_eq!(
            table.take_reply(caller, &mut buf),
            Err(IpcError::PeerClosed)
        );
        assert_eq!(table.call(caller, b"again", &[]), Err(IpcError::PeerClosed));

        let (caller_b, responder_b) = table.create_channel().unwrap();
        table.close(caller_b).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        assert_eq!(
            table.recv(responder_b, &mut out_msg, &mut out_handles),
            Err(IpcError::PeerClosed)
        );
        assert_eq!(table.reply(responder_b, b"nope"), Err(IpcError::PeerClosed));
    }

    #[test]
    fn empty_message_ok() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        table.call(caller, &[], &[]).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let (len, handle_count) = table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(len, 0);
        assert_eq!(handle_count, 0);
        table.reply(responder, &[]).unwrap();
        let mut reply_buf = [0u8; MAX_MESSAGE_BYTES];
        let reply_len = table.take_reply(caller, &mut reply_buf).unwrap();
        assert_eq!(reply_len, 0);
        assert_eq!(table.channel_state(caller), Ok(ChannelState::Idle));
    }

    #[test]
    fn rights_mismatch_rejected() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let mut reply_buf = [0u8; MAX_MESSAGE_BYTES];
        // Caller may not receive or reply.
        assert_eq!(
            table.recv(caller, &mut out_msg, &mut out_handles),
            Err(IpcError::RightsMismatch)
        );
        assert_eq!(table.reply(caller, b"x"), Err(IpcError::RightsMismatch));
        // Responder may not call or take a reply.
        assert_eq!(
            table.call(responder, b"x", &[]),
            Err(IpcError::RightsMismatch)
        );
        assert_eq!(
            table.take_reply(responder, &mut reply_buf),
            Err(IpcError::RightsMismatch)
        );
    }

    #[test]
    fn invalid_channel_rejected() {
        let mut table = ChannelTable::new();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let mut reply_buf = [0u8; MAX_MESSAGE_BYTES];
        let bogus = 0xDEAD_BEEFu32;
        assert!(!table.is_valid(bogus));
        assert_eq!(table.call(bogus, b"x", &[]), Err(IpcError::InvalidChannel));
        assert_eq!(
            table.recv(bogus, &mut out_msg, &mut out_handles),
            Err(IpcError::InvalidChannel)
        );
        assert_eq!(table.reply(bogus, b"x"), Err(IpcError::InvalidChannel));
        assert_eq!(
            table.take_reply(bogus, &mut reply_buf),
            Err(IpcError::InvalidChannel)
        );
        assert_eq!(table.close(bogus), Err(IpcError::InvalidChannel));
        assert_eq!(table.channel_state(bogus), Err(IpcError::InvalidChannel));

        // Closing twice reports invalid the second time.
        let (caller, _) = table.create_channel().unwrap();
        table.close(caller).unwrap();
        assert!(!table.is_valid(caller));
        assert_eq!(table.close(caller), Err(IpcError::InvalidChannel));
        assert_eq!(table.call(caller, b"x", &[]), Err(IpcError::InvalidChannel));
    }

    #[test]
    fn table_full_rejected_and_slot_reused() {
        let mut table = ChannelTable::new();
        let mut endpoints = [(0u32, 0u32); MAX_CHANNELS];
        for slot in &mut endpoints {
            *slot = table.create_channel().unwrap();
        }
        assert_eq!(table.len(), MAX_CHANNELS);
        assert_eq!(table.create_channel(), Err(IpcError::InvalidChannel));
        // Free one slot by closing both endpoints, then reuse succeeds.
        let (first_caller, first_responder) = endpoints[0];
        table.close(first_caller).unwrap();
        // Peer still open, so the slot is retained and creation still fails.
        assert_eq!(table.len(), MAX_CHANNELS);
        assert_eq!(table.create_channel(), Err(IpcError::InvalidChannel));
        table.close(first_responder).unwrap();
        assert_eq!(table.len(), MAX_CHANNELS - 1);
        let (caller, responder) = table.create_channel().unwrap();
        assert!(table.is_valid(caller));
        assert!(table.is_valid(responder));
        // Full roundtrip still works on the reused slot.
        drain_call(&mut table, caller, responder, b"reuse", &[1]);
        table.reply(responder, b"ok").unwrap();
        let mut buf = [0u8; MAX_MESSAGE_BYTES];
        let len = table.take_reply(caller, &mut buf).unwrap();
        assert_eq!(&buf[..len], b"ok");
    }

    #[test]
    fn max_size_message_roundtrip() {
        let mut table = ChannelTable::new();
        let (caller, responder) = table.create_channel().unwrap();
        let msg = [0x5Au8; MAX_MESSAGE_BYTES];
        let handles = [1u32, 2u32, 3u32, 4u32];
        table.call(caller, &msg, &handles).unwrap();
        let mut out_msg = [0u8; MAX_MESSAGE_BYTES];
        let mut out_handles = [0u32; MAX_TRANSFERRED_HANDLES];
        let (len, handle_count) = table
            .recv(responder, &mut out_msg, &mut out_handles)
            .unwrap();
        assert_eq!(len, MAX_MESSAGE_BYTES);
        assert_eq!(handle_count, MAX_TRANSFERRED_HANDLES);
        assert_eq!(&out_msg[..len], &msg[..]);
        assert_eq!(&out_handles[..handle_count], &handles[..]);
        table.reply(responder, &msg).unwrap();
        let mut reply_buf = [0u8; MAX_MESSAGE_BYTES];
        let reply_len = table.take_reply(caller, &mut reply_buf).unwrap();
        assert_eq!(&reply_buf[..reply_len], &msg[..]);
    }

    #[test]
    fn channel_state_machine_directly() {
        let mut channel = Channel::new();
        assert!(channel.is_idle());
        assert_eq!(channel.state(), ChannelState::Idle);
        assert_eq!(channel.recv(&mut [], &mut []), Err(IpcError::NoPendingCall));
        assert_eq!(channel.reply(&[]), Err(IpcError::NoPendingCall));
        assert_eq!(channel.take_reply(&mut []), Err(IpcError::NoPendingCall));
        channel.call(&[], &[]).unwrap();
        assert!(!channel.is_idle());
        assert_eq!(channel.call(&[], &[]), Err(IpcError::AlreadyPending));
        let mut out = [0u8; MAX_MESSAGE_BYTES];
        let mut handles = [0u32; MAX_TRANSFERRED_HANDLES];
        channel.recv(&mut out, &mut handles).unwrap();
        assert_eq!(channel.reply(&[9, 8]), Ok(()));
        let mut reply = [0u8; MAX_MESSAGE_BYTES];
        assert_eq!(channel.take_reply(&mut reply), Ok(2));
        assert_eq!(&reply[..2], &[9, 8]);
        assert!(channel.is_idle());
    }
}
