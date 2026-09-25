#![allow(clippy::too_many_lines)]

//! Bounded split-virtqueue state and transport contract.
//!
//! The queue owns descriptor availability and publication state without
//! dereferencing guest memory. An architecture-specific transport later
//! writes the returned ring addresses to VirtIO-MMIO/PCIe registers. Keeping
//! that boundary explicit prevents packet builders from claiming that a
//! request reached hardware.

use super::{DESC_F_NEXT, DESC_F_WRITE, MAX_QUEUE_SIZE, VirtqDesc};
use core::sync::atomic::{Ordering, fence};

/// Minimum split-virtqueue size accepted by this bounded implementation.
pub const MIN_QUEUE_SIZE: u16 = 2;
/// Size of one split descriptor-table entry in bytes.
pub const QUEUE_DESC_SIZE: u64 = 16;
/// Alignment required for the descriptor table.
pub const QUEUE_DESC_ALIGNMENT: u64 = 16;
/// Alignment required for the available ring.
pub const QUEUE_AVAIL_ALIGNMENT: u64 = 2;
/// Alignment required for the used ring.
pub const QUEUE_USED_ALIGNMENT: u64 = 4;

/// Layout and ownership information for one split virtqueue.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SplitVirtqueueLayout {
    /// `VirtIO` queue index used by the transport notification register.
    pub queue_index: u16,
    /// Number of descriptors in the queue; a power of two.
    pub queue_size: u16,
    /// Whether the `VIRTIO_F_EVENT_IDX` feature adds event fields to the rings.
    pub event_index: bool,
    /// Guest physical address of the descriptor table.
    pub descriptor_address: u64,
    /// Guest physical address of the available ring.
    pub available_address: u64,
    /// Guest physical address of the used ring.
    pub used_address: u64,
}

impl SplitVirtqueueLayout {
    /// Validate queue size, ring alignment, address ranges, and overlap.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError`] for an invalid size, alignment, overflow, or
    /// overlapping ring region.
    pub fn new(
        queue_index: u16,
        queue_size: u16,
        descriptor_address: u64,
        available_address: u64,
        used_address: u64,
    ) -> Result<Self, QueueError> {
        Self::new_with_event_index(
            queue_index,
            queue_size,
            false,
            descriptor_address,
            available_address,
            used_address,
        )
    }

    /// Validate a split-virtqueue layout with an explicit event-index mode.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError`] for an invalid size, alignment, overflow, or
    /// overlapping ring region.
    pub fn new_with_event_index(
        queue_index: u16,
        queue_size: u16,
        event_index: bool,
        descriptor_address: u64,
        available_address: u64,
        used_address: u64,
    ) -> Result<Self, QueueError> {
        let max_queue_size = u16::try_from(MAX_QUEUE_SIZE).unwrap_or(u16::MAX);
        if !(MIN_QUEUE_SIZE..=max_queue_size).contains(&queue_size) || !queue_size.is_power_of_two()
        {
            return Err(QueueError::InvalidQueueSize(queue_size));
        }
        if descriptor_address == 0
            || available_address == 0
            || used_address == 0
            || !descriptor_address.is_multiple_of(QUEUE_DESC_ALIGNMENT)
            || !available_address.is_multiple_of(QUEUE_AVAIL_ALIGNMENT)
            || !used_address.is_multiple_of(QUEUE_USED_ALIGNMENT)
        {
            return Err(QueueError::InvalidRingAddress);
        }

        let descriptor_bytes = u64::from(queue_size) * QUEUE_DESC_SIZE;
        let available_bytes = 4 + u64::from(queue_size) * 2 + if event_index { 2 } else { 0 };
        let used_bytes = 4 + u64::from(queue_size) * 8 + if event_index { 2 } else { 0 };
        let descriptor_end = descriptor_address
            .checked_add(descriptor_bytes)
            .ok_or(QueueError::AddressOverflow)?;
        let available_end = available_address
            .checked_add(available_bytes)
            .ok_or(QueueError::AddressOverflow)?;
        let used_end = used_address
            .checked_add(used_bytes)
            .ok_or(QueueError::AddressOverflow)?;

        let overlaps =
            |start_a: u64, end_a: u64, start_b: u64, end_b: u64| start_a < end_b && start_b < end_a;
        if overlaps(
            descriptor_address,
            descriptor_end,
            available_address,
            available_end,
        ) || overlaps(descriptor_address, descriptor_end, used_address, used_end)
            || overlaps(available_address, available_end, used_address, used_end)
        {
            return Err(QueueError::OverlappingRings);
        }

        Ok(Self {
            queue_index,
            queue_size,
            event_index,
            descriptor_address,
            available_address,
            used_address,
        })
    }

