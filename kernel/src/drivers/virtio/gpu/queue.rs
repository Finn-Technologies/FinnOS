//! VirtIO-GPU control-queue policy over the bounded split-virtqueue.

use crate::drivers::virtio::split_queue::{
    QueueBuffer, QueueError, QueuePublication, QueuedChain, ReclaimedChain, SplitVirtqueue,
    SplitVirtqueueLayout, SplitVirtqueueStorage, TransportError, VirtqUsedElem, VirtqueueTransport,
};
use crate::drivers::virtio::transport::{VirtioPciTransport, VirtioPciTransportError};

use super::GpuDisplayBuffer;
use super::{
    VirtioGpuCtrlHdr, VirtioGpuMemEntry, VirtioGpuResourceAttachBacking, VirtioGpuResourceCreate2d,
    VirtioGpuResourceDetachBacking, VirtioGpuResourceFlush, VirtioGpuResourceUnref,
    VirtioGpuSetScanout, VirtioGpuTransferToHost2d,
};

/// Errors produced by the VirtIO-GPU control-queue adapter.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GpuControlQueueError {
    /// The underlying split queue rejected the operation.
    Queue(QueueError),
    /// A control request must be driver-readable.
    WritableRequestHead,
    /// The command buffer is too small for a VirtIO-GPU control header.
    RequestTooSmall,
    /// The response buffer is too small for a VirtIO-GPU control header.
    ResponseTooSmall,
    /// The response buffer must be device-writable.
    ReadOnlyResponse,
    /// A null command or response address was supplied.
    InvalidBufferAddress,
    /// The architecture-specific transport rejected publication.
    Transport(TransportError),
    /// A response carried an unexpected type or an unbounded payload.
    InvalidResponse,
    /// A response was structurally complete but had an unexpected type or
    /// payload length.
    ResponseShape {
        /// Device-written length.
        length: u32,
        /// First control word in the response.
        control_type: u32,
    },
}

/// Errors returned by the bounded GPU control-query bring-up path.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GpuControlQueryError {
    /// The queue operation failed.
    Queue(GpuControlQueueError),
    /// The modern VirtIO-PCI transport failed.
    Transport(VirtioPciTransportError),
    /// The device did not publish a used-ring completion before the bound.
    CompletionTimeout,
    /// The presentation parameters could not be represented safely.
    InvalidPresentationParameters,
}

/// Buffer addresses used by the bounded control-command path.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GpuControlBufferAddresses {
    /// Guest-physical address of the request buffer.
    pub request: u64,
    /// Guest-physical address of the response buffer.
    pub response: u64,
}

/// Parameters for one bounded 2D presentation sequence.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Gpu2dPresentation {
    /// Guest-assigned resource ID.
    pub resource_id: u32,
    /// Guest-physical address of the backing framebuffer.
    pub framebuffer_address: u64,
    /// Surface width in pixels.
    pub width: u32,
    /// Surface height in pixels.
    pub height: u32,
    /// Source stride in pixels.
    pub stride: u32,
}

impl Gpu2dPresentation {
    /// Build presentation parameters from a live owned display buffer.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueryError::InvalidPresentationParameters`] when
    /// the resource ID is zero or the buffer has already been released.
    pub fn from_display_buffer(
        resource_id: u32,
        buffer: &GpuDisplayBuffer,
    ) -> Result<Self, GpuControlQueryError> {
        if resource_id == 0 || buffer.is_released() {
            return Err(GpuControlQueryError::InvalidPresentationParameters);
        }
        Ok(Self {
            resource_id,
            framebuffer_address: buffer
                .backing_address()
                .ok_or(GpuControlQueryError::InvalidPresentationParameters)?,
            width: buffer.width(),
            height: buffer.height(),
            stride: buffer.stride(),
        })
    }
}

/// Lifecycle state of one bounded VirtIO-GPU 2D resource.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Gpu2dResourceState {
    /// No create command has completed for the resource.
    Unbound,
    /// The device accepted resource creation.
    Created,
    /// The device accepted the backing attachment.
    BackingAttached,
    /// The full source frame has been transferred to host GPU memory.
    ContentTransferred,
    /// The resource is bound to the primary scanout.
    ScanoutBound,
    /// The initial full-frame flush has completed.
    Flushed,
    /// The scanout has been unbound from the resource.
    ScanoutDetached,
    /// The guest backing mapping has been detached.
    BackingDetached,
    /// The resource reference has been released.
    Unreffed,
}

/// Kernel-owned metadata for one bounded 2D display resource.
///
/// The current desktop path still points at the UEFI/ramfb framebuffer. This
/// type owns the protocol lifecycle and validates that backing range; a future
/// allocator/resource broker can replace the address without changing the
/// command contract.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Gpu2dResource {
    /// Guest-assigned resource ID.
    pub resource_id: u32,
    /// Guest-physical address of the backing framebuffer.
    pub backing_address: u64,
    /// Surface width in pixels.
    pub width: u32,
    /// Surface height in pixels.
    pub height: u32,
    /// Source stride in pixels.
    pub stride: u32,
    /// Full backing length in bytes.
    pub backing_length: u32,
    /// Current protocol lifecycle state.
    state: Gpu2dResourceState,
}

/// Result of one initial presentation and an optional follow-up damage frame.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Gpu2dPresentationReport {
    /// Number of commands completed while establishing the first frame.
    pub initial_commands: u32,
    /// Number of commands completed for the follow-up damage frame.
    pub followup_commands: u32,
}

/// Result of one bounded 2D teardown sequence.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Gpu2dTeardownReport {
    /// Commands completed during teardown.
    pub commands: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Gpu2dTeardownAction {
    DisableScanout,
    DetachBacking,
    UnrefResource,
    Complete,
}

const fn next_teardown_action(
    state: Gpu2dResourceState,
) -> Result<Gpu2dTeardownAction, GpuControlQueryError> {
    match state {
        Gpu2dResourceState::Flushed => Ok(Gpu2dTeardownAction::DisableScanout),
        Gpu2dResourceState::ScanoutDetached => Ok(Gpu2dTeardownAction::DetachBacking),
        Gpu2dResourceState::BackingDetached => Ok(Gpu2dTeardownAction::UnrefResource),
        Gpu2dResourceState::Unreffed => Ok(Gpu2dTeardownAction::Complete),
        _ => Err(GpuControlQueryError::InvalidPresentationParameters),
    }
}

const fn advance_teardown_state(
    state: Gpu2dResourceState,
    action: Gpu2dTeardownAction,
) -> Gpu2dResourceState {
    match (state, action) {
        (Gpu2dResourceState::Flushed, Gpu2dTeardownAction::DisableScanout) => {
            Gpu2dResourceState::ScanoutDetached
        }
        (Gpu2dResourceState::ScanoutDetached, Gpu2dTeardownAction::DetachBacking) => {
            Gpu2dResourceState::BackingDetached
        }
        (Gpu2dResourceState::BackingDetached, Gpu2dTeardownAction::UnrefResource) => {
            Gpu2dResourceState::Unreffed
        }
        _ => state,
    }
}

