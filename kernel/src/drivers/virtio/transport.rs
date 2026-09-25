#![allow(unsafe_code)]

//! Modern VirtIO-PCI common-register and queue-notification transport.
//!
//! Architecture adapters provide the already-mapped MMIO accessors. This
//! module owns the VirtIO-PCI wire policy, status handshake, feature
//! negotiation, split-ring address programming, and bounded queue notify.

use super::split_queue::{
    QueuePublication, SplitVirtqueueLayout, TransportError, VirtqueueTransport,
};
use super::{
    MAX_QUEUE_SIZE, VIRTIO_STATUS_ACKNOWLEDGE, VIRTIO_STATUS_DRIVER, VIRTIO_STATUS_DRIVER_OK,
    VIRTIO_STATUS_FAILED, VIRTIO_STATUS_FEATURES_OK,
};
use crate::drivers::pci::PciVirtioRegions;
use crate::drivers::virtio::pci::common;

/// Maximum number of feature words exposed by the modern `VirtIO` common
/// configuration structure.
pub const VIRTIO_FEATURE_WORDS: u32 = 2;

/// Value written to a queue's MSI-X vector register to mask its interrupt.
pub const NO_VECTOR: u16 = 0xffff;

/// Interrupt-status bits read from the `VirtIO` ISR structure.
///
/// Bit 0 means at least one virtqueue has a pending completion. Bits 1-3
/// carry the device-specific configuration interrupt number, so a device that
/// uses configuration interrupts still reports through bit 0 for queues.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueueInterruptFlags {
    /// Raw byte returned by the device.
    pub raw: u8,
}

impl QueueInterruptFlags {
    /// Decode the device byte returned by an ISR read.
    #[must_use]
    pub const fn from_device_byte(raw: u8) -> Self {
        Self { raw }
    }

    /// Return whether the device signalled a virtqueue completion.
    ///
    /// Reading the ISR structure already cleared the register, so this is a
    /// one-shot observation of the value that was pending.
    #[must_use]
    pub const fn queue_completion_pending(self) -> bool {
        self.raw & 1 != 0
    }
}

/// Fixed virtual window reserved for transient VirtIO-PCI MMIO mappings.
pub const VIRTIO_PCI_MMIO_WINDOW_BASE: u64 = 0x0000_5000_0000_0000;
/// Maximum BAR span accepted by the bounded MMIO mapping helper.
pub const VIRTIO_PCI_MAX_BAR_MAPPING_BYTES: u64 = 64 * 1024;
/// Page size used by both currently supported mapping backends.
pub const VIRTIO_PCI_MAPPING_PAGE_SIZE: u64 = 4096;

/// Errors returned while turning decoded regions into one bounded MMIO map.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VirtioPciBarMappingError {
    /// The capability regions do not select the same containing BAR.
    IncompatibleBars,
    /// Region arithmetic overflowed.
    AddressOverflow,
    /// The rounded BAR span exceeds the fixed mapping window.
    MappingTooLarge,
}

/// A page-rounded physical BAR span and the virtual-relative region offsets.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtioPciBarMapping {
    /// First physical page of the rounded BAR span.
    pub physical_start: u64,
    /// Rounded physical span in bytes.
    pub byte_len: u64,
    /// Common-region offset from [`Self::physical_start`].
    pub common_offset: u64,
    /// Notification-region offset from [`Self::physical_start`].
    pub notify_offset: u64,
    /// Device-region offset from [`Self::physical_start`].
    pub device_offset: u64,
}

/// Build one bounded physical mapping plan for modern `VirtIO` regions.
///
/// # Errors
///
/// Returns [`VirtioPciBarMappingError`] for regions from different BARs,
/// overflowing arithmetic, or a rounded span larger than
/// [`VIRTIO_PCI_MAX_BAR_MAPPING_BYTES`].
pub fn bar_mapping(
    regions: &PciVirtioRegions,
) -> Result<VirtioPciBarMapping, VirtioPciBarMappingError> {
    let bar_index = regions.common.bar_index;
    let bar_address = regions.common.bar_address;
    if regions.notify.bar_index != bar_index
        || regions.device.bar_index != bar_index
        || regions.notify.bar_address != bar_address
        || regions.device.bar_address != bar_address
    {
        return Err(VirtioPciBarMappingError::IncompatibleBars);
    }
    let common_end = regions
        .common
        .physical_base
        .checked_add(u64::from(regions.common.byte_len))
        .ok_or(VirtioPciBarMappingError::AddressOverflow)?;
    let notify_end = regions
        .notify
        .physical_base
        .checked_add(u64::from(regions.notify.byte_len))
        .ok_or(VirtioPciBarMappingError::AddressOverflow)?;
    let device_end = regions
        .device
        .physical_base
        .checked_add(u64::from(regions.device.byte_len))
        .ok_or(VirtioPciBarMappingError::AddressOverflow)?;
    let physical_start = regions
        .common
        .physical_base
        .min(regions.notify.physical_base)
        .min(regions.device.physical_base);
    let physical_end = common_end.max(notify_end).max(device_end);
    let start = physical_start & !(VIRTIO_PCI_MAPPING_PAGE_SIZE - 1);
    let end = physical_end
        .checked_add(VIRTIO_PCI_MAPPING_PAGE_SIZE - 1)
        .ok_or(VirtioPciBarMappingError::AddressOverflow)?
        & !(VIRTIO_PCI_MAPPING_PAGE_SIZE - 1);
    let byte_len = end
        .checked_sub(start)
        .ok_or(VirtioPciBarMappingError::AddressOverflow)?;
    if byte_len > VIRTIO_PCI_MAX_BAR_MAPPING_BYTES {
        return Err(VirtioPciBarMappingError::MappingTooLarge);
    }
    Ok(VirtioPciBarMapping {
        physical_start: start,
        byte_len,
        common_offset: regions
            .common
            .physical_base
            .checked_sub(start)
            .ok_or(VirtioPciBarMappingError::AddressOverflow)?,
        notify_offset: regions
            .notify
            .physical_base
            .checked_sub(start)
            .ok_or(VirtioPciBarMappingError::AddressOverflow)?,
        device_offset: regions
            .device
            .physical_base
            .checked_sub(start)
            .ok_or(VirtioPciBarMappingError::AddressOverflow)?,
    })
}