    /// Number of bytes occupied by the descriptor table.
    #[must_use]
    pub const fn descriptor_bytes(&self) -> u64 {
        self.queue_size as u64 * QUEUE_DESC_SIZE
    }

    /// Number of bytes occupied by the available ring.
    #[must_use]
    pub const fn available_bytes(&self) -> u64 {
        4 + self.queue_size as u64 * 2 + if self.event_index { 2 } else { 0 }
    }

    /// Number of bytes occupied by the used ring.
    #[must_use]
    pub const fn used_bytes(&self) -> u64 {
        4 + self.queue_size as u64 * 8 + if self.event_index { 2 } else { 0 }
    }
}

/// A single buffer exposed to a device through a descriptor.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueueBuffer {
    /// Guest physical address of the buffer.
    pub address: u64,
    /// Buffer length in bytes.
    pub length: u32,
    /// Whether the device may write into the buffer.
    pub device_writable: bool,
}

impl QueueBuffer {
    /// Create a queue buffer descriptor input.
    #[must_use]
    pub const fn new(address: u64, length: u32, device_writable: bool) -> Self {
        Self {
            address,
            length,
            device_writable,
        }
    }
}

/// A used-ring completion entry.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtqUsedElem {
    /// Descriptor ID completed by the device.
    pub id: u32,
    /// Number of bytes written into the descriptor's buffer.
    pub length: u32,
}

/// A queue publication notification ready for a transport backend.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueuePublication {
    /// Queue index to write to the transport's notification register.
    pub queue_index: u16,
    /// Next available-ring index the device should observe.
    pub next_available: u16,
}

/// A successfully queued descriptor chain.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueuedChain {
    /// Head descriptor index.
    pub head: u16,
    /// Number of descriptors in the chain.
    pub descriptor_count: u16,
}

/// A descriptor chain reclaimed from the used ring.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReclaimedChain {
    /// Head descriptor index.
    pub head: u16,
    /// Device-reported written length for the head buffer.
    pub length: u32,
    /// Number of descriptors reclaimed from the chain.
    pub descriptor_count: u16,
}

/// Errors produced while validating or managing a split queue.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum QueueError {
    /// Queue size is outside the supported power-of-two range.
    InvalidQueueSize(u16),
    /// Ring address is zero or incorrectly aligned.
    InvalidRingAddress,
    /// Ring address arithmetic overflowed.
    AddressOverflow,
    /// Two ring regions overlap.
    OverlappingRings,
    /// A queue operation supplied no buffers.
    EmptyChain,
    /// A queue operation supplied a zero-length buffer.
    ZeroLengthBuffer,
    /// A queue operation supplied a null buffer address.
    InvalidBufferAddress,
    /// No free descriptor is available.
    QueueExhausted,
    /// A used-ring index arrived out of order.
    UsedIndexOutOfOrder {
        /// Expected next used-ring index.
        expected: u16,
        /// Supplied used-ring index.
        received: u16,
    },
    /// A used entry names an unknown or already reclaimed descriptor.
    InvalidUsedDescriptor(u32),
    /// A descriptor chain could not be validated.
    InvalidChain(super::DescError),
    /// Caller-provided ring storage is smaller than the layout requires.
    StorageTooSmall,
    /// A used-ring index is invalid or not yet published.
    InvalidUsedIndex(u16),
}

/// Errors returned by an architecture-specific transport notification.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransportError {
    /// No transport is currently available.
    Unavailable,
    /// The device or transport rejected the notification.
    Rejected,
    /// The transport does not implement the requested queue.
    InvalidQueue(u16),
}

/// Architecture-specific notification boundary for a split virtqueue.
pub trait VirtqueueTransport {
    /// Notify the device that new available descriptors are published.
    ///
    /// # Errors
    ///
    /// Returns [`TransportError`] when the backend is unavailable or rejects
    /// the notification.
    fn notify(&mut self, publication: QueuePublication) -> Result<(), TransportError>;
}

/// Bounded split-virtqueue state with explicit descriptor ownership.
#[derive(Clone, Debug)]
pub struct SplitVirtqueue {
    layout: SplitVirtqueueLayout,
    descriptors: [VirtqDesc; MAX_QUEUE_SIZE],
    available: [u16; MAX_QUEUE_SIZE],
    used: [VirtqUsedElem; MAX_QUEUE_SIZE],
    in_flight: [bool; MAX_QUEUE_SIZE],
    available_index: u16,
    publication_pending: bool,
    used_index: u16,
}