fn commit_teardown_state(
    state: Gpu2dResourceState,
    action: Gpu2dTeardownAction,
    completion: Result<(), GpuControlQueryError>,
) -> Result<Gpu2dResourceState, GpuControlQueryError> {
    completion?;
    Ok(advance_teardown_state(state, action))
}

impl Gpu2dResource {
    /// Construct a validated 2D resource descriptor.
    #[must_use]
    pub fn new(
        resource_id: u32,
        backing_address: u64,
        width: u32,
        height: u32,
        stride: u32,
    ) -> Option<Self> {
        if resource_id == 0 || backing_address == 0 || width == 0 || height == 0 || stride < width {
            return None;
        }
        let backing_length = u64::from(stride)
            .checked_mul(u64::from(height))
            .and_then(|value| value.checked_mul(4))
            .and_then(|value| u32::try_from(value).ok())?;
        backing_address.checked_add(u64::from(backing_length))?;
        Some(Self {
            resource_id,
            backing_address,
            width,
            height,
            stride,
            backing_length,
            state: Gpu2dResourceState::Unbound,
        })
    }

    /// Build a resource descriptor from a live owned display buffer.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueryError::InvalidPresentationParameters`] when
    /// the resource ID or backing range is invalid.
    pub fn from_display_buffer(
        resource_id: u32,
        buffer: &GpuDisplayBuffer,
    ) -> Result<Self, GpuControlQueryError> {
        if buffer.is_released() {
            return Err(GpuControlQueryError::InvalidPresentationParameters);
        }
        let presentation = Gpu2dPresentation::from_display_buffer(resource_id, buffer)?;
        let mut resource = Self::new(
            presentation.resource_id,
            presentation.framebuffer_address,
            presentation.width,
            presentation.height,
            presentation.stride,
        )
        .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
        // An owned buffer is attached as its complete page-rounded range. The
        // visible stride/height length remains the basis for damage offsets.
        resource.backing_length = buffer.backing_byte_len();
        Ok(resource)
    }

    /// Return the current protocol lifecycle state.
    #[must_use]
    pub const fn state(&self) -> Gpu2dResourceState {
        self.state
    }

    /// Return whether the device has completed resource unref.
    ///
    /// The owner may reclaim guest backing only when this returns `true`.
    #[must_use]
    pub const fn device_resource_released(&self) -> bool {
        matches!(self.state, Gpu2dResourceState::Unreffed)
    }

    /// Construct a test fixture with an explicit lifecycle state.
    #[cfg(test)]
    const fn with_state(mut self, state: Gpu2dResourceState) -> Self {
        self.state = state;
        self
    }

    /// Return the byte offset of a damage rectangle in the source backing.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueryError::InvalidPresentationParameters`] when
    /// the rectangle is outside the surface or its source range overflows the
    /// backing allocation.
    pub fn damage_source_offset(
        &self,
        damage: &super::VirtioGpuRect,
    ) -> Result<u64, GpuControlQueryError> {
        if !damage.is_valid_for(self.width, self.height) {
            return Err(GpuControlQueryError::InvalidPresentationParameters);
        }
        let offset = u64::from(damage.y)
            .checked_mul(u64::from(self.stride))
            .and_then(|value| value.checked_add(u64::from(damage.x)))
            .and_then(|value| value.checked_mul(4))
            .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
        let row_span = u64::from(damage.height - 1)
            .checked_mul(u64::from(self.stride))
            .and_then(|value| value.checked_mul(4))
            .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
        let end = offset
            .checked_add(row_span)
            .and_then(|value| value.checked_add(u64::from(damage.width) * 4))
            .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
        if end > u64::from(self.backing_length) {
            return Err(GpuControlQueryError::InvalidPresentationParameters);
        }
        Ok(offset)
    }

    fn validate_damage(&self, damage: &super::VirtioGpuRect) -> Result<(), GpuControlQueryError> {
        self.damage_source_offset(damage).map(|_| ())
    }
}

impl From<GpuControlQueueError> for GpuControlQueryError {
    fn from(error: GpuControlQueueError) -> Self {
        Self::Queue(error)
    }
}

impl From<VirtioPciTransportError> for GpuControlQueryError {
    fn from(error: VirtioPciTransportError) -> Self {
        Self::Transport(error)
    }
}

/// Maximum polling iterations for the bounded control-query completion path.
pub const CONTROL_QUERY_MAX_POLLS: usize = 100_000;

/// How the driver was told to wait for a control-queue completion.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CompletionMode {
    /// No interrupt was observed; the used ring is sampled in a bounded loop.
    Polled,
    /// A vector is bound and the ISR structure was observed for this wait.
    InterruptSignalled,
}

/// One completion observation supplied by the caller.
///
/// The used ring remains the only authority for whether a completion actually
/// exists. An interrupt observation decides *when* to look, never *whether* a
/// descriptor was returned, so a spurious or duplicated signal cannot
/// manufacture a completion.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CompletionSignal {
    /// Sample the used ring again.
    Poll,
    /// The device signalled a virtqueue interrupt for the observed queue.
    Interrupted,
    /// No interrupt is available for this queue.
    Unsupported,
}

/// Decide how to wait for the next completion.
///
/// When a queue has no bound vector the result is [`CompletionMode::Polled`],
/// which is the honest state for a device that cannot signal completion. When
/// a vector is bound and a signal was observed, the wait becomes
/// [`CompletionMode::InterruptSignalled`] so the caller re-checks the used ring
/// immediately instead of spinning.
#[must_use]
pub const fn completion_mode(signal: CompletionSignal, vector_bound: bool) -> CompletionMode {
    if vector_bound && matches!(signal, CompletionSignal::Interrupted) {
        CompletionMode::InterruptSignalled
    } else {
        CompletionMode::Polled
    }
}

/// Fixed storage for one control-queue display-information query.
#[repr(C, align(4096))]
pub struct GpuControlSmokeStorage {
    /// Split descriptor table bytes.
    pub descriptors: [u8; 128],
    /// Split available-ring bytes.
    pub available: [u8; 22],
    /// Padding to the used-ring's required four-byte alignment.
    pub used_alignment_padding: [u8; 2],
    /// Split used-ring bytes.
    pub used: [u8; 70],
    /// Padding to keep request and response buffers naturally aligned.
    pub request_alignment_padding: [u8; 2],
    /// Driver-readable request bytes.
    pub request: [u8; 128],
    /// Device-writable response bytes.
    pub response: [u8; 4096],
}

impl GpuControlSmokeStorage {
    /// Construct zeroed storage for the fixed eight-entry control queue.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            descriptors: [0; 128],
            available: [0; 22],
            used_alignment_padding: [0; 2],
            used: [0; 70],
            request_alignment_padding: [0; 2],
            request: [0; 128],
            response: [0; 4096],
        }
    }

    /// Build a fixed split-ring layout from guest-physical ring addresses.
    #[must_use]
    pub fn queue_layout_at(
        &self,
        descriptor: u64,
        available: u64,
        used: u64,
        event_index: bool,
    ) -> Option<SplitVirtqueueLayout> {
        SplitVirtqueueLayout::new_with_event_index(0, 8, event_index, descriptor, available, used)
            .ok()
    }
}