/// A decoded queue address pair for a split virtqueue.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtioPciQueueAddresses {
    /// Guest physical address of the descriptor table.
    pub descriptor: u64,
    /// Guest physical address of the available ring.
    pub available: u64,
    /// Guest physical address of the used ring.
    pub used: u64,
}

/// Modern VirtIO-PCI register access boundary.
pub trait VirtioPciMmio {
    /// Read one byte from the common configuration region.
    fn common_read_u8(&mut self, offset: u32) -> u8;
    /// Write one byte to the common configuration region.
    fn common_write_u8(&mut self, offset: u32, value: u8);
    /// Read one 16-bit little-endian value from the common region.
    fn common_read_u16(&mut self, offset: u32) -> u16;
    /// Write one 16-bit little-endian value to the common region.
    fn common_write_u16(&mut self, offset: u32, value: u16);
    /// Read one 32-bit little-endian value from the common region.
    fn common_read_u32(&mut self, offset: u32) -> u32;
    /// Write one 32-bit little-endian value to the common region.
    fn common_write_u32(&mut self, offset: u32, value: u32);
    /// Write one 16-bit little-endian value to the notification region.
    fn notify_write_u16(&mut self, offset: u32, value: u16);
    /// Read the interrupt-status structure, which the device clears on read.
    ///
    /// A read is edge-triggered state consumption, not a level: the device
    /// atomically clears the register and deasserts its interrupt line as part
    /// of servicing the access. The return value is the bit set that was
    /// pending at the moment of the read.
    fn isr_read_u8(&mut self, offset: u32) -> u8;
}

/// A volatile MMIO adapter over already-mapped VirtIO-PCI regions.
///
/// The caller must map each range as supervisor read-write, non-executable
/// device memory and keep those mappings alive for the adapter's lifetime.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MappedVirtioPciMmio {
    /// Virtual base of the common configuration region.
    pub common_base: u64,
    /// Length of the common configuration region.
    pub common_length: u32,
    /// Virtual base of the notification region.
    pub notify_base: u64,
    /// Length of the notification region.
    pub notify_length: u32,
    /// Virtual base of the interrupt-status structure, when the device
    /// exposes one.
    pub isr_base: Option<u64>,
    /// Length of the interrupt-status structure, when present.
    pub isr_length: u32,
}

impl MappedVirtioPciMmio {
    /// Construct an adapter for already-mapped regions.
    ///
    /// `isr_base` is [`None`] when the device exposes no interrupt-status
    /// structure. A caller that needs interrupt-driven completion must treat
    /// that as "not supported" rather than falling back to polling.
    #[must_use]
    pub const fn new(
        common_base: u64,
        common_length: u32,
        notify_base: u64,
        notify_length: u32,
        isr_base: Option<u64>,
        isr_length: u32,
    ) -> Self {
        Self {
            common_base,
            common_length,
            notify_base,
            notify_length,
            isr_base,
            isr_length,
        }
    }

    fn common_ptr(&self, offset: u32, width: u32) -> Option<*mut u8> {
        let end = offset.checked_add(width)?;
        if end > self.common_length {
            return None;
        }
        if !offset.is_multiple_of(width) {
            return None;
        }
        if !self.common_base.is_multiple_of(u64::from(width)) {
            return None;
        }
        let address = self.common_base.checked_add(u64::from(offset))?;
        let address = usize::try_from(address).ok()?;
        Some(address as *mut u8)
    }

    fn notify_ptr(&self, offset: u32, width: u32) -> Option<*mut u8> {
        let end = offset.checked_add(width)?;
        if end > self.notify_length {
            return None;
        }
        if !offset.is_multiple_of(width) {
            return None;
        }
        if !self.notify_base.is_multiple_of(u64::from(width)) {
            return None;
        }
        let address = self.notify_base.checked_add(u64::from(offset))?;
        let address = usize::try_from(address).ok()?;
        Some(address as *mut u8)
    }

    fn isr_ptr(&self, offset: u32, width: u32) -> Option<*mut u8> {
        let end = offset.checked_add(width)?;
        if end > self.isr_length {
            return None;
        }
        if !offset.is_multiple_of(width) {
            return None;
        }
        let base = self.isr_base?;
        if !base.is_multiple_of(u64::from(width)) {
            return None;
        }
        let address = base.checked_add(u64::from(offset))?;
        let address = usize::try_from(address).ok()?;
        Some(address as *mut u8)
    }
}

impl VirtioPciMmio for MappedVirtioPciMmio {
    fn common_read_u8(&mut self, offset: u32) -> u8 {
        self.common_ptr(offset, 1).map_or(0xff, |pointer| {
            // SAFETY: The constructor contract requires a live supervisor MMIO
            // mapping, and the offset and width are bounded above.
            unsafe { core::ptr::read_volatile(pointer) }
        })
    }

    fn common_write_u8(&mut self, offset: u32, value: u8) {
        if let Some(pointer) = self.common_ptr(offset, 1) {
            // SAFETY: See `common_read_u8`; this is bounded volatile MMIO.
            unsafe { core::ptr::write_volatile(pointer, value) };
        }
    }

    fn common_read_u16(&mut self, offset: u32) -> u16 {
        #[allow(clippy::cast_ptr_alignment)]
        self.common_ptr(offset, 2).map_or(0xffff, |pointer| {
            // SAFETY: The constructor contract and width check make this a
            // bounded, aligned volatile MMIO access.
            unsafe { core::ptr::read_volatile(pointer.cast::<u16>()) }
        })
    }

    fn common_write_u16(&mut self, offset: u32, value: u16) {
        if let Some(pointer) = self.common_ptr(offset, 2) {
            // SAFETY: See `common_read_u16`; this is bounded volatile MMIO.
            // SAFETY: The constructor contract proves the base alignment and
            // the checked offset is aligned to the access width.
            #[allow(clippy::cast_ptr_alignment)]
            unsafe {
                core::ptr::write_volatile(pointer.cast::<u16>(), value);
            };
        }
    }