/// Guest-owned byte storage for one split virtqueue's three ring regions.
///
/// The queue model never interprets raw bytes itself. This adapter copies
/// descriptor and available-ring state using the `VirtIO` little-endian wire
/// format, and reads the device-owned used ring. The caller must provide
/// identity-mapped, writable guest memory and the platform memory barrier
/// required by its architecture.
pub struct SplitVirtqueueStorage<'a> {
    descriptors: &'a mut [u8],
    available: &'a mut [u8],
    used: &'a [u8],
}

impl<'a> SplitVirtqueueStorage<'a> {
    /// Create storage for a queue layout after checking minimum capacities.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError::StorageTooSmall`] when any region is shorter
    /// than the configured queue requires.
    pub fn new(
        layout: &SplitVirtqueueLayout,
        descriptors: &'a mut [u8],
        available: &'a mut [u8],
        used: &'a [u8],
    ) -> Result<Self, QueueError> {
        if descriptors.len()
            < usize::try_from(layout.descriptor_bytes()).map_err(|_| QueueError::StorageTooSmall)?
            || available.len()
                < usize::try_from(layout.available_bytes())
                    .map_err(|_| QueueError::StorageTooSmall)?
            || used.len()
                < usize::try_from(layout.used_bytes()).map_err(|_| QueueError::StorageTooSmall)?
        {
            return Err(QueueError::StorageTooSmall);
        }
        Ok(Self {
            descriptors,
            available,
            used,
        })
    }

    /// Write driver-owned descriptor and available-ring state.
    ///
    /// The available index is written last. A release fence is issued before
    /// the caller invokes the transport notification.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError::StorageTooSmall`] if storage is too small.
    pub fn publish_driver_state(&mut self, queue: &SplitVirtqueue) -> Result<(), QueueError> {
        let layout = queue.layout();
        let descriptor_bytes =
            usize::try_from(layout.descriptor_bytes()).map_err(|_| QueueError::StorageTooSmall)?;
        let available_bytes =
            usize::try_from(layout.available_bytes()).map_err(|_| QueueError::StorageTooSmall)?;
        if self.descriptors.len() < descriptor_bytes || self.available.len() < available_bytes {
            return Err(QueueError::StorageTooSmall);
        }

        let mut index = 0;
        while index < layout.queue_size as usize {
            let descriptor = queue.descriptors[index];
            let offset = index * usize::try_from(QUEUE_DESC_SIZE).unwrap_or(usize::MAX);
            self.descriptors[offset..offset + 8].copy_from_slice(&descriptor.addr.to_le_bytes());
            self.descriptors[offset + 8..offset + 12]
                .copy_from_slice(&descriptor.len.to_le_bytes());
            self.descriptors[offset + 12..offset + 14]
                .copy_from_slice(&descriptor.flags.to_le_bytes());
            self.descriptors[offset + 14..offset + 16]
                .copy_from_slice(&descriptor.next.to_le_bytes());
            index += 1;
        }

        let published = queue.available_index();
        self.available[0..2].copy_from_slice(&0u16.to_le_bytes());
        let count = usize::from(published).min(layout.queue_size as usize);
        let first =
            published.wrapping_sub(u16::try_from(count).map_err(|_| QueueError::StorageTooSmall)?);
        for entry in 0..count {
            let ring_slot = first
                .wrapping_add(u16::try_from(entry).map_err(|_| QueueError::StorageTooSmall)?)
                as usize
                % layout.queue_size as usize;
            let offset = 4 + ring_slot * 2;
            self.available[offset..offset + 2]
                .copy_from_slice(&queue.available[ring_slot].to_le_bytes());
        }
        self.available[2..4].copy_from_slice(&published.to_le_bytes());
        if layout.event_index {
            let used_event_offset = 4 + layout.queue_size as usize * 2;
            self.available
                .get_mut(used_event_offset..used_event_offset + 2)
                .ok_or(QueueError::StorageTooSmall)?
                .copy_from_slice(&0u16.to_le_bytes());
        }
        fence(Ordering::Release);
        Ok(())
    }

    /// Read the device-published used-ring index.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError::InvalidUsedIndex`] if the ring is too small or
    /// the device has not published a used index.
    pub fn used_index(&self, queue: &SplitVirtqueue) -> Result<u16, QueueError> {
        fence(Ordering::Acquire);
        let required = usize::try_from(queue.layout().used_bytes())
            .map_err(|_| QueueError::StorageTooSmall)?;
        if self.used.len() < required {
            return Err(QueueError::StorageTooSmall);
        }
        let index = u16::from_le_bytes([self.used[2], self.used[3]]);
        if index == 0 {
            return Err(QueueError::InvalidUsedIndex(index));
        }
        Ok(index)
    }