impl Default for GpuControlSmokeStorage {
    fn default() -> Self {
        Self::new()
    }
}

static mut CONTROL_SMOKE_STORAGE: GpuControlSmokeStorage = GpuControlSmokeStorage::new();

/// Return the single kernel-owned control-query storage object.
///
/// # Safety
///
/// The caller must ensure that only one kernel control path borrows this
/// storage at a time. The returned object is supervisor-owned and identity
/// mapped by the kernel image.
#[must_use]
#[allow(unsafe_code)]
pub unsafe fn control_smoke_storage() -> &'static mut GpuControlSmokeStorage {
    // SAFETY: The caller upholds the single-owner contract above.
    unsafe { &mut *core::ptr::addr_of_mut!(CONTROL_SMOKE_STORAGE) }
}

/// Submit and poll one VirtIO-GPU `GET_DISPLAY_INFO` control request.
///
/// The queue is assumed to have been configured and the device to have been
/// moved to `DRIVER_OK` by the caller. Polling is deliberately bounded; a
/// timeout is reported without reclaiming an in-flight descriptor.
///
/// # Errors
///
/// Returns [`GpuControlQueryError`] for serialization, queue publication,
/// transport notification, malformed used-ring completion, or a bounded
/// completion timeout.
pub fn submit_display_info_query<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &mut [u8],
    response: &mut [u8],
    request_address: u64,
    response_address: u64,
) -> Result<u32, GpuControlQueryError> {
    let header = super::VirtioGpuGetDisplayInfo::new().hdr;
    let request_len = encode_control_request(header, &[], request)?;
    let completion_length = submit_control_command(
        transport,
        queue,
        storage,
        &request[..request_len],
        response,
        GpuControlBufferAddresses {
            request: request_address,
            response: response_address,
        },
        super::VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    )?;
    Ok(decode_display_info_response(
        &response[..completion_length as usize],
    )?)
}

/// Submit one serialized control command and wait for its bounded completion.
///
/// # Errors
///
/// Returns [`GpuControlQueryError`] for invalid buffers, publication errors,
/// malformed completions, unexpected response types, or timeout.
pub fn submit_control_command<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &[u8],
    response: &mut [u8],
    addresses: GpuControlBufferAddresses,
    expected_type: u32,
) -> Result<u32, GpuControlQueryError> {
    let header_size = core::mem::size_of::<VirtioGpuCtrlHdr>();
    if request.len() < header_size {
        return Err(GpuControlQueryError::Queue(
            GpuControlQueueError::RequestTooSmall,
        ));
    }
    if response.len() < header_size {
        return Err(GpuControlQueryError::Queue(
            GpuControlQueueError::ResponseTooSmall,
        ));
    }
    let request_length = u32::try_from(request.len())
        .map_err(|_| GpuControlQueryError::Queue(GpuControlQueueError::RequestTooSmall))?;
    let response_length = u32::try_from(response.len())
        .map_err(|_| GpuControlQueryError::Queue(GpuControlQueueError::ResponseTooSmall))?;
    queue.queue_command(
        QueueBuffer::new(addresses.request, request_length, false),
        QueueBuffer::new(addresses.response, response_length, true),
    )?;
    queue.publish_driver_state(storage)?;
    queue
        .publish(transport)?
        .ok_or(GpuControlQueryError::Queue(GpuControlQueueError::Queue(
            QueueError::InvalidUsedIndex(0),
        )))?;
    // An interrupt only changes how the wait proceeds; the used ring below
    // remains the sole authority for whether a descriptor was returned.
    let vector_bound = matches!(
        transport.queue_interrupt(0)?,
        crate::drivers::virtio::transport::QueueInterrupt::Signalled { .. }
    );
    let mut polls = 0usize;
    loop {
        if let Some(completion) = queue.reclaim_next(storage)? {
            let header_length = u32::try_from(header_size)
                .map_err(|_| GpuControlQueryError::InvalidPresentationParameters)?;
            if completion.length < header_length || completion.length > response_length {
                return Err(GpuControlQueryError::Queue(
                    GpuControlQueueError::InvalidResponse,
                ));
            }
            let response_slice = &response[..completion.length as usize];
            let response_header = decode_control_response(response_slice)?;
            if response_header.req_type != expected_type {
                return Err(GpuControlQueryError::Queue(
                    GpuControlQueueError::ResponseShape {
                        length: completion.length,
                        control_type: response_header.req_type,
                    },
                ));
            }
            return Ok(completion.length);
        }
        if vector_bound {
            // A bound vector means the device can signal completion. Reading
            // the ISR is destructive, so it is sampled at most once per poll
            // and only used to decide whether the wait was interrupt-driven.
            //
            // A device that reports no pending bit, or a transport that cannot
            // read the ISR at all, both mean "no interrupt was observed"; the
            // wait stays polled rather than claiming a signal that never
            // happened.
            let signal = match transport.read_interrupt_status() {
                Ok(flags) if flags.queue_completion_pending() => CompletionSignal::Interrupted,
                Err(
                    crate::drivers::virtio::transport::VirtioPciTransportError::InterruptStatusUnsupported,
                ) => CompletionSignal::Unsupported,
                Ok(_) | Err(_) => CompletionSignal::Poll,
            };
            match completion_mode(signal, vector_bound) {
                CompletionMode::InterruptSignalled => core::hint::spin_loop(),
                CompletionMode::Polled => {}
            }
        }
        if polls >= CONTROL_QUERY_MAX_POLLS {
            return Err(GpuControlQueryError::CompletionTimeout);
        }
        polls += 1;
        core::hint::spin_loop();
    }
}

#[allow(clippy::too_many_arguments)]
fn submit_nodata_command<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &mut [u8],
    response: &mut [u8],
    addresses: GpuControlBufferAddresses,
    header: VirtioGpuCtrlHdr,
    body: &[u8],
) -> Result<(), GpuControlQueryError> {
    let request_length = encode_control_request(header, body, request)?;
    submit_control_command(
        transport,
        queue,
        storage,
        &request[..request_length],
        response,
        addresses,
        super::VIRTIO_GPU_RESP_OK_NODATA,
    )?;
    Ok(())
}