    fn common_read_u32(&mut self, offset: u32) -> u32 {
        #[allow(clippy::cast_ptr_alignment)]
        self.common_ptr(offset, 4).map_or(0xffff_ffff, |pointer| {
            // SAFETY: The constructor contract and width check make this a
            // bounded, aligned volatile MMIO access.
            unsafe { core::ptr::read_volatile(pointer.cast::<u32>()) }
        })
    }

    fn common_write_u32(&mut self, offset: u32, value: u32) {
        if let Some(pointer) = self.common_ptr(offset, 4) {
            // SAFETY: See `common_read_u32`; this is bounded volatile MMIO.
            // SAFETY: The constructor contract proves the base alignment and
            // the checked offset is aligned to the access width.
            #[allow(clippy::cast_ptr_alignment)]
            unsafe {
                core::ptr::write_volatile(pointer.cast::<u32>(), value);
            };
        }
    }

    fn notify_write_u16(&mut self, offset: u32, value: u16) {
        if let Some(pointer) = self.notify_ptr(offset, 2) {
            // SAFETY: The constructor contract and width check make this a
            // bounded, aligned volatile notification write.
            #[allow(clippy::cast_ptr_alignment)]
            unsafe {
                core::ptr::write_volatile(pointer.cast::<u16>(), value);
            };
        }
    }

    fn isr_read_u8(&mut self, offset: u32) -> u8 {
        self.isr_ptr(offset, 1).map_or(0, |pointer| {
            // SAFETY: The constructor contract requires a live supervisor MMIO
            // mapping. A read of the ISR structure is device-defined to clear
            // the register, which is exactly why this is a dedicated accessor
            // rather than a general read.
            unsafe { core::ptr::read_volatile(pointer) }
        })
    }
}

/// Errors returned by the modern VirtIO-PCI transport policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VirtioPciTransportError {
    /// The device reported no queues or more than the bounded capacity.
    InvalidQueueCount(u16),
    /// The requested queue index is not supported.
    InvalidQueueIndex(u16),
    /// The requested queue size is below the minimum supported size.
    QueueSizeTooSmall(u16),
    /// The requested queue size is not a power of two.
    QueueSizeNotPowerOfTwo(u16),
    /// The device's maximum queue size is invalid or smaller than requested.
    QueueSizeUnavailable {
        /// Requested queue size.
        requested: u16,
        /// Device-advertised maximum size.
        maximum: u16,
    },
    /// The device did not retain the requested split-ring queue size.
    QueueSizeWriteFailed {
        /// Requested queue size.
        requested: u16,
        /// Device-advertised maximum size.
        maximum: u16,
    },
    /// A queue address is null, misaligned, or overflows its ring.
    InvalidQueueAddress,
    /// The notification address arithmetic overflowed or exceeded its region.
    NotifyAddressOverflow,
    /// The device rejected the feature set by clearing `FEATURES_OK`.
    FeatureNegotiationFailed(u8),
    /// The device entered the failed state during initialization.
    DeviceFailed(u8),
    /// The queue was not enabled before notification.
    QueueNotEnabled,
    /// A queue notification used a queue that was not configured.
    QueueNotConfigured(u16),
    /// The device exposes no interrupt-status structure.
    InterruptStatusUnsupported,
    /// No free MSI-X vector remains for the requested queue.
    NoFreeVector(u16),
    /// The device did not retain the requested MSI-X vector.
    VectorWriteRejected(u16),
    /// The queue was already bound to an MSI-X vector.
    VectorAlreadyBound(u16),
}

/// Interrupt-signalling state of a configured queue.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum QueueInterrupt {
    /// No MSI-X vector is bound; completion can only be observed by polling.
    Polled,
    /// An MSI-X vector is bound and completion can be signalled by the device.
    Signalled {
        /// Allocated MSI-X vector index owned by this driver.
        vector: u16,
    },
}

/// Bounded per-queue MSI-X vector ownership for one modern `VirtIO` device.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VectorAllocator {
    bound: [u16; MAX_QUEUE_SIZE],
    assigned: [bool; MAX_QUEUE_SIZE],
    used: u16,
    capacity: u16,
}

impl VectorAllocator {
    /// Create an allocator bounded by the device's MSI-X vector count.
    #[must_use]
    pub const fn new(capacity: u16) -> Self {
        Self {
            bound: [0; MAX_QUEUE_SIZE],
            assigned: [false; MAX_QUEUE_SIZE],
            used: 0,
            capacity,
        }
    }

    /// Bind one queue to the lowest free MSI-X vector.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] for an
    /// out-of-range queue, [`VirtioPciTransportError::VectorAlreadyBound`]
    /// when the queue already owns a vector, and
    /// [`VirtioPciTransportError::NoFreeVector`] when the bounded table is
    /// exhausted.
    pub fn bind(&mut self, queue_index: u16) -> Result<QueueInterrupt, VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if self.assigned[slot] {
            return Err(VirtioPciTransportError::VectorAlreadyBound(queue_index));
        }
        if self.capacity == 0 || self.used >= self.capacity {
            return Err(VirtioPciTransportError::NoFreeVector(queue_index));
        }
        let vector = self.used;
        self.used += 1;
        self.bound[slot] = vector;
        self.assigned[slot] = true;
        Ok(QueueInterrupt::Signalled { vector })
    }

    /// Release one queue's MSI-X vector so it can be reused.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] for an
    /// out-of-range queue. Releasing an unbound queue is idempotent.
    pub fn release(&mut self, queue_index: u16) -> Result<(), VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if self.assigned[slot] {
            self.assigned[slot] = false;
            self.used = self.used.saturating_sub(1);
        }
        Ok(())
    }

    /// Return the interrupt state of one configured queue.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] for an
    /// out-of-range queue.
    pub fn interrupt_for(
        &self,
        queue_index: u16,
    ) -> Result<QueueInterrupt, VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if self.assigned[slot] {
            Ok(QueueInterrupt::Signalled {
                vector: self.bound[slot],
            })
        } else {
            Ok(QueueInterrupt::Polled)
        }
    }
}

/// A bounded modern VirtIO-PCI transport policy.
pub struct VirtioPciTransport<M> {
    mmio: M,
    queue_count: u16,
    queue_notify_offsets: [Option<u16>; MAX_QUEUE_SIZE],
    queue_enabled: [bool; MAX_QUEUE_SIZE],
    notify_offset_multiplier: u32,
    common_length: u32,
    notify_length: u32,
    status: u8,
    vectors: VectorAllocator,
    interrupt_status_present: bool,
}