    /// Read one used-ring element after an acquire barrier.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError::InvalidUsedIndex`] when the element is not yet
    /// within the published used-ring range.
    pub fn used_element(
        &self,
        queue: &SplitVirtqueue,
        index: u16,
    ) -> Result<VirtqUsedElem, QueueError> {
        fence(Ordering::Acquire);
        let required = usize::try_from(queue.layout().used_bytes())
            .map_err(|_| QueueError::StorageTooSmall)?;
        if self.used.len() < required {
            return Err(QueueError::StorageTooSmall);
        }
        let published = u16::from_le_bytes([self.used[2], self.used[3]]);
        if index == 0 || index > published {
            return Err(QueueError::InvalidUsedIndex(index));
        }
        let slot = usize::from(index.wrapping_sub(1)) % queue.layout().queue_size as usize;
        let offset = 4 + slot * 8;
        Ok(VirtqUsedElem {
            id: u32::from_le_bytes([
                self.used[offset],
                self.used[offset + 1],
                self.used[offset + 2],
                self.used[offset + 3],
            ]),
            length: u32::from_le_bytes([
                self.used[offset + 4],
                self.used[offset + 5],
                self.used[offset + 6],
                self.used[offset + 7],
            ]),
        })
    }
}

impl SplitVirtqueue {
    /// Create an empty queue with the supplied validated layout.
    #[must_use]
    pub const fn new(layout: SplitVirtqueueLayout) -> Self {
        Self {
            layout,
            descriptors: [VirtqDesc {
                addr: 0,
                len: 0,
                flags: 0,
                next: 0,
            }; MAX_QUEUE_SIZE],
            available: [0; MAX_QUEUE_SIZE],
            used: [VirtqUsedElem { id: 0, length: 0 }; MAX_QUEUE_SIZE],
            in_flight: [false; MAX_QUEUE_SIZE],
            available_index: 0,
            publication_pending: false,
            used_index: 0,
        }
    }

    /// Return the validated queue layout.
    #[must_use]
    pub const fn layout(&self) -> &SplitVirtqueueLayout {
        &self.layout
    }

    /// Return the current available-ring index.
    #[must_use]
    pub const fn available_index(&self) -> u16 {
        self.available_index
    }

    /// Return the last used-ring index consumed by the driver.
    #[must_use]
    pub const fn used_index(&self) -> u16 {
        self.used_index
    }

    /// Return whether descriptors have been published but not notified.
    #[must_use]
    pub const fn has_unnotified_descriptors(&self) -> bool {
        self.publication_pending
    }

    /// Return the number of in-flight descriptor chains.
    #[must_use]
    pub fn in_flight_count(&self) -> usize {
        self.in_flight.iter().filter(|&&entry| entry).count()
    }

    /// Add one driver-readable buffer and publish its descriptor index.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError`] for an invalid buffer or an exhausted queue.
    pub fn enqueue(&mut self, buffer: QueueBuffer) -> Result<QueuedChain, QueueError> {
        self.enqueue_chain(core::slice::from_ref(&buffer))
    }