/// Submit an initial 2D frame and an optional follow-up damage frame.
///
/// The first frame completes resource creation, backing attachment, full
/// source transfer, scanout binding, and flush. Once the resource is bound,
/// `followup_damage` submits only a source transfer and flush for that
/// rectangle. This is a bounded protocol session, not a continuously
/// running compositor service.
///
/// # Errors
///
/// Returns [`GpuControlQueryError`] for invalid resource or damage bounds,
/// an invalid lifecycle transition, or a failed command completion.
#[allow(clippy::too_many_arguments, clippy::too_many_lines)]
pub fn submit_2d_resource_session<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &mut [u8],
    response: &mut [u8],
    addresses: GpuControlBufferAddresses,
    resource: &mut Gpu2dResource,
    followup_damage: Option<super::VirtioGpuRect>,
) -> Result<Gpu2dPresentationReport, GpuControlQueryError> {
    let full_rect = super::VirtioGpuRect {
        x: 0,
        y: 0,
        width: resource.width,
        height: resource.height,
    };
    let mut initial_commands = 0u32;

    while !matches!(
        resource.state,
        Gpu2dResourceState::Flushed
            | Gpu2dResourceState::ScanoutDetached
            | Gpu2dResourceState::BackingDetached
            | Gpu2dResourceState::Unreffed
    ) {
        match resource.state {
            Gpu2dResourceState::Unbound => {
                resource.validate_damage(&full_rect)?;
                let create = VirtioGpuResourceCreate2d {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
                    resource_id: resource.resource_id,
                    format: super::VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                    width: resource.width,
                    height: resource.height,
                };
                let body = encode_resource_create_2d_body(
                    create.resource_id,
                    create.format,
                    create.width,
                    create.height,
                );
                submit_nodata_command(
                    transport, queue, storage, request, response, addresses, create.hdr, &body,
                )?;
                resource.state = Gpu2dResourceState::Created;
                initial_commands += 1;
            }
            Gpu2dResourceState::Created => {
                let attach = VirtioGpuResourceAttachBacking {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING),
                    resource_id: resource.resource_id,
                    nr_entries: 1,
                };
                let entry = VirtioGpuMemEntry {
                    addr: resource.backing_address,
                    length: resource.backing_length,
                    padding: 0,
                };
                let body = encode_resource_attach_backing_body(
                    attach.resource_id,
                    attach.nr_entries,
                    entry,
                );
                submit_nodata_command(
                    transport, queue, storage, request, response, addresses, attach.hdr, &body,
                )?;
                resource.state = Gpu2dResourceState::BackingAttached;
                initial_commands += 1;
            }
            Gpu2dResourceState::BackingAttached => {
                resource.validate_damage(&full_rect)?;

                let transfer = VirtioGpuTransferToHost2d {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D),
                    rect: full_rect,
                    offset: 0,
                    resource_id: resource.resource_id,
                    padding: 0,
                };
                let body = encode_transfer_to_host_2d_body(
                    transfer.rect,
                    transfer.offset,
                    transfer.resource_id,
                    transfer.padding,
                );
                submit_nodata_command(
                    transport,
                    queue,
                    storage,
                    request,
                    response,
                    addresses,
                    transfer.hdr,
                    &body,
                )?;
                resource.state = Gpu2dResourceState::ContentTransferred;
                initial_commands += 1;
            }
            Gpu2dResourceState::ContentTransferred => {
                resource.validate_damage(&full_rect)?;

                let scanout = VirtioGpuSetScanout {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_SET_SCANOUT),
                    rect: full_rect,
                    scanout_id: 0,
                    resource_id: resource.resource_id,
                };
                let body =
                    encode_set_scanout_body(scanout.rect, scanout.scanout_id, scanout.resource_id);
                submit_nodata_command(
                    transport,
                    queue,
                    storage,
                    request,
                    response,
                    addresses,
                    scanout.hdr,
                    &body,
                )?;
                resource.state = Gpu2dResourceState::ScanoutBound;
                initial_commands += 1;
            }
            Gpu2dResourceState::ScanoutBound => {
                resource.validate_damage(&full_rect)?;

                let flush = VirtioGpuResourceFlush {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_FLUSH),
                    rect: full_rect,
                    resource_id: resource.resource_id,
                    padding: 0,
                };
                let body = encode_resource_flush_body(flush.rect, flush.resource_id, flush.padding);
                submit_nodata_command(
                    transport, queue, storage, request, response, addresses, flush.hdr, &body,
                )?;
                resource.state = Gpu2dResourceState::Flushed;
                initial_commands += 1;
            }
            Gpu2dResourceState::Flushed => {
                initial_commands = 0;
                break;
            }
            Gpu2dResourceState::ScanoutDetached
            | Gpu2dResourceState::BackingDetached
            | Gpu2dResourceState::Unreffed => {
                return Err(GpuControlQueryError::InvalidPresentationParameters);
            }
        }
    }

    let followup_commands = if let Some(damage) = followup_damage {
        if resource.state != Gpu2dResourceState::Flushed {
            return Err(GpuControlQueryError::InvalidPresentationParameters);
        }
        let offset = resource.damage_source_offset(&damage)?;
        let transfer = VirtioGpuTransferToHost2d {
            hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D),
            rect: damage,
            offset,
            resource_id: resource.resource_id,
            padding: 0,
        };
        let body = encode_transfer_to_host_2d_body(
            transfer.rect,
            transfer.offset,
            transfer.resource_id,
            transfer.padding,
        );
        submit_nodata_command(
            transport,
            queue,
            storage,
            request,
            response,
            addresses,
            transfer.hdr,
            &body,
        )?;
        let flush = VirtioGpuResourceFlush {
            hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_FLUSH),
            rect: damage,
            resource_id: resource.resource_id,
            padding: 0,
        };
        let body = encode_resource_flush_body(flush.rect, flush.resource_id, flush.padding);
        submit_nodata_command(
            transport, queue, storage, request, response, addresses, flush.hdr, &body,
        )?;
        2
    } else {
        0
    };

    Ok(Gpu2dPresentationReport {
        initial_commands,
        followup_commands,
    })
}

/// Submit the resumable teardown sequence for a presented 2D resource.
///
/// The only valid next action depends on the last successful completion:
/// scanout unbind, backing detach, resource unref, or complete. The caller
/// must not release the owned backing until [`Gpu2dResourceState::Unreffed`]
/// is observed.
///
/// # Errors
///
/// Returns [`GpuControlQueryError`] for an invalid lifecycle state or a
/// failed command completion. A failed command leaves the state at the last
/// successful stage. A retry is valid only after the caller has resolved any
/// completion still in flight from the failed attempt; a timeout does not make
/// blind resubmission safe.
#[allow(clippy::too_many_arguments)]
pub fn submit_2d_teardown_session<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &mut [u8],
    response: &mut [u8],
    addresses: GpuControlBufferAddresses,
    resource: &mut Gpu2dResource,
) -> Result<Gpu2dTeardownReport, GpuControlQueryError> {
    let full_rect = super::VirtioGpuRect {
        x: 0,
        y: 0,
        width: resource.width,
        height: resource.height,
    };
    let mut commands = 0u32;
    loop {
        let action = next_teardown_action(resource.state)?;
        match action {
            Gpu2dTeardownAction::DisableScanout => {
                let body = encode_set_scanout_body(full_rect, 0, 0);
                let completion = submit_nodata_command(
                    transport,
                    queue,
                    storage,
                    request,
                    response,
                    addresses,
                    VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_SET_SCANOUT),
                    &body,
                );
                resource.state = commit_teardown_state(resource.state, action, completion)?;
            }
            Gpu2dTeardownAction::DetachBacking => {
                let detach = VirtioGpuResourceDetachBacking {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING),
                    resource_id: resource.resource_id,
                    padding: 0,
                };
                let body = encode_resource_detach_backing_body(detach.resource_id);
                let completion = submit_nodata_command(
                    transport, queue, storage, request, response, addresses, detach.hdr, &body,
                );
                resource.state = commit_teardown_state(resource.state, action, completion)?;
            }
            Gpu2dTeardownAction::UnrefResource => {
                let unref = VirtioGpuResourceUnref {
                    hdr: VirtioGpuCtrlHdr::new(super::VIRTIO_GPU_CMD_RESOURCE_UNREF),
                    resource_id: resource.resource_id,
                    padding: 0,
                };
                let body = encode_resource_unref_body(unref.resource_id);
                let completion = submit_nodata_command(
                    transport, queue, storage, request, response, addresses, unref.hdr, &body,
                );
                resource.state = commit_teardown_state(resource.state, action, completion)?;
            }
            Gpu2dTeardownAction::Complete => {
                return Ok(Gpu2dTeardownReport { commands });
            }
        }
        commands = commands
            .checked_add(1)
            .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
    }
}