impl<M: VirtioPciMmio> VirtioPciTransport<M> {
    /// Construct a transport around bounded common and notification regions.
    ///
    /// `vector_capacity` is the device's MSI-X vector count. Zero means the
    /// device cannot signal completion with an interrupt, so queues stay
    /// polled by explicit policy rather than by omission.
    #[must_use]
    pub const fn new(
        mmio: M,
        common_length: u32,
        notify_length: u32,
        notify_offset_multiplier: u32,
        vector_capacity: u16,
    ) -> Self {
        Self::with_interrupts(
            mmio,
            common_length,
            notify_length,
            notify_offset_multiplier,
            vector_capacity,
            false,
        )
    }

    /// Construct a transport and record whether the device exposes an
    /// interrupt-status structure.
    ///
    /// `interrupt_status_present` reflects the resolved ISR capability, not an
    /// assumption. When it is `false`, [`Self::read_interrupt_status`] fails
    /// explicitly so completion can never be reported from a missing
    /// structure.
    #[must_use]
    pub const fn with_interrupts(
        mmio: M,
        common_length: u32,
        notify_length: u32,
        notify_offset_multiplier: u32,
        vector_capacity: u16,
        interrupt_status_present: bool,
    ) -> Self {
        Self {
            mmio,
            queue_count: 0,
            queue_notify_offsets: [None; MAX_QUEUE_SIZE],
            queue_enabled: [false; MAX_QUEUE_SIZE],
            notify_offset_multiplier,
            common_length,
            notify_length,
            status: 0,
            vectors: VectorAllocator::new(vector_capacity),
            interrupt_status_present,
        }
    }

    /// Return the current driver-visible device status byte.
    #[must_use]
    pub const fn status(&self) -> u8 {
        self.status
    }

    /// Return the device-advertised queue count after initialization.
    #[must_use]
    pub const fn queue_count(&self) -> u16 {
        self.queue_count
    }

    /// Return the common configuration region length.
    #[must_use]
    pub const fn common_length(&self) -> u32 {
        self.common_length
    }

    /// Read the interrupt-status structure and report the pending bits.
    ///
    /// The read is destructive by device contract: the device atomically
    /// clears the register and deasserts its interrupt line as part of
    /// servicing the access. Reading is therefore the acknowledgement, and
    /// this must be called exactly once per delivered interrupt.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InterruptStatusUnsupported`] when
    /// the device exposes no interrupt-status structure. That is a device
    /// capability fact and must not be smoothed into "no completion".
    pub fn read_interrupt_status(
        &mut self,
    ) -> Result<QueueInterruptFlags, VirtioPciTransportError> {
        if !self.interrupt_status_present {
            return Err(VirtioPciTransportError::InterruptStatusUnsupported);
        }
        Ok(QueueInterruptFlags::from_device_byte(
            self.mmio.isr_read_u8(0),
        ))
    }

    /// Return the interrupt state of one configured queue.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] for a queue
    /// outside the bounded set.
    pub fn queue_interrupt(
        &self,
        queue_index: u16,
    ) -> Result<QueueInterrupt, VirtioPciTransportError> {
        self.vectors.interrupt_for(queue_index)
    }

    /// Program one configured queue's MSI-X vector.
    ///
    /// This only programs the device-side vector index in the common
    /// configuration structure. The caller must own the platform MSI-X table
    /// entry that makes the allocated vector deliverable, so this method
    /// never claims an interrupt is routed merely because the index was
    /// accepted.
    ///
    /// # Errors
    ///
    /// Returns an error for an unknown or unconfigured queue, when the
    /// device exposes no interrupt-status structure, when no vector is free,
    /// or when the device does not retain the programmed vector.
    pub fn enable_queue_interrupt(
        &mut self,
        queue_index: u16,
    ) -> Result<QueueInterrupt, VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE || queue_index >= self.queue_count {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if !self.queue_enabled[slot] {
            return Err(VirtioPciTransportError::QueueNotConfigured(queue_index));
        }
        if !self.interrupt_status_present {
            return Err(VirtioPciTransportError::InterruptStatusUnsupported);
        }
        let QueueInterrupt::Signalled { vector } = self.vectors.bind(queue_index)? else {
            return Err(VirtioPciTransportError::InterruptStatusUnsupported);
        };
        self.write_queue_select(queue_index);
        self.mmio
            .common_write_u16(common::QUEUE_MSIX_VECTOR, vector);
        let readback = self.mmio.common_read_u16(common::QUEUE_MSIX_VECTOR);
        if readback != vector {
            self.vectors.release(queue_index)?;
            return Err(VirtioPciTransportError::VectorWriteRejected(queue_index));
        }
        Ok(QueueInterrupt::Signalled { vector })
    }

    /// Release one queue's MSI-X vector and mask it on the device.
    ///
    /// # Errors
    ///
    /// Returns an error for a queue outside the bounded set.
    pub fn disable_queue_interrupt(
        &mut self,
        queue_index: u16,
    ) -> Result<(), VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE || queue_index >= self.queue_count {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        self.write_queue_select(queue_index);
        self.mmio
            .common_write_u16(common::QUEUE_MSIX_VECTOR, NO_VECTOR);
        self.vectors.release(queue_index)
    }

    /// Read the device status register from MMIO.
    pub fn read_status(&mut self) -> u8 {
        self.mmio.common_read_u8(common::STATUS)
    }

    /// Write the device status register to MMIO.
    pub fn write_status(&mut self, status: u8) {
        self.mmio.common_write_u8(common::STATUS, status);
        self.status = status;
    }

    /// Reset the device and enter the acknowledge/driver states.
    ///
    /// The caller must have released all queue buffers before calling this
    /// method. This bounded policy does not claim asynchronous reset safety.
    pub fn begin(&mut self) {
        self.write_status(0);
        self.write_status(VIRTIO_STATUS_ACKNOWLEDGE);
        self.write_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    }