    /// Add a descriptor chain and make it visible to the available ring.
    ///
    /// The operation is transactional: if any descriptor or buffer is
    /// invalid, no descriptor state is changed.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError`] for an invalid chain or an exhausted queue.
    pub fn enqueue_chain(&mut self, buffers: &[QueueBuffer]) -> Result<QueuedChain, QueueError> {
        if buffers.is_empty() {
            return Err(QueueError::EmptyChain);
        }
        if buffers.len() > MAX_QUEUE_SIZE {
            return Err(QueueError::QueueExhausted);
        }
        for buffer in buffers {
            if buffer.length == 0 {
                return Err(QueueError::ZeroLengthBuffer);
            }
            if buffer.address == 0 {
                return Err(QueueError::InvalidBufferAddress);
            }
        }
        let mut selected = [0u16; MAX_QUEUE_SIZE];
        let mut selected_len = 0;
        while selected_len < buffers.len() {
            let mut found = None;
            let mut index = 0;
            while index < self.layout.queue_size as usize {
                let already_selected = selected[..selected_len]
                    .iter()
                    .any(|entry| usize::from(*entry) == index);
                if !self.in_flight[index] && !already_selected {
                    found = Some(index);
                    break;
                }
                index += 1;
            }
            let Some(index) = found else {
                return Err(QueueError::QueueExhausted);
            };
            let Ok(index) = u16::try_from(index) else {
                return Err(QueueError::QueueExhausted);
            };
            selected[selected_len] = index;
            selected_len += 1;
        }

        let mut descriptor_index = 0;
        while descriptor_index < selected_len {
            let buffer = buffers[descriptor_index];
            let flags = if descriptor_index + 1 < selected_len {
                DESC_F_NEXT
            } else {
                0
            } | if buffer.device_writable {
                DESC_F_WRITE
            } else {
                0
            };
            let next = if descriptor_index + 1 < selected_len {
                selected[descriptor_index + 1]
            } else {
                0
            };
            let index = selected[descriptor_index] as usize;
            self.descriptors[index] = VirtqDesc {
                addr: buffer.address,
                len: buffer.length,
                flags,
                next,
            };
            descriptor_index += 1;
        }

        descriptor_index = 0;
        while descriptor_index < selected_len {
            let index = selected[descriptor_index] as usize;
            self.in_flight[index] = true;
            descriptor_index += 1;
        }
        let head = selected[0];
        let available_slot = self.available_index as usize % self.layout.queue_size as usize;
        self.available[available_slot] = head;
        self.available_index = self.available_index.wrapping_add(1);
        self.publication_pending = true;
        let Ok(descriptor_count) = u16::try_from(selected_len) else {
            return Err(QueueError::QueueExhausted);
        };
        Ok(QueuedChain {
            head,
            descriptor_count,
        })
    }

    /// Publish all newly available descriptors through a transport.
    ///
    /// A failed notification leaves the publication pending so a later call
    /// can retry without duplicating descriptors.
    ///
    /// # Errors
    ///
    /// Returns [`TransportError`] without consuming the pending publication.
    pub fn publish<T: VirtqueueTransport>(
        &mut self,
        transport: &mut T,
    ) -> Result<Option<QueuePublication>, TransportError> {
        if !self.has_unnotified_descriptors() {
            return Ok(None);
        }
        let publication = QueuePublication {
            queue_index: self.layout.queue_index,
            next_available: self.available_index,
        };
        transport.notify(publication)?;
        self.publication_pending = false;
        Ok(Some(publication))
    }

    /// Record and reclaim one completion from the used ring.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError`] for an out-of-order or invalid completion and
    /// leaves ownership unchanged.
    pub fn reclaim(
        &mut self,
        used_index: u16,
        element: VirtqUsedElem,
    ) -> Result<ReclaimedChain, QueueError> {
        let expected = self.used_index.wrapping_add(1);
        if used_index != expected {
            return Err(QueueError::UsedIndexOutOfOrder {
                expected,
                received: used_index,
            });
        }
        let descriptor_index = usize::try_from(element.id).unwrap_or(MAX_QUEUE_SIZE);
        if descriptor_index >= self.layout.queue_size as usize || !self.in_flight[descriptor_index]
        {
            return Err(QueueError::InvalidUsedDescriptor(element.id));
        }

        let mut chain_indices = [0u16; MAX_QUEUE_SIZE];
        let mut chain_len = 0;
        let mut current = u16::try_from(descriptor_index)
            .map_err(|_| QueueError::InvalidUsedDescriptor(element.id))?;
        loop {
            let index = usize::from(current);
            if index >= self.layout.queue_size as usize
                || !self.in_flight[index]
                || chain_indices[..chain_len]
                    .iter()
                    .any(|entry| usize::from(*entry) == index)
            {
                return Err(QueueError::InvalidUsedDescriptor(element.id));
            }
            chain_indices[chain_len] = current;
            chain_len += 1;
            let descriptor = self.descriptors[index];
            if descriptor.flags & DESC_F_NEXT == 0 {
                break;
            }
            current = descriptor.next;
            if chain_len >= self.layout.queue_size as usize {
                return Err(QueueError::InvalidUsedDescriptor(element.id));
            }
        }

        let mut chain_index = 0;
        while chain_index < chain_len {
            let index = usize::from(chain_indices[chain_index]);
            self.in_flight[index] = false;
            self.descriptors[index] = VirtqDesc {
                addr: 0,
                len: 0,
                flags: 0,
                next: 0,
            };
            chain_index += 1;
        }
        self.used[used_index as usize % self.layout.queue_size as usize] = element;
        self.used_index = used_index;
        let Ok(head) = u16::try_from(element.id) else {
            return Err(QueueError::InvalidUsedDescriptor(element.id));
        };
        let Ok(descriptor_count) = u16::try_from(chain_len) else {
            return Err(QueueError::InvalidUsedDescriptor(element.id));
        };
        Ok(ReclaimedChain {
            head,
            length: element.length,
            descriptor_count,
        })
    }