/// Submit a legacy one-frame 2D presentation from primitive parameters.
///
/// # Errors
///
/// Returns [`GpuControlQueryError`] for invalid dimensions or a failed
/// command completion.
pub fn submit_2d_presentation<M: crate::drivers::virtio::transport::VirtioPciMmio>(
    transport: &mut VirtioPciTransport<M>,
    queue: &mut GpuControlQueue,
    storage: &mut SplitVirtqueueStorage<'_>,
    request: &mut [u8],
    response: &mut [u8],
    addresses: GpuControlBufferAddresses,
    presentation: Gpu2dPresentation,
) -> Result<u32, GpuControlQueryError> {
    let mut resource = Gpu2dResource::new(
        presentation.resource_id,
        presentation.framebuffer_address,
        presentation.width,
        presentation.height,
        presentation.stride,
    )
    .ok_or(GpuControlQueryError::InvalidPresentationParameters)?;
    let report = submit_2d_resource_session(
        transport,
        queue,
        storage,
        request,
        response,
        addresses,
        &mut resource,
        None,
    )?;
    Ok(report.initial_commands)
}

/// Encode the request body for a 2D resource-create command.
#[must_use]
pub fn encode_resource_create_2d_body(
    resource_id: u32,
    format: u32,
    width: u32,
    height: u32,
) -> [u8; 16] {
    let mut body = [0u8; 16];
    body[0..4].copy_from_slice(&resource_id.to_le_bytes());
    body[4..8].copy_from_slice(&format.to_le_bytes());
    body[8..12].copy_from_slice(&width.to_le_bytes());
    body[12..16].copy_from_slice(&height.to_le_bytes());
    body
}

/// Encode the request body for a resource-backing attach command.
#[must_use]
pub fn encode_resource_attach_backing_body(
    resource_id: u32,
    nr_entries: u32,
    entry: super::VirtioGpuMemEntry,
) -> [u8; 24] {
    let mut body = [0u8; 24];
    body[0..4].copy_from_slice(&resource_id.to_le_bytes());
    body[4..8].copy_from_slice(&nr_entries.to_le_bytes());
    body[8..16].copy_from_slice(&entry.addr.to_le_bytes());
    body[16..20].copy_from_slice(&entry.length.to_le_bytes());
    body[20..24].copy_from_slice(&entry.padding.to_le_bytes());
    body
}

/// Encode the request body for a resource-backing detach command.
#[must_use]
pub fn encode_resource_detach_backing_body(resource_id: u32) -> [u8; 8] {
    let mut body = [0u8; 8];
    body[0..4].copy_from_slice(&resource_id.to_le_bytes());
    body
}

/// Encode the request body for a resource-unref command.
#[must_use]
pub fn encode_resource_unref_body(resource_id: u32) -> [u8; 8] {
    let mut body = [0u8; 8];
    body[0..4].copy_from_slice(&resource_id.to_le_bytes());
    body
}

/// Encode the request body for a host-transfer command.
#[must_use]
pub fn encode_transfer_to_host_2d_body(
    rect: super::VirtioGpuRect,
    offset: u64,
    resource_id: u32,
    padding: u32,
) -> [u8; 32] {
    let mut body = [0u8; 32];
    body[0..4].copy_from_slice(&rect.x.to_le_bytes());
    body[4..8].copy_from_slice(&rect.y.to_le_bytes());
    body[8..12].copy_from_slice(&rect.width.to_le_bytes());
    body[12..16].copy_from_slice(&rect.height.to_le_bytes());
    body[16..24].copy_from_slice(&offset.to_le_bytes());
    body[24..28].copy_from_slice(&resource_id.to_le_bytes());
    body[28..32].copy_from_slice(&padding.to_le_bytes());
    body
}

/// Encode the request body for a scanout-bind command.
#[must_use]
pub fn encode_set_scanout_body(
    rect: super::VirtioGpuRect,
    scanout_id: u32,
    resource_id: u32,
) -> [u8; 24] {
    let mut body = [0u8; 24];
    body[0..4].copy_from_slice(&rect.x.to_le_bytes());
    body[4..8].copy_from_slice(&rect.y.to_le_bytes());
    body[8..12].copy_from_slice(&rect.width.to_le_bytes());
    body[12..16].copy_from_slice(&rect.height.to_le_bytes());
    body[16..20].copy_from_slice(&scanout_id.to_le_bytes());
    body[20..24].copy_from_slice(&resource_id.to_le_bytes());
    body
}

/// Encode the request body for a resource-flush command.
#[must_use]
pub fn encode_resource_flush_body(
    rect: super::VirtioGpuRect,
    resource_id: u32,
    padding: u32,
) -> [u8; 24] {
    let mut body = [0u8; 24];
    body[0..4].copy_from_slice(&rect.x.to_le_bytes());
    body[4..8].copy_from_slice(&rect.y.to_le_bytes());
    body[8..12].copy_from_slice(&rect.width.to_le_bytes());
    body[12..16].copy_from_slice(&rect.height.to_le_bytes());
    body[16..20].copy_from_slice(&resource_id.to_le_bytes());
    body[20..24].copy_from_slice(&padding.to_le_bytes());
    body
}

/// Bounded VirtIO-GPU control queue for request/response command chains.
#[derive(Clone, Debug)]
pub struct GpuControlQueue {
    queue: SplitVirtqueue,
}

/// Encode a GPU control request into a bounded little-endian byte buffer.
///
/// # Errors
///
/// Returns [`GpuControlQueueError::RequestTooSmall`] when `output` cannot
/// hold the request header and optional body.
pub fn encode_control_request(
    header: VirtioGpuCtrlHdr,
    body: &[u8],
    output: &mut [u8],
) -> Result<usize, GpuControlQueueError> {
    let header_size = core::mem::size_of::<VirtioGpuCtrlHdr>();
    let body_size = body.len();
    let Some(total) = header_size.checked_add(body_size) else {
        return Err(GpuControlQueueError::RequestTooSmall);
    };
    if output.len() < total {
        return Err(GpuControlQueueError::RequestTooSmall);
    }
    output[..header_size].copy_from_slice(&header.encode());
    output[header_size..total].copy_from_slice(body);
    Ok(total)
}