    /// Read and validate the device queue count.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueCount`] when the device
    /// reports no queues or more queues than this bounded implementation.
    pub fn initialize(&mut self) -> Result<u16, VirtioPciTransportError> {
        let count = self.mmio.common_read_u16(common::NUM_QUEUES);
        if count == 0 || usize::from(count) > MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueCount(count));
        }
        self.queue_count = count;
        Ok(count)
    }

    /// Select and read one device feature word.
    pub fn read_device_features(&mut self, select: u32) -> u32 {
        if select >= VIRTIO_FEATURE_WORDS {
            return 0;
        }
        self.mmio
            .common_write_u32(common::DEVICE_FEATURE_SELECT, select);
        self.mmio.common_read_u32(common::DEVICE_FEATURE)
    }

    /// Negotiate supported features and verify the device accepted them.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::FeatureNegotiationFailed`] when the
    /// device clears `FEATURES_OK`, or [`VirtioPciTransportError::DeviceFailed`]
    /// when it enters the failed state.
    pub fn negotiate_features(
        &mut self,
        supported_features: [u64; 2],
    ) -> Result<u64, VirtioPciTransportError> {
        let mut offered = [0u64; 2];
        let mut selected = [0u64; 2];
        for select in 0..VIRTIO_FEATURE_WORDS {
            offered[select as usize] = u64::from(self.read_device_features(select));
            selected[select as usize] =
                offered[select as usize] & supported_features[select as usize];
        }
        for (select, features) in selected.iter().enumerate() {
            let select = u32::try_from(select).unwrap_or(u32::MAX);
            self.mmio
                .common_write_u32(common::DRIVER_FEATURE_SELECT, select);
            self.mmio.common_write_u32(
                common::DRIVER_FEATURE,
                u32::try_from(*features).unwrap_or(0),
            );
        }
        self.write_status(
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK,
        );
        let status = self.read_status();
        if status & VIRTIO_STATUS_FAILED != 0 {
            return Err(VirtioPciTransportError::DeviceFailed(status));
        }
        if status & VIRTIO_STATUS_FEATURES_OK == 0 {
            return Err(VirtioPciTransportError::FeatureNegotiationFailed(status));
        }
        let mut accepted = 0u64;
        for select in 0..VIRTIO_FEATURE_WORDS {
            accepted |= selected[select as usize] << (select * 32);
        }
        Ok(accepted)
    }

    /// Configure one split virtqueue after feature negotiation.
    ///
    /// # Errors
    ///
    /// Rejects invalid queue indexes/sizes, non-power-of-two sizes, malformed
    /// ring addresses, and queue-enable failures.
    pub fn configure_queue(
        &mut self,
        queue_index: u16,
        requested_size: u16,
        layout: &SplitVirtqueueLayout,
    ) -> Result<u16, VirtioPciTransportError> {
        if queue_index >= self.queue_count || usize::from(queue_index) >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if requested_size < 2 {
            return Err(VirtioPciTransportError::QueueSizeTooSmall(requested_size));
        }
        if !requested_size.is_power_of_two() {
            return Err(VirtioPciTransportError::QueueSizeNotPowerOfTwo(
                requested_size,
            ));
        }
        if layout.queue_index != queue_index || layout.queue_size != requested_size {
            return Err(VirtioPciTransportError::InvalidQueueAddress);
        }
        validate_queue_addresses(layout)?;
        self.write_queue_select(queue_index);
        let maximum = self.read_queue_size();
        if maximum < 2 || requested_size > maximum {
            return Err(VirtioPciTransportError::QueueSizeUnavailable {
                requested: requested_size,
                maximum,
            });
        }
        self.write_queue_size(requested_size);
        if self.read_queue_size() != requested_size {
            return Err(VirtioPciTransportError::QueueSizeWriteFailed {
                requested: requested_size,
                maximum,
            });
        }
        let notify_offset = self.read_queue_notify_offset();
        self.validate_notification_offset(notify_offset)?;
        self.write_queue_addresses(VirtioPciQueueAddresses {
            descriptor: layout.descriptor_address,
            available: layout.available_address,
            used: layout.used_address,
        });
        self.write_queue_enable(1);
        if self.read_queue_enable() != 1 {
            return Err(VirtioPciTransportError::QueueNotEnabled);
        }
        self.queue_notify_offsets[usize::from(queue_index)] = Some(notify_offset);
        self.queue_enabled[usize::from(queue_index)] = true;
        Ok(maximum)
    }

    /// Read the current value of the selected queue's enable register.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] when the queue
    /// is outside the device's bounded queue set.
    pub fn read_queue_enabled(&mut self, queue_index: u16) -> Result<u16, VirtioPciTransportError> {
        if queue_index >= self.queue_count || usize::from(queue_index) >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        self.write_queue_select(queue_index);
        Ok(self.read_queue_enable())
    }

    /// Complete the driver-ready state and verify the device accepted it.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::DeviceFailed`] if the device did
    /// not retain the `DRIVER_OK` state.
    pub fn finish(&mut self) -> Result<(), VirtioPciTransportError> {
        self.write_status(
            VIRTIO_STATUS_ACKNOWLEDGE
                | VIRTIO_STATUS_DRIVER
                | VIRTIO_STATUS_FEATURES_OK
                | VIRTIO_STATUS_DRIVER_OK,
        );
        let status = self.read_status();
        if status & VIRTIO_STATUS_DRIVER_OK == 0 {
            return Err(VirtioPciTransportError::DeviceFailed(status));
        }
        Ok(())
    }

    /// Read the selected queue's used-ring address after configuration.
    ///
    /// # Errors
    ///
    /// Returns [`VirtioPciTransportError::InvalidQueueIndex`] when the queue
    /// is outside the device's bounded queue set.
    pub fn read_queue_used_address(
        &mut self,
        queue_index: u16,
    ) -> Result<u64, VirtioPciTransportError> {
        if queue_index >= self.queue_count || usize::from(queue_index) >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        self.write_queue_select(queue_index);
        Ok(u64::from(self.mmio.common_read_u32(common::QUEUE_DEVICE))
            | (u64::from(self.mmio.common_read_u32(common::QUEUE_DEVICE + 4)) << 32))
    }

    /// Return the architecture adapter after a successful transport bring-up.
    pub fn into_mmio(self) -> M {
        self.mmio
    }

    /// Notify the device that a configured queue has new available entries.
    ///
    /// # Errors
    ///
    /// Rejects an unconfigured queue and notification-region arithmetic
    /// overflow. The multiplier is applied exactly once, as required by the
    /// modern VirtIO-PCI notification capability.
    pub fn notify(&mut self, queue_index: u16) -> Result<(), VirtioPciTransportError> {
        let slot = usize::from(queue_index);
        if slot >= MAX_QUEUE_SIZE {
            return Err(VirtioPciTransportError::InvalidQueueIndex(queue_index));
        }
        if !self.queue_enabled[slot] {
            return Err(VirtioPciTransportError::QueueNotConfigured(queue_index));
        }
        let notify_offset = self.queue_notify_offsets[slot]
            .ok_or(VirtioPciTransportError::QueueNotConfigured(queue_index))?;
        let relative = self.notification_relative_offset(notify_offset)?;
        let relative =
            u32::try_from(relative).map_err(|_| VirtioPciTransportError::NotifyAddressOverflow)?;
        self.mmio.notify_write_u16(relative, queue_index);
        Ok(())
    }

    fn validate_notification_offset(&self, offset: u16) -> Result<(), VirtioPciTransportError> {
        self.notification_relative_offset(offset).map(|_| ())
    }

    fn notification_relative_offset(&self, offset: u16) -> Result<u64, VirtioPciTransportError> {
        let relative = u64::from(offset)
            .checked_mul(u64::from(self.notify_offset_multiplier))
            .ok_or(VirtioPciTransportError::NotifyAddressOverflow)?;
        let end = relative
            .checked_add(2)
            .ok_or(VirtioPciTransportError::NotifyAddressOverflow)?;
        if end > u64::from(self.notify_length) {
            return Err(VirtioPciTransportError::NotifyAddressOverflow);
        }
        Ok(relative)
    }

    fn write_queue_select(&mut self, queue_index: u16) {
        self.mmio
            .common_write_u16(common::QUEUE_SELECT, queue_index);
    }

    fn read_queue_size(&mut self) -> u16 {
        self.mmio.common_read_u16(common::QUEUE_SIZE)
    }

    fn write_queue_size(&mut self, value: u16) {
        self.mmio.common_write_u16(common::QUEUE_SIZE, value);
    }

    fn read_queue_notify_offset(&mut self) -> u16 {
        self.mmio.common_read_u16(common::QUEUE_NOTIFY_OFF)
    }

    fn read_queue_enable(&mut self) -> u16 {
        self.mmio.common_read_u16(common::QUEUE_ENABLE)
    }

    fn write_queue_enable(&mut self, value: u16) {
        self.mmio.common_write_u16(common::QUEUE_ENABLE, value);
    }

    fn write_queue_addresses(&mut self, addresses: VirtioPciQueueAddresses) {
        self.write_u64(common::QUEUE_DESC, addresses.descriptor);
        self.write_u64(common::QUEUE_DRIVER, addresses.available);
        self.write_u64(common::QUEUE_DEVICE, addresses.used);
    }

    fn write_u64(&mut self, offset: u32, value: u64) {
        let low = value & 0xffff_ffff;
        let high = value >> 32;
        self.mmio
            .common_write_u32(offset, u32::try_from(low).unwrap_or(u32::MAX));
        self.mmio
            .common_write_u32(offset + 4, u32::try_from(high).unwrap_or(u32::MAX));
    }
}