    /// Validate a descriptor chain currently stored in the queue.
    ///
    /// # Errors
    ///
    /// Returns [`QueueError::InvalidChain`] when the chain is malformed or
    /// does not terminate within the configured queue size.
    pub fn validate_chain(&self, head: u16) -> Result<usize, QueueError> {
        let count = self.layout.queue_size as usize;
        super::validate_chain(&self.descriptors[..count], head, count)
            .map_err(QueueError::InvalidChain)
    }

    /// Reset ownership and ring indices while retaining the layout.
    pub fn reset(&mut self) {
        self.descriptors.fill(VirtqDesc {
            addr: 0,
            len: 0,
            flags: 0,
            next: 0,
        });
        self.available.fill(0);
        self.used.fill(VirtqUsedElem { id: 0, length: 0 });
        self.in_flight.fill(false);
        self.available_index = 0;
        self.publication_pending = false;
        self.used_index = 0;
    }
}

#[cfg(test)]
mod tests {
    use std::vec;

    use super::{
        DESC_F_NEXT, DESC_F_WRITE, QueueBuffer, QueueError, QueuePublication, SplitVirtqueue,
        SplitVirtqueueLayout, SplitVirtqueueStorage, TransportError, VirtqUsedElem,
        VirtqueueTransport,
    };

    #[derive(Default)]
    struct RecordingTransport {
        publications: [Option<QueuePublication>; 8],
        count: usize,
        fail_next: bool,
    }

    impl VirtqueueTransport for RecordingTransport {
        fn notify(&mut self, publication: QueuePublication) -> Result<(), TransportError> {
            if self.fail_next {
                self.fail_next = false;
                return Err(TransportError::Rejected);
            }
            self.publications[self.count] = Some(publication);
            self.count += 1;
            Ok(())
        }
    }

    fn layout() -> SplitVirtqueueLayout {
        SplitVirtqueueLayout::new(0, 8, 0x1000, 0x2000, 0x3000).unwrap()
    }

    #[test]
    fn event_index_ring_sizes_match_virtio_wire_format() {
        let event_layout =
            SplitVirtqueueLayout::new_with_event_index(0, 8, true, 0x1000, 0x2000, 0x3000).unwrap();
        assert_eq!(event_layout.available_bytes(), 22);
        assert_eq!(event_layout.used_bytes(), 70);
        let legacy = layout();
        assert_eq!(legacy.available_bytes(), 20);
        assert_eq!(legacy.used_bytes(), 68);
    }

    #[test]
    fn layout_rejects_bad_alignment_and_overlap() {
        assert!(matches!(
            SplitVirtqueueLayout::new(0, 7, 0x1000, 0x2000, 0x3000),
            Err(QueueError::InvalidQueueSize(7))
        ));
        assert_eq!(
            SplitVirtqueueLayout::new(0, 8, 0x1001, 0x2000, 0x3000),
            Err(QueueError::InvalidRingAddress)
        );
        assert_eq!(
            SplitVirtqueueLayout::new(0, 8, 0x1000, 0x1000 + 0x7e, 0x3000),
            Err(QueueError::OverlappingRings)
        );
    }

    #[test]
    fn enqueue_publish_retry_and_reclaim() {
        let mut queue = SplitVirtqueue::new(layout());
        let mut transport = RecordingTransport::default();
        let chain = queue
            .enqueue_chain(&[
                QueueBuffer::new(0x4000, 64, false),
                QueueBuffer::new(0x5000, 8, true),
            ])
            .unwrap();
        assert_eq!(chain.descriptor_count, 2);
        assert!(queue.has_unnotified_descriptors());
        assert_eq!(
            queue.publish(&mut transport).unwrap(),
            Some(QueuePublication {
                queue_index: 0,
                next_available: 1,
            })
        );
        assert!(!queue.has_unnotified_descriptors());
        assert_eq!(queue.publish(&mut transport).unwrap(), None);

        let reclaimed = queue
            .reclaim(
                1,
                VirtqUsedElem {
                    id: chain.head.into(),
                    length: 64,
                },
            )
            .unwrap();
        assert_eq!(reclaimed.head, chain.head);
        assert_eq!(reclaimed.length, 64);
        assert_eq!(reclaimed.descriptor_count, 2);
        assert_eq!(queue.in_flight_count(), 0);
    }