/// Decode a GPU control response header from a little-endian byte buffer.
///
/// # Errors
///
/// Returns [`GpuControlQueueError::ResponseTooSmall`] when fewer than one
/// complete header is available.
pub fn decode_control_response(input: &[u8]) -> Result<VirtioGpuCtrlHdr, GpuControlQueueError> {
    let header_size = core::mem::size_of::<VirtioGpuCtrlHdr>();
    if input.len() < header_size {
        return Err(GpuControlQueueError::ResponseTooSmall);
    }
    let mut bytes = [0u8; 24];
    bytes.copy_from_slice(&input[..header_size]);
    Ok(VirtioGpuCtrlHdr::decode(bytes))
}

/// Decode the display-information response payload after its control header.
///
/// # Errors
///
/// Returns [`GpuControlQueueError::ResponseTooSmall`] for a truncated payload,
/// or [`GpuControlQueueError::InvalidResponse`] when the response type does
/// not match [`super::VIRTIO_GPU_RESP_OK_DISPLAY_INFO`] or the scanout count is
/// unbounded.
pub fn decode_display_info_response(input: &[u8]) -> Result<u32, GpuControlQueueError> {
    let header_size = core::mem::size_of::<VirtioGpuCtrlHdr>();
    let scanout_size = core::mem::size_of::<super::VirtioGpuDisplayOne>();
    let required = header_size
        .checked_add(
            scanout_size
                .checked_mul(super::VIRTIO_GPU_MAX_SCANOUTS)
                .ok_or(GpuControlQueueError::InvalidResponse)?,
        )
        .ok_or(GpuControlQueueError::InvalidResponse)?;
    if input.len() < required {
        return Err(GpuControlQueueError::ResponseTooSmall);
    }
    let mut header = [0u8; 24];
    header.copy_from_slice(&input[..header_size]);
    if VirtioGpuCtrlHdr::decode(header).req_type != super::VIRTIO_GPU_RESP_OK_DISPLAY_INFO {
        return Err(GpuControlQueueError::InvalidResponse);
    }
    let mut enabled = 0u32;
    for index in 0..super::VIRTIO_GPU_MAX_SCANOUTS {
        let offset = header_size + index * scanout_size;
        let enabled_byte = offset + core::mem::size_of::<super::VirtioGpuRect>();
        let value = u32::from_le_bytes([
            input[enabled_byte],
            input[enabled_byte + 1],
            input[enabled_byte + 2],
            input[enabled_byte + 3],
        ]);
        if value != 0 {
            enabled = enabled.saturating_add(1);
        }
    }
    Ok(enabled)
}

impl GpuControlQueue {
    /// Create a control queue from a validated split-ring layout.
    #[must_use]
    pub const fn new(layout: SplitVirtqueueLayout) -> Self {
        Self {
            queue: SplitVirtqueue::new(layout),
        }
    }

    /// Return the split-ring layout.
    #[must_use]
    pub const fn layout(&self) -> &SplitVirtqueueLayout {
        self.queue.layout()
    }

    /// Return the number of descriptors currently owned by the device or an
    /// outstanding completion.
    #[must_use]
    pub fn in_flight_descriptor_count(&self) -> usize {
        self.queue.in_flight_count()
    }

    /// Serialize the queue's driver-owned descriptor and available-ring state.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueueError::Queue`] when the caller-provided ring
    /// storage is too small for this queue.
    pub fn publish_driver_state(
        &self,
        storage: &mut SplitVirtqueueStorage<'_>,
    ) -> Result<(), GpuControlQueueError> {
        storage
            .publish_driver_state(&self.queue)
            .map_err(GpuControlQueueError::Queue)
    }

    /// Queue one driver-readable command and one device-writable response.
    ///
    /// This validates the control protocol's direction and minimum header
    /// sizes but does not serialize command data. The caller owns the backing
    /// memory and must make it resident for the transport publication.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueueError`] without changing queue ownership when
    /// either buffer violates the control protocol or the queue is exhausted.
    pub fn queue_command(
        &mut self,
        request: QueueBuffer,
        response: QueueBuffer,
    ) -> Result<QueuedChain, GpuControlQueueError> {
        if request.address == 0 || response.address == 0 {
            return Err(GpuControlQueueError::InvalidBufferAddress);
        }
        if request.device_writable {
            return Err(GpuControlQueueError::WritableRequestHead);
        }
        if request.length
            < u32::try_from(core::mem::size_of::<VirtioGpuCtrlHdr>()).unwrap_or(u32::MAX)
        {
            return Err(GpuControlQueueError::RequestTooSmall);
        }
        if response.length
            < u32::try_from(core::mem::size_of::<VirtioGpuCtrlHdr>()).unwrap_or(u32::MAX)
        {
            return Err(GpuControlQueueError::ResponseTooSmall);
        }
        if !response.device_writable {
            return Err(GpuControlQueueError::ReadOnlyResponse);
        }
        self.queue
            .enqueue_chain(&[request, response])
            .map_err(GpuControlQueueError::Queue)
    }

    /// Publish newly queued control commands through an architecture-specific
    /// transport backend.
    ///
    /// # Errors
    ///
    /// Returns the transport error and leaves publication pending for retry.
    pub fn publish<T: VirtqueueTransport>(
        &mut self,
        transport: &mut T,
    ) -> Result<Option<QueuePublication>, GpuControlQueueError> {
        self.queue
            .publish(transport)
            .map_err(GpuControlQueueError::Transport)
    }

    /// Reclaim one completed control request and its response descriptor.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueueError::Queue`] for malformed or out-of-order
    /// used-ring entries.
    pub fn reclaim(
        &mut self,
        used_index: u16,
        element: VirtqUsedElem,
    ) -> Result<ReclaimedChain, GpuControlQueueError> {
        self.queue
            .reclaim(used_index, element)
            .map_err(GpuControlQueueError::Queue)
    }

    /// Poll the device-owned used ring and reclaim the next completion.
    ///
    /// # Errors
    ///
    /// Returns [`GpuControlQueueError::Queue`] for malformed or out-of-order
    /// used-ring entries.
    pub fn reclaim_next(
        &mut self,
        storage: &SplitVirtqueueStorage<'_>,
    ) -> Result<Option<ReclaimedChain>, GpuControlQueueError> {
        let published = match storage.used_index(&self.queue) {
            Ok(index) => index,
            Err(QueueError::InvalidUsedIndex(0)) => return Ok(None),
            Err(error) => return Err(GpuControlQueueError::Queue(error)),
        };
        let expected = self.queue.used_index().wrapping_add(1);
        if published == self.queue.used_index() {
            return Ok(None);
        }
        let element = storage
            .used_element(&self.queue, expected)
            .map_err(GpuControlQueueError::Queue)?;
        self.reclaim(expected, element).map(Some)
    }

    /// Reset the queue while retaining its ring layout.
    pub fn reset(&mut self) {
        self.queue.reset();
    }
}