impl<M: VirtioPciMmio> VirtqueueTransport for VirtioPciTransport<M> {
    fn notify(&mut self, publication: QueuePublication) -> Result<(), TransportError> {
        self.notify(publication.queue_index)
            .map_err(|error| match error {
                VirtioPciTransportError::InvalidQueueIndex(_)
                | VirtioPciTransportError::QueueNotConfigured(_)
                | VirtioPciTransportError::QueueNotEnabled => {
                    TransportError::InvalidQueue(publication.queue_index)
                }
                _ => TransportError::Rejected,
            })
    }
}

const fn validate_queue_addresses(
    layout: &SplitVirtqueueLayout,
) -> Result<(), VirtioPciTransportError> {
    if layout.descriptor_address == 0
        || layout.available_address == 0
        || layout.used_address == 0
        || !layout.descriptor_address.is_multiple_of(16)
        || !layout.available_address.is_multiple_of(2)
        || !layout.used_address.is_multiple_of(4)
    {
        return Err(VirtioPciTransportError::InvalidQueueAddress);
    }
    if layout
        .descriptor_address
        .checked_add(layout.descriptor_bytes())
        .is_none()
        || layout
            .available_address
            .checked_add(layout.available_bytes())
            .is_none()
        || layout
            .used_address
            .checked_add(layout.used_bytes())
            .is_none()
    {
        return Err(VirtioPciTransportError::InvalidQueueAddress);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        VirtioPciMmio, VirtioPciQueueAddresses, VirtioPciTransport, VirtioPciTransportError,
    };
    use crate::drivers::virtio::split_queue::SplitVirtqueueLayout;
    use crate::drivers::virtio::{
        VIRTIO_STATUS_ACKNOWLEDGE, VIRTIO_STATUS_DRIVER, VIRTIO_STATUS_DRIVER_OK,
        VIRTIO_STATUS_FEATURES_OK,
    };

    struct FakeMmio {
        common: [u8; 0x40],
        notify: [u8; 0x20],
        isr: [u8; 4],
        max_queue_size: u16,
        queue_count: u16,
        device_features: [u32; 2],
        feature_select: u32,
        driver_feature_select: u32,
        queue_enable: u16,
        selected_queue_size: u16,
        queue_size_readonly: bool,
        reject_features: bool,
        reject_vector_write: bool,
    }

    impl Default for FakeMmio {
        fn default() -> Self {
            // A real device reports NO_VECTOR until a driver programs a queue,
            // so the fake must not appear to accept vector zero by default.
            let vector = super::NO_VECTOR;
            let mut common = [0u8; 0x40];
            let offset = super::super::pci::common::QUEUE_MSIX_VECTOR as usize;
            common[offset..offset + 2].copy_from_slice(&vector.to_le_bytes());
            Self {
                common,
                notify: [0; 0x20],
                isr: [0; 4],
                max_queue_size: 0,
                queue_count: 0,
                device_features: [0; 2],
                feature_select: 0,
                driver_feature_select: 0,
                queue_enable: 0,
                selected_queue_size: 0,
                queue_size_readonly: false,
                reject_features: false,
                reject_vector_write: false,
            }
        }
    }

    impl FakeMmio {
        fn with_device() -> Self {
            Self {
                max_queue_size: 8,
                selected_queue_size: 0,
                queue_count: 1,
                device_features: [3, 0],
                ..Self::default()
            }
        }
    }

    impl VirtioPciMmio for FakeMmio {
        fn common_read_u8(&mut self, offset: u32) -> u8 {
            self.common[offset as usize]
        }

        fn common_write_u8(&mut self, offset: u32, value: u8) {
            if offset == super::super::pci::common::STATUS {
                self.common[offset as usize] = if self.reject_features
                    && value & super::super::VIRTIO_STATUS_FEATURES_OK != 0
                {
                    0
                } else {
                    value
                };
            } else {
                self.common[offset as usize] = value;
            }
        }

        fn common_read_u16(&mut self, offset: u32) -> u16 {
            if offset == super::super::pci::common::QUEUE_SIZE {
                if self.queue_size_readonly || self.selected_queue_size == 0 {
                    return self.max_queue_size;
                }
                return self.selected_queue_size;
            }
            if offset == super::super::pci::common::QUEUE_ENABLE {
                return self.queue_enable;
            }
            if offset == super::super::pci::common::NUM_QUEUES {
                return self.queue_count;
            }
            u16::from_le_bytes([
                self.common[offset as usize],
                self.common[offset as usize + 1],
            ])
        }

        fn common_write_u16(&mut self, offset: u32, value: u16) {
            if offset == super::super::pci::common::QUEUE_SIZE {
                if !self.queue_size_readonly {
                    self.selected_queue_size = value;
                }
                return;
            }
            if offset == super::super::pci::common::QUEUE_ENABLE {
                self.queue_enable = value;
                return;
            }
            if offset == super::super::pci::common::QUEUE_MSIX_VECTOR {
                if self.reject_vector_write {
                    return;
                }
                self.common[offset as usize..offset as usize + 2]
                    .copy_from_slice(&value.to_le_bytes());
                return;
            }
            self.common[offset as usize..offset as usize + 2].copy_from_slice(&value.to_le_bytes());
        }

        fn common_read_u32(&mut self, offset: u32) -> u32 {
            if offset == super::super::pci::common::DEVICE_FEATURE {
                return self.device_features[self.feature_select as usize];
            }
            u32::from_le_bytes([
                self.common[offset as usize],
                self.common[offset as usize + 1],
                self.common[offset as usize + 2],
                self.common[offset as usize + 3],
            ])
        }

        fn common_write_u32(&mut self, offset: u32, value: u32) {
            if offset == super::super::pci::common::DEVICE_FEATURE_SELECT {
                self.feature_select = value;
                return;
            }
            if offset == super::super::pci::common::DRIVER_FEATURE_SELECT {
                self.driver_feature_select = value;
                return;
            }
            self.common[offset as usize..offset as usize + 4].copy_from_slice(&value.to_le_bytes());
        }

        fn notify_write_u16(&mut self, offset: u32, value: u16) {
            self.notify[offset as usize..offset as usize + 2].copy_from_slice(&value.to_le_bytes());
        }

        fn isr_read_u8(&mut self, offset: u32) -> u8 {
            let value = self.isr[offset as usize];
            // A device-defined read clears the register.
            self.isr[offset as usize] = 0;
            value
        }
    }

    fn transport(mmio: FakeMmio) -> VirtioPciTransport<FakeMmio> {
        VirtioPciTransport::new(mmio, 0x1000, 0x1000, 4, 0)
    }

    fn interrupt_transport(
        mmio: FakeMmio,
        vector_capacity: u16,
        isr_present: bool,
    ) -> VirtioPciTransport<FakeMmio> {
        VirtioPciTransport::with_interrupts(mmio, 0x1000, 0x1000, 4, vector_capacity, isr_present)
    }

    fn configured_interrupts(
        mmio: FakeMmio,
        vector_capacity: u16,
        isr_present: bool,
    ) -> VirtioPciTransport<FakeMmio> {
        let mut transport = interrupt_transport(mmio, vector_capacity, isr_present);
        transport.begin();
        assert_eq!(transport.initialize(), Ok(1));
        let layout = SplitVirtqueueLayout::new(0, 8, 0x1000, 0x2000, 0x3000).unwrap();
        assert_eq!(transport.configure_queue(0, 8, &layout), Ok(8));
        transport
    }

    #[test]
    fn vector_allocator_is_bounded_and_reusable() {
        let mut allocator = super::VectorAllocator::new(2);
        assert_eq!(
            allocator.interrupt_for(0),
            Ok(super::QueueInterrupt::Polled)
        );
        assert_eq!(
            allocator.bind(0),
            Ok(super::QueueInterrupt::Signalled { vector: 0 })
        );
        assert_eq!(
            allocator.bind(1),
            Ok(super::QueueInterrupt::Signalled { vector: 1 })
        );
        assert_eq!(
            allocator.bind(2),
            Err(super::VirtioPciTransportError::NoFreeVector(2))
        );
        assert_eq!(
            allocator.bind(0),
            Err(super::VirtioPciTransportError::VectorAlreadyBound(0))
        );
        allocator.release(0).unwrap();
        assert_eq!(
            allocator.interrupt_for(0),
            Ok(super::QueueInterrupt::Polled)
        );
        // A released vector is reusable by a later queue.
        assert_eq!(
            allocator.bind(2),
            Ok(super::QueueInterrupt::Signalled { vector: 1 })
        );
        assert_eq!(
            allocator.interrupt_for(200),
            Err(super::VirtioPciTransportError::InvalidQueueIndex(200))
        );
    }

    #[test]
    fn zero_vector_capacity_means_polled_not_signalled() {
        let mut transport = configured_interrupts(FakeMmio::with_device(), 0, true);
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Polled)
        );
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Err(super::VirtioPciTransportError::NoFreeVector(0))
        );
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Polled)
        );
    }

    #[test]
    fn device_without_isr_refuses_interrupt_use_explicitly() {
        let mut transport = configured_interrupts(FakeMmio::with_device(), 4, false);
        assert_eq!(
            transport.read_interrupt_status(),
            Err(super::VirtioPciTransportError::InterruptStatusUnsupported)
        );
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Err(super::VirtioPciTransportError::InterruptStatusUnsupported)
        );
        // Failing to bind must not leak a vector from the bounded table.
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Polled)
        );
    }

    #[test]
    fn queue_vector_is_programmed_and_readback_verified() {
        let mut transport = configured_interrupts(FakeMmio::with_device(), 4, true);
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Ok(super::QueueInterrupt::Signalled { vector: 0 })
        );
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Signalled { vector: 0 })
        );
        assert_eq!(
            transport.read_interrupt_status(),
            Ok(super::QueueInterruptFlags::from_device_byte(0))
        );
        transport.disable_queue_interrupt(0).unwrap();
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Polled)
        );
        // Releasing returns the vector to the bounded table, so the same queue
        // may be re-bound.
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Ok(super::QueueInterrupt::Signalled { vector: 0 })
        );
    }

    #[test]
    fn rejected_vector_write_releases_the_allocation() {
        let mut mmio = FakeMmio::with_device();
        mmio.reject_vector_write = true;
        let mut transport = configured_interrupts(mmio, 4, true);
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Err(super::VirtioPciTransportError::VectorWriteRejected(0))
        );
        // The allocator must not retain a vector the device never accepted.
        assert_eq!(
            transport.queue_interrupt(0),
            Ok(super::QueueInterrupt::Polled)
        );
        let mut mmio = FakeMmio::with_device();
        mmio.reject_vector_write = false;
        let mut transport = configured_interrupts(mmio, 4, true);
        assert_eq!(
            transport.enable_queue_interrupt(0),
            Ok(super::QueueInterrupt::Signalled { vector: 0 })
        );
    }

    #[test]
    fn isr_read_is_destructive_and_reports_queue_completion() {
        let mut mmio = FakeMmio::with_device();
        mmio.isr[0] = 0b101;
        let mut transport = configured_interrupts(mmio, 4, true);
        let first = transport.read_interrupt_status().unwrap();
        assert_eq!(first.raw, 0b101);
        assert!(first.queue_completion_pending());
        // The device cleared the register as part of servicing the read.
        let second = transport.read_interrupt_status().unwrap();
        assert_eq!(second.raw, 0);
        assert!(!second.queue_completion_pending());
    }

    #[test]
    fn queue_configuration_programs_split_ring_and_notification() {
        let mut transport = transport(FakeMmio::with_device());
        transport.begin();
        assert_eq!(transport.initialize(), Ok(1));
        assert_eq!(
            transport.status(),
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
        );
        assert_eq!(transport.negotiate_features([u64::MAX, u64::MAX]), Ok(3));
        let layout = SplitVirtqueueLayout::new(0, 8, 0x10_0000, 0x20_0000, 0x30_0000).unwrap();
        assert_eq!(transport.configure_queue(0, 8, &layout), Ok(8));
        transport.notify(0).unwrap();
        assert_eq!(&transport.mmio.notify[0..2], &0u16.to_le_bytes());
        assert_eq!(
            &transport.mmio.common[0x20..0x24],
            &0x10_0000u64.to_le_bytes()[0..4]
        );
        assert_eq!(
            &transport.mmio.common[0x28..0x2c],
            &0x20_0000u64.to_le_bytes()[0..4]
        );
        assert_eq!(
            &transport.mmio.common[0x30..0x34],
            &0x30_0000u64.to_le_bytes()[0..4]
        );
        transport.finish().unwrap();
        assert_eq!(
            transport.status(),
            VIRTIO_STATUS_ACKNOWLEDGE
                | VIRTIO_STATUS_DRIVER
                | VIRTIO_STATUS_FEATURES_OK
                | VIRTIO_STATUS_DRIVER_OK
        );
        let _ = VirtioPciQueueAddresses {
            descriptor: 1,
            available: 1,
            used: 1,
        };
    }

    #[test]
    fn invalid_queue_and_notification_are_rejected_transactionally() {
        let mut transport = transport(FakeMmio::with_device());
        transport.begin();
        transport.initialize().unwrap();
        let layout = SplitVirtqueueLayout::new(0, 8, 0x10_0000, 0x20_0000, 0x30_0000).unwrap();
        assert_eq!(
            transport.configure_queue(0, 7, &layout),
            Err(VirtioPciTransportError::QueueSizeNotPowerOfTwo(7))
        );
        assert!(!transport.queue_enabled[0]);
        assert_eq!(
            transport.notify(0),
            Err(VirtioPciTransportError::QueueNotConfigured(0))
        );
        transport.mmio.common[super::super::pci::common::QUEUE_NOTIFY_OFF as usize] = 1;
        assert_eq!(transport.configure_queue(0, 8, &layout), Ok(8));
        transport.notify_offset_multiplier = u32::MAX;
        transport.queue_notify_offsets[0] = Some(1);
        assert_eq!(
            transport.notify(0),
            Err(VirtioPciTransportError::NotifyAddressOverflow)
        );
    }

    #[test]
    fn feature_negotiation_reports_device_rejection() {
        let mut mmio = FakeMmio::with_device();
        mmio.reject_features = true;
        let mut transport = transport(mmio);
        transport.begin();
        assert!(matches!(
            transport.negotiate_features([0, 0]),
            Err(VirtioPciTransportError::FeatureNegotiationFailed(_))
        ));
    }

    #[test]
    fn queue_size_writeback_is_verified() {
        let mut mmio = FakeMmio::with_device();
        mmio.queue_size_readonly = true;
        let mut transport = transport(mmio);
        transport.begin();
        transport.initialize().unwrap();
        let layout = SplitVirtqueueLayout::new(0, 4, 0x10_0000, 0x20_0000, 0x30_0000).unwrap();
        assert!(matches!(
            transport.configure_queue(0, 4, &layout),
            Err(VirtioPciTransportError::QueueSizeWriteFailed {
                requested: 4,
                maximum: 8
            })
        ));
    }
}