    #[test]
    fn invalid_chain_is_transactional_and_wraps_without_loss() {
        let mut queue =
            SplitVirtqueue::new(SplitVirtqueueLayout::new(0, 2, 0x1000, 0x2000, 0x3000).unwrap());
        assert_eq!(
            queue.enqueue_chain(&[
                QueueBuffer::new(0x4000, 1, false),
                QueueBuffer::new(0, 1, false),
            ]),
            Err(QueueError::InvalidBufferAddress)
        );
        assert_eq!(queue.in_flight_count(), 0);
        assert_eq!(queue.available_index(), 0);

        let mut transport = RecordingTransport::default();
        for address in [0x4000, 0x5000] {
            queue.enqueue(QueueBuffer::new(address, 1, false)).unwrap();
        }
        assert_eq!(queue.available_index(), 2);
        queue.publish(&mut transport).unwrap();
        queue
            .reclaim(1, VirtqUsedElem { id: 0, length: 1 })
            .unwrap();
        queue
            .reclaim(2, VirtqUsedElem { id: 1, length: 1 })
            .unwrap();
        assert_eq!(queue.in_flight_count(), 0);
        queue.enqueue(QueueBuffer::new(0x6000, 1, false)).unwrap();
        assert!(queue.has_unnotified_descriptors());
        assert_eq!(
            queue
                .publish(&mut transport)
                .unwrap()
                .unwrap()
                .next_available,
            3
        );
    }

    #[test]
    fn failed_notification_remains_retryable() {
        let mut queue = SplitVirtqueue::new(layout());
        let mut transport = RecordingTransport {
            fail_next: true,
            ..RecordingTransport::default()
        };
        queue.enqueue(QueueBuffer::new(0x4000, 64, false)).unwrap();
        assert_eq!(queue.publish(&mut transport), Err(TransportError::Rejected));
        assert!(queue.has_unnotified_descriptors());
        assert!(queue.publish(&mut transport).unwrap().is_some());
    }

    #[test]
    fn exhaustion_and_out_of_order_completion_are_rejected() {
        let mut queue =
            SplitVirtqueue::new(SplitVirtqueueLayout::new(0, 2, 0x1000, 0x2000, 0x3000).unwrap());
        queue.enqueue(QueueBuffer::new(0x4000, 1, false)).unwrap();
        queue.enqueue(QueueBuffer::new(0x5000, 1, false)).unwrap();
        assert_eq!(
            queue.enqueue(QueueBuffer::new(0x6000, 1, false)),
            Err(QueueError::QueueExhausted)
        );
        assert_eq!(
            queue.reclaim(2, VirtqUsedElem { id: 0, length: 1 }),
            Err(QueueError::UsedIndexOutOfOrder {
                expected: 1,
                received: 2,
            })
        );
        assert_eq!(
            queue.reclaim(1, VirtqUsedElem { id: 99, length: 1 }),
            Err(QueueError::InvalidUsedDescriptor(99))
        );
    }

    #[test]
    fn storage_publishes_wire_layout_and_releases_before_notification() {
        let layout = layout();
        let mut queue = SplitVirtqueue::new(layout);
        queue
            .enqueue_chain(&[
                QueueBuffer::new(0x1122_3344_5566_7788, 0x20, false),
                QueueBuffer::new(0x8877_6655_4433_2211, 0x08, true),
            ])
            .unwrap();
        let mut descriptors = vec![0u8; 0x80];
        let mut available = vec![0u8; 0x20];
        let used = vec![0u8; 0x48];
        let mut storage =
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used).unwrap();
        storage.publish_driver_state(&queue).unwrap();