#[cfg(test)]
mod tests {
    use super::{GpuControlQueue, decode_control_response, encode_control_request};
    use crate::drivers::virtio::gpu::VirtioGpuCtrlHdr;
    use crate::drivers::virtio::gpu::{
        VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
        VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING, VIRTIO_GPU_CMD_RESOURCE_FLUSH,
        VIRTIO_GPU_CMD_RESOURCE_UNREF, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
    };
    use crate::drivers::virtio::split_queue::{
        QueueBuffer, QueuePublication, SplitVirtqueueLayout, TransportError, VirtqueueTransport,
    };

    #[derive(Default)]
    struct RecordingTransport {
        publication: Option<QueuePublication>,
    }

    impl VirtqueueTransport for RecordingTransport {
        fn notify(&mut self, publication: QueuePublication) -> Result<(), TransportError> {
            self.publication = Some(publication);
            Ok(())
        }
    }

    fn queue() -> GpuControlQueue {
        GpuControlQueue::new(
            SplitVirtqueueLayout::new(0, 8, 0x10_0000, 0x20_0000, 0x30_0000).unwrap(),
        )
    }

    #[test]
    fn command_direction_and_sizes_are_validated_transactionally() {
        let mut queue = queue();
        assert!(
            queue
                .queue_command(
                    QueueBuffer::new(0x40_0000, 24, true),
                    QueueBuffer::new(0x41_0000, 24, true),
                )
                .is_err()
        );
        assert!(
            queue
                .queue_command(
                    QueueBuffer::new(0x40_0000, 23, false),
                    QueueBuffer::new(0x41_0000, 24, true),
                )
                .is_err()
        );
        assert!(
            queue
                .queue_command(
                    QueueBuffer::new(0x40_0000, 24, false),
                    QueueBuffer::new(0x41_0000, 24, false),
                )
                .is_err()
        );
        assert_eq!(queue.in_flight_descriptor_count(), 0);
    }

    #[test]
    fn command_publish_and_reclaim_use_two_descriptors() {
        let mut queue = queue();
        let mut transport = RecordingTransport::default();
        let chain = queue
            .queue_command(
                QueueBuffer::new(0x40_0000, 64, false),
                QueueBuffer::new(0x41_0000, 24, true),
            )
            .unwrap();
        assert_eq!(chain.descriptor_count, 2);
        assert!(queue.publish(&mut transport).unwrap().is_some());
        let reclaimed = queue
            .reclaim(
                1,
                crate::drivers::virtio::split_queue::VirtqUsedElem {
                    id: chain.head.into(),
                    length: 24,
                },
            )
            .unwrap();
        assert_eq!(reclaimed.descriptor_count, 2);
        assert_eq!(queue.in_flight_descriptor_count(), 0);
    }