        assert_eq!(&descriptors[0..8], &0x1122_3344_5566_7788u64.to_le_bytes());
        assert_eq!(&descriptors[8..12], &0x20u32.to_le_bytes());
        assert_eq!(&descriptors[12..14], &DESC_F_NEXT.to_le_bytes());
        assert_eq!(&descriptors[14..16], &1u16.to_le_bytes());
        assert_eq!(
            &descriptors[16..24],
            &0x8877_6655_4433_2211u64.to_le_bytes()
        );
        assert_eq!(&descriptors[24..28], &8u32.to_le_bytes());
        assert_eq!(&descriptors[28..30], &DESC_F_WRITE.to_le_bytes());
        assert_eq!(&available[2..4], &1u16.to_le_bytes());
        assert_eq!(&available[4..6], &0u16.to_le_bytes());
    }

    #[test]
    fn storage_republishes_latest_heads_after_available_wrap() {
        let layout = SplitVirtqueueLayout::new(0, 2, 0x1000, 0x2000, 0x3000).unwrap();
        let mut queue = SplitVirtqueue::new(layout);
        let mut descriptors = vec![0u8; 0x20];
        let mut available = vec![0u8; 0x10];
        let used = vec![0u8; 0x20];
        let mut storage =
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used).unwrap();

        let first = queue.enqueue(QueueBuffer::new(0x4000, 1, false)).unwrap();
        queue.enqueue(QueueBuffer::new(0x5000, 1, false)).unwrap();
        storage.publish_driver_state(&queue).unwrap();
        queue
            .reclaim(
                1,
                VirtqUsedElem {
                    id: first.head.into(),
                    length: 1,
                },
            )
            .unwrap();
        queue
            .reclaim(2, VirtqUsedElem { id: 1, length: 1 })
            .unwrap();
        let third = queue.enqueue(QueueBuffer::new(0x6000, 1, false)).unwrap();
        storage.publish_driver_state(&queue).unwrap();

        assert_eq!(&available[2..4], &3u16.to_le_bytes());
        assert_eq!(&available[4..6], &0u16.to_le_bytes());
        assert_eq!(&available[6..8], &1u16.to_le_bytes());
        assert_eq!(third.head, 0);
    }

    #[test]
    fn storage_publishes_event_index_fields_at_ring_boundaries() {
        let layout =
            SplitVirtqueueLayout::new_with_event_index(0, 8, true, 0x1000, 0x2000, 0x3000).unwrap();
        let mut queue = SplitVirtqueue::new(layout);
        queue
            .enqueue_chain(&[
                QueueBuffer::new(0x4000, 1, false),
                QueueBuffer::new(0x5000, 1, true),
            ])
            .unwrap();
        let mut descriptors = vec![0u8; 0x80];
        let mut available = vec![0xffu8; 0x20];
        let used = vec![0xa5u8; 0x48];
        let mut storage =
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used).unwrap();
        storage.publish_driver_state(&queue).unwrap();

        assert_eq!(&available[0..2], &0u16.to_le_bytes());
        assert_eq!(&available[2..4], &1u16.to_le_bytes());
        assert_eq!(&available[4..6], &0u16.to_le_bytes());
        assert_eq!(&available[20..22], &0u16.to_le_bytes());
        assert_eq!(&used[68..70], &[0xa5, 0xa5]);
    }

    #[test]
    fn storage_reads_used_ring_with_bounds_and_acquire_semantics() {
        let layout = layout();
        let queue = SplitVirtqueue::new(layout);
        let mut descriptors = vec![0u8; 0x80];
        let mut available = vec![0u8; 0x20];
        let mut used = vec![0u8; 0x48];
        assert_eq!(
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used)
                .unwrap()
                .used_index(&queue),
            Err(QueueError::InvalidUsedIndex(0))
        );

        used[2..4].copy_from_slice(&1u16.to_le_bytes());
        used[4..8].copy_from_slice(&0x0102_0304u32.to_le_bytes());
        used[8..12].copy_from_slice(&128u32.to_le_bytes());
        let storage =
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used).unwrap();
        assert_eq!(storage.used_index(&queue), Ok(1));
        assert_eq!(
            storage.used_element(&queue, 1),
            Ok(VirtqUsedElem {
                id: 0x0102_0304,
                length: 128,
            })
        );
        assert_eq!(
            storage.used_element(&queue, 2),
            Err(QueueError::InvalidUsedIndex(2))
        );
    }

    #[test]
    fn storage_rewrites_wrapped_available_slots() {
        let layout = SplitVirtqueueLayout::new(0, 2, 0x1000, 0x2000, 0x3000).unwrap();
        let mut queue = SplitVirtqueue::new(layout);
        let mut descriptors = vec![0u8; 0x20];
        let mut available = vec![0u8; 0x10];
        let used = vec![0u8; 0x20];
        let mut storage =
            SplitVirtqueueStorage::new(&layout, &mut descriptors, &mut available, &used).unwrap();

        for _ in 0..2 {
            queue.enqueue(QueueBuffer::new(0x4000, 1, false)).unwrap();
        }
        queue
            .reclaim(1, VirtqUsedElem { id: 0, length: 1 })
            .unwrap();
        queue
            .reclaim(2, VirtqUsedElem { id: 1, length: 1 })
            .unwrap();
        queue.enqueue(QueueBuffer::new(0x5000, 1, false)).unwrap();
        queue.enqueue(QueueBuffer::new(0x6000, 1, false)).unwrap();
        storage.publish_driver_state(&queue).unwrap();
        assert_eq!(&available[2..4], &4u16.to_le_bytes());
        assert_eq!(&available[4..6], &0u16.to_le_bytes());
        assert_eq!(&available[6..8], &1u16.to_le_bytes());
    }
}