    #[test]
    fn control_request_and_response_headers_round_trip() {
        let header =
            VirtioGpuCtrlHdr::new(crate::drivers::virtio::gpu::VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
        let mut request = [0u8; 32];
        assert_eq!(
            encode_control_request(header, &[1, 2, 3], &mut request),
            Ok(27)
        );
        assert_eq!(decode_control_response(&request), Ok(header));
        assert!(encode_control_request(header, &[1; 32], &mut [0u8; 24]).is_err());
    }

    #[test]
    fn two_dimensional_command_bodies_match_wire_layout() {
        let rect = crate::drivers::virtio::gpu::VirtioGpuRect {
            x: 0,
            y: 0,
            width: 320,
            height: 200,
        };
        let entry = crate::drivers::virtio::gpu::VirtioGpuMemEntry {
            addr: 0x8000_0000,
            length: 320 * 200 * 4,
            padding: 0,
        };
        let mut request = [0u8; 128];
        let create = super::encode_resource_create_2d_body(7, 2, 320, 200);
        assert_eq!(
            super::encode_control_request(
                VirtioGpuCtrlHdr::new(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D),
                &create,
                &mut request,
            ),
            Ok(40)
        );
        assert_eq!(&request[24..28], &7u32.to_le_bytes());

        let attach = super::encode_resource_attach_backing_body(7, 1, entry);
        assert_eq!(attach.len(), 24);
        assert_eq!(&attach[8..16], &entry.addr.to_le_bytes());
        assert_eq!(&attach[16..20], &entry.length.to_le_bytes());

        let transfer = super::encode_transfer_to_host_2d_body(rect, 0, 7, 0);
        assert_eq!(transfer.len(), 32);
        assert_eq!(&transfer[8..12], &320u32.to_le_bytes());
        assert_eq!(&transfer[24..28], &7u32.to_le_bytes());

        let scanout = super::encode_set_scanout_body(rect, 0, 7);
        assert_eq!(&scanout[16..20], &0u32.to_le_bytes());
        assert_eq!(&scanout[20..24], &7u32.to_le_bytes());

        let detach = super::encode_resource_detach_backing_body(7);
        assert_eq!(detach.len(), 8);
        assert_eq!(&detach[0..4], &7u32.to_le_bytes());

        let unref = super::encode_resource_unref_body(7);
        assert_eq!(unref.len(), 8);
        assert_eq!(&unref[0..4], &7u32.to_le_bytes());

        let flush = super::encode_resource_flush_body(rect, 7, 0);
        assert_eq!(&flush[16..20], &7u32.to_le_bytes());
        assert_eq!(VIRTIO_GPU_CMD_RESOURCE_UNREF, 0x0102);
        assert_eq!(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, 0x0106);
        assert_eq!(VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING, 0x0107);
        assert_eq!(VIRTIO_GPU_CMD_RESOURCE_FLUSH, 0x0104);
        assert_eq!(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D, 0x0105);
    }

    #[test]
    fn resource_bounds_and_damage_offsets_are_validated() {
        let resource = super::Gpu2dResource::new(7, 0x8000_0000, 320, 200, 400).unwrap();
        assert_eq!(resource.backing_length, 400 * 200 * 4);
        assert_eq!(resource.state(), super::Gpu2dResourceState::Unbound);
        let damage = crate::drivers::virtio::gpu::VirtioGpuRect {
            x: 8,
            y: 12,
            width: 16,
            height: 4,
        };
        assert_eq!(
            resource.damage_source_offset(&damage),
            Ok((12 * 400 + 8) * 4)
        );
        assert!(
            resource
                .damage_source_offset(&crate::drivers::virtio::gpu::VirtioGpuRect {
                    x: 319,
                    y: 0,
                    width: 2,
                    height: 1,
                })
                .is_err()
        );
        assert!(super::Gpu2dResource::new(0, 0x8000_0000, 320, 200, 400).is_none());
        assert!(super::Gpu2dResource::new(7, 0x8000_0000, 320, 200, 319).is_none());
        assert!(super::Gpu2dResource::new(7, u64::MAX, 320, 200, 400).is_none());
        assert!(super::Gpu2dResource::new(7, 0x8000_0000, u32::MAX, u32::MAX, u32::MAX).is_none());
        let created = super::Gpu2dResource::new(7, 0x8000_0000, 320, 200, 400)
            .unwrap()
            .with_state(super::Gpu2dResourceState::Created);
        assert_eq!(created.state(), super::Gpu2dResourceState::Created);
    }

    #[test]
    fn teardown_state_machine_requires_every_successful_stage() {
        use super::{
            Gpu2dResourceState, advance_teardown_state, commit_teardown_state, next_teardown_action,
        };

        for state in [
            Gpu2dResourceState::Unbound,
            Gpu2dResourceState::Created,
            Gpu2dResourceState::BackingAttached,
            Gpu2dResourceState::ContentTransferred,
            Gpu2dResourceState::ScanoutBound,
        ] {
            assert_eq!(
                next_teardown_action(state),
                Err(super::GpuControlQueryError::InvalidPresentationParameters)
            );
        }

        let mut resource = super::Gpu2dResource::new(7, 0x8000_0000, 320, 200, 400).unwrap();
        let expected = [
            (
                Gpu2dResourceState::Flushed,
                Gpu2dResourceState::ScanoutDetached,
            ),
            (
                Gpu2dResourceState::ScanoutDetached,
                Gpu2dResourceState::BackingDetached,
            ),
            (
                Gpu2dResourceState::BackingDetached,
                Gpu2dResourceState::Unreffed,
            ),
        ];
        for (from, to) in expected {
            resource.state = from;
            let action = next_teardown_action(resource.state).unwrap();
            resource.state = advance_teardown_state(resource.state, action);
            assert_eq!(resource.state(), to);
        }
        assert_eq!(
            next_teardown_action(resource.state()),
            Ok(super::Gpu2dTeardownAction::Complete)
        );
        assert_eq!(
            advance_teardown_state(
                Gpu2dResourceState::Unreffed,
                super::Gpu2dTeardownAction::UnrefResource
            ),
            Gpu2dResourceState::Unreffed
        );

        for (from, action, to) in [
            (
                Gpu2dResourceState::Flushed,
                super::Gpu2dTeardownAction::DisableScanout,
                Gpu2dResourceState::ScanoutDetached,
            ),
            (
                Gpu2dResourceState::ScanoutDetached,
                super::Gpu2dTeardownAction::DetachBacking,
                Gpu2dResourceState::BackingDetached,
            ),
            (
                Gpu2dResourceState::BackingDetached,
                super::Gpu2dTeardownAction::UnrefResource,
                Gpu2dResourceState::Unreffed,
            ),
        ] {
            let mut injected = super::Gpu2dResource::new(7, 0x8000_0000, 320, 200, 400).unwrap();
            injected.state = from;
            assert_eq!(
                commit_teardown_state(
                    from,
                    action,
                    Err(super::GpuControlQueryError::CompletionTimeout),
                ),
                Err(super::GpuControlQueryError::CompletionTimeout)
            );
            assert_eq!(injected.state(), from);
            assert!(!injected.device_resource_released());
            assert_eq!(commit_teardown_state(from, action, Ok(())), Ok(to));
        }
    }

    #[test]
    fn owned_display_buffer_bridges_to_resource_descriptor() {
        use crate::drivers::virtio::gpu::GpuDisplayBuffer;
        use crate::memory::{
            EarlyPhysicalPageAllocator, MemoryRegion, MemoryRegionKind, MemoryRegionSource,
            PAGE_SIZE, RegionTable, UefiMemoryType,
        };

        let mut table = RegionTable::new();
        table
            .push(MemoryRegion {
                start: 0x1000,
                byte_len: 1_024 * PAGE_SIZE,
                kind: MemoryRegionKind::Usable,
                source: MemoryRegionSource::Uefi(UefiMemoryType::Conventional),
                attributes: 0,
            })
            .unwrap();
        let mut allocator = EarlyPhysicalPageAllocator::from_memory_regions(&table).unwrap();
        let buffer = GpuDisplayBuffer::allocate(&mut allocator, 320, 200, 400).unwrap();
        let resource = super::Gpu2dResource::from_display_buffer(7, &buffer).unwrap();
        assert_eq!(resource.backing_address, 0x1000);
        assert_eq!(resource.width, 320);
        assert_eq!(resource.height, 200);
        assert_eq!(resource.stride, 400);
        assert_eq!(
            resource.backing_length,
            u32::try_from(79 * PAGE_SIZE).unwrap()
        );
        assert_eq!(resource.state(), super::Gpu2dResourceState::Unbound);
    }

    #[test]
    fn smoke_storage_keeps_event_index_rings_disjoint() {
        let storage = super::GpuControlSmokeStorage::new();
        let descriptor_start = storage.descriptors.as_ptr() as usize;
        let available_start = storage.available.as_ptr() as usize;
        let used_start = storage.used.as_ptr() as usize;
        let request_start = storage.request.as_ptr() as usize;
        let response_start = storage.response.as_ptr() as usize;

        assert_eq!(storage.descriptors.len(), 128);
        assert_eq!(storage.available.len(), 22);
        assert_eq!(storage.used.len(), 70);
        assert_eq!(available_start, descriptor_start + 128);
        assert_eq!(used_start, available_start + 24);
        assert_eq!(request_start, used_start + 72);
        assert_eq!(response_start, request_start + 128);
        assert!(used_start.is_multiple_of(4));

        let event_layout = storage
            .queue_layout_at(0x1000, 0x2000, 0x3000, true)
            .unwrap();
        let legacy_layout = storage
            .queue_layout_at(0x4000, 0x5000, 0x6000, false)
            .unwrap();
        assert!(event_layout.event_index);
        assert!(!legacy_layout.event_index);
    }

    #[test]
    fn completion_mode_requires_a_bound_vector_and_a_real_signal() {
        use super::{CompletionMode, CompletionSignal, completion_mode};

        // No bound vector stays polled regardless of any reported signal.
        assert_eq!(
            completion_mode(CompletionSignal::Interrupted, false),
            CompletionMode::Polled
        );
        assert_eq!(
            completion_mode(CompletionSignal::Unsupported, false),
            CompletionMode::Polled
        );
        // A bound vector only becomes interrupt-driven on a real signal.
        assert_eq!(
            completion_mode(CompletionSignal::Interrupted, true),
            CompletionMode::InterruptSignalled
        );
        assert_eq!(
            completion_mode(CompletionSignal::Poll, true),
            CompletionMode::Polled
        );
        assert_eq!(
            completion_mode(CompletionSignal::Unsupported, true),
            CompletionMode::Polled
        );
    }

    #[test]
    fn interrupt_observation_never_manufactures_a_completion() {
        // The completion policy is intentionally separate from the used-ring
        // authority: an interrupt can only switch the wait mode. A device that
        // signals with no descriptor in the used ring must still time out
        // rather than report a fabricated response.
        let mut queue = super::GpuControlQueue::new(
            crate::drivers::virtio::split_queue::SplitVirtqueueLayout::new(
                0, 8, 0x10_0000, 0x20_0000, 0x30_0000,
            )
            .unwrap(),
        );
        let mut descriptors = [0u8; 128];
        let mut available = [0u8; 22];
        let used = [0u8; 70];
        let storage = crate::drivers::virtio::split_queue::SplitVirtqueueStorage::new(
            queue.layout(),
            &mut descriptors,
            &mut available,
            &used,
        )
        .unwrap();
        // No used-ring entries exist, so reclamation yields nothing.
        assert!(queue.reclaim_next(&storage).unwrap().is_none());
        assert_eq!(queue.in_flight_descriptor_count(), 0);
    }
}
