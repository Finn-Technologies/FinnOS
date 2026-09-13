#![deny(missing_docs)]

//! Host-testable `VirtIO` policy foundation for block devices.
//!
//! This module captures portable `VirtIO` constants, split-virtqueue
//! descriptor-chain validation, feature negotiation, and `VirtIO` block
//! request policy. It is intentionally transport free: there is no MMIO, IRQ,
//! DMA, virtqueue memory layout, or device reset here.
//!
//! Real DMA and IRQ remain **Blocked** on the driver-architecture resource
//! broker (`DMA` ownership and buffer lifetime) and on interrupt routing for
//! queue notifications and completion interrupts. Architecture-specific PCI
//! access lives behind [`super::pci`]; transport bring-up must not bypass
//! those gates.
//!
//! The policy in this module is shared by `x86_64` and `AArch64`. There is no
//! architecture-specific mechanism here, so no parity table is needed.

pub mod gpu;

/// PCI vendor ID shared by all `VirtIO` devices (`0x1AF4`).
pub const VIRTIO_VENDOR: u16 = 0x1AF4;
/// PCI device ID of the legacy `VirtIO` block device (`0x1001`).
pub const VIRTIO_BLK_DEVICE: u16 = 0x1001;

/// Bit index of the read-only `VirtIO` block feature (`READ_ONLY` = 5).
pub const VIRTIO_BLK_F_READ_ONLY_BIT: u32 = 5;
/// Bit index of the flush `VirtIO` block feature (`FLUSH` = 9).
pub const VIRTIO_BLK_F_FLUSH_BIT: u32 = 9;
/// Feature mask for read-only `VirtIO` block devices (`1 << 5`).
pub const VIRTIO_BLK_F_READ_ONLY: u32 = 1_u32 << VIRTIO_BLK_F_READ_ONLY_BIT;
/// Feature mask for `VirtIO` block flush support (`1 << 9`).
pub const VIRTIO_BLK_F_FLUSH: u32 = 1_u32 << VIRTIO_BLK_F_FLUSH_BIT;

/// `VirtIO` device status: guest has acknowledged the device (`1`).
pub const VIRTIO_STATUS_ACKNOWLEDGE: u8 = 1;
/// `VirtIO` device status: guest knows how to drive the device (`2`).
pub const VIRTIO_STATUS_DRIVER: u8 = 2;
/// `VirtIO` device status: driver initialization is complete (`4`).
pub const VIRTIO_STATUS_DRIVER_OK: u8 = 4;
/// `VirtIO` device status: feature negotiation is complete (`8`).
pub const VIRTIO_STATUS_FEATURES_OK: u8 = 8;
/// `VirtIO` device status: something went wrong, stop the device (`128`).
pub const VIRTIO_STATUS_FAILED: u8 = 128;

/// Maximum number of descriptors in a single virtqueue (`128`).
pub const MAX_QUEUE_SIZE: usize = 128;
/// Descriptor flag: the chain continues via the `next` field (`1`).
pub const DESC_F_NEXT: u16 = 1;
/// Descriptor flag: the buffer is device-writable (`2`).
pub const DESC_F_WRITE: u16 = 2;

/// Size in bytes of one `VirtIO` block sector (`512`).
pub const BLK_SECTOR_SIZE: usize = 512;

/// `VirtIO` block request type: read sectors from the device (`0`).
pub const BLK_REQ_IN: u32 = 0;
/// `VirtIO` block request type: write sectors to the device (`1`).
pub const BLK_REQ_OUT: u32 = 1;
/// `VirtIO` block request type: flush the device write cache (`4`).
pub const BLK_REQ_FLUSH: u32 = 4;

/// Encoded size in bytes of [`BlkReqHeader`]: a `u32` type plus a `u64` sector.
pub const BLK_REQ_HEADER_LEN: usize = 12;

/// One split-virtqueue descriptor: a buffer plus chain linkage.
///
/// `addr` is a guest-physical address placeholder. This policy layer never
/// dereferences it: real DMA mapping and bounce-buffer ownership are Blocked
/// on the driver-architecture resource broker, so validation only reasons
/// about chain structure, lengths, and direction flags.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct VirtqDesc {
    /// Guest-physical address of the buffer (never dereferenced here).
    pub addr: u64,
    /// Length in bytes of the buffer.
    pub len: u32,
    /// Descriptor flags (`DESC_F_NEXT`, `DESC_F_WRITE`).
    pub flags: u16,
    /// Index of the next descriptor when `DESC_F_NEXT` is set.
    pub next: u16,
}

/// Structural descriptor-chain rejection reason.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DescError {
    /// The head index is outside the descriptor table.
    HeadOutOfBounds,
    /// A `next` link points outside the descriptor table.
    NextOutOfBounds {
        /// Table index that the faulty `next` link pointed at.
        index: usize,
    },
    /// The chain did not terminate within `max_len` descriptors, so it is
    /// either too long or cyclic.
    TooLong,
    /// A descriptor carries a zero-length buffer. No zero-length placeholder
    /// is currently allow-listed: every descriptor of a `VirtIO` block
    /// request carries header, data, or status payload.
    ZeroLength {
        /// Table index of the zero-length descriptor.
        index: usize,
    },
    /// The head descriptor is device-writable. The head always carries the
    /// driver-to-device request header, so `DESC_F_WRITE` must be clear on it.
    UnexpectedWrite {
        /// Table index of the offending descriptor (always the head).
        index: usize,
    },
}

/// Validate a split-virtqueue descriptor chain without touching hardware.
///
/// Walks at most `max_len` descriptors starting at `head` and returns the
/// number of descriptors in the chain. Pass [`MAX_QUEUE_SIZE`] as `max_len`
/// unless a smaller bound is known.
///
/// # WRITE-flag policy
///
/// The head descriptor carries the driver-to-device request header, so a
/// writable head is rejected with [`DescError::UnexpectedWrite`]. Full
/// direction checks need the request type and belong to the block layer, not
/// to this structural validator: data buffers for `IN` reads must be
/// device-writable, `OUT` header/data buffers must stay readable, and the
/// trailing status byte must always be device-writable.
///
/// # Errors
///
/// Returns [`DescError::HeadOutOfBounds`] when `head` is outside `descs`,
/// [`DescError::NextOutOfBounds`] for a bad `next` link,
/// [`DescError::TooLong`] when the chain does not terminate within `max_len`,
/// [`DescError::ZeroLength`] for an empty buffer, or
/// [`DescError::UnexpectedWrite`] for a writable head.
pub fn validate_chain(descs: &[VirtqDesc], head: u16, max_len: usize) -> Result<usize, DescError> {
    if descs.get(usize::from(head)).is_none() {
        return Err(DescError::HeadOutOfBounds);
    }
    let mut count: usize = 0;
    let mut index: usize = usize::from(head);
    loop {
        if count >= max_len {
            return Err(DescError::TooLong);
        }
        let desc = match descs.get(index) {
            Some(entry) => *entry,
            None => return Err(DescError::NextOutOfBounds { index }),
        };
        if desc.len == 0 {
            return Err(DescError::ZeroLength { index });
        }
        if count == 0 && desc.flags & DESC_F_WRITE != 0 {
            return Err(DescError::UnexpectedWrite { index });
        }
        count += 1;
        if desc.flags & DESC_F_NEXT == 0 {
            return Ok(count);
        }
        index = usize::from(desc.next);
    }
}

/// Intersect device-offered features with driver-supported features.
///
/// Returns the bitwise AND of both sets. The caller must still complete the
/// status handshake (`FEATURES_OK`) on real hardware, which is Blocked on the
/// driver-architecture and interrupt-routing gates documented above.
#[must_use]
pub const fn negotiate_features(device_features: u32, supported: u32) -> u32 {
    device_features & supported
}

/// Rejection reason for a decoded [`BlkReqHeader`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BlkError {
    /// The request type is not `IN`, `OUT`, or `FLUSH`.
    UnknownRequestType(u32),
    /// The target sector is outside the disk (`sector >= disk_sectors`).
    SectorOutOfBounds {
        /// Requested sector number.
        sector: u64,
        /// Total number of sectors on the disk.
        disk_sectors: u64,
    },
}

/// `VirtIO` block request header: type plus starting sector.
///
/// The on-wire layout is 12 bytes little-endian: `req_type` followed by
/// `sector`. Payload buffers and the trailing status byte travel in separate
/// virtqueue descriptors and are validated by [`validate_chain`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct BlkReqHeader {
    /// Request type: [`BLK_REQ_IN`], [`BLK_REQ_OUT`], or [`BLK_REQ_FLUSH`].
    pub req_type: u32,
    /// Starting sector number (ignored for [`BLK_REQ_FLUSH`]).
    pub sector: u64,
}

impl BlkReqHeader {
    /// Encode the header into its 12-byte little-endian wire form.
    #[must_use]
    pub const fn encode(self) -> [u8; BLK_REQ_HEADER_LEN] {
        let [t0, t1, t2, t3] = self.req_type.to_le_bytes();
        let [s0, s1, s2, s3, s4, s5, s6, s7] = self.sector.to_le_bytes();
        [t0, t1, t2, t3, s0, s1, s2, s3, s4, s5, s6, s7]
    }

    /// Decode a header from its 12-byte little-endian wire form.
    ///
    /// Decoding never validates: call [`BlkReqHeader::validate`] afterwards to
    /// check the request type and sector bounds.
    #[must_use]
    pub const fn decode(bytes: [u8; BLK_REQ_HEADER_LEN]) -> Self {
        let [t0, t1, t2, t3, s0, s1, s2, s3, s4, s5, s6, s7] = bytes;
        Self {
            req_type: u32::from_le_bytes([t0, t1, t2, t3]),
            sector: u64::from_le_bytes([s0, s1, s2, s3, s4, s5, s6, s7]),
        }
    }

    /// Check the request type and sector against a disk of `disk_sectors`.
    ///
    /// `IN` and `OUT` requests require `sector < disk_sectors`. `FLUSH`
    /// carries no sector, so its sector field is ignored and any value is
    /// accepted.
    ///
    /// # Errors
    ///
    /// Returns [`BlkError::UnknownRequestType`] for an unsupported type, or
    /// [`BlkError::SectorOutOfBounds`] when an `IN`/`OUT` sector is outside
    /// the disk.
    pub const fn validate(self, disk_sectors: u64) -> Result<(), BlkError> {
        match self.req_type {
            BLK_REQ_IN | BLK_REQ_OUT => {
                if self.sector >= disk_sectors {
                    return Err(BlkError::SectorOutOfBounds {
                        sector: self.sector,
                        disk_sectors,
                    });
                }
                Ok(())
            }
            BLK_REQ_FLUSH => Ok(()),
            other => Err(BlkError::UnknownRequestType(other)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{
        BLK_REQ_FLUSH, BLK_REQ_IN, BLK_REQ_OUT, BlkError, BlkReqHeader, DESC_F_NEXT, DESC_F_WRITE,
        DescError, MAX_QUEUE_SIZE, VIRTIO_BLK_F_FLUSH, VIRTIO_BLK_F_READ_ONLY, VirtqDesc,
        negotiate_features, validate_chain,
    };

    const fn data_desc(addr: u64, next: u16) -> VirtqDesc {
        VirtqDesc {
            addr,
            len: 512,
            flags: DESC_F_NEXT,
            next,
        }
    }

    #[test]
    fn single_descriptor_chain_is_valid() {
        let descs = [VirtqDesc {
            addr: 0x1000,
            len: 512,
            flags: 0,
            next: 0,
        }];
        assert_eq!(validate_chain(&descs, 0, MAX_QUEUE_SIZE), Ok(1));
    }

    #[test]
    fn chained_descriptors_via_next_are_valid() {
        // Header (readable) -> data (readable) -> status (device-writable).
        let descs = [
            data_desc(0x1000, 1),
            data_desc(0x2000, 2),
            VirtqDesc {
                addr: 0x3000,
                len: 1,
                flags: DESC_F_WRITE,
                next: 0,
            },
        ];
        assert_eq!(validate_chain(&descs, 0, MAX_QUEUE_SIZE), Ok(3));
    }

    #[test]
    fn head_out_of_bounds_is_rejected() {
        let descs = [VirtqDesc {
            addr: 0x1000,
            len: 512,
            flags: 0,
            next: 0,
        }];
        assert_eq!(
            validate_chain(&descs, 5, MAX_QUEUE_SIZE),
            Err(DescError::HeadOutOfBounds)
        );
    }

    #[test]
    fn next_out_of_bounds_is_rejected() {
        let descs = [data_desc(0x1000, 7)];
        assert_eq!(
            validate_chain(&descs, 0, MAX_QUEUE_SIZE),
            Err(DescError::NextOutOfBounds { index: 7 })
        );
    }

    #[test]
    fn over_long_cyclic_chain_is_rejected() {
        // Unterminated 0 -> 1 -> 0 cycle must not spin forever.
        let descs = [data_desc(0x1000, 1), data_desc(0x2000, 0)];
        assert_eq!(validate_chain(&descs, 0, 4), Err(DescError::TooLong));
    }

    #[test]
    fn zero_length_descriptor_is_rejected() {
        let descs = [VirtqDesc {
            addr: 0x1000,
            len: 0,
            flags: 0,
            next: 0,
        }];
        assert_eq!(
            validate_chain(&descs, 0, MAX_QUEUE_SIZE),
            Err(DescError::ZeroLength { index: 0 })
        );
    }

    #[test]
    fn writable_head_is_rejected() {
        let descs = [VirtqDesc {
            addr: 0x1000,
            len: 16,
            flags: DESC_F_WRITE,
            next: 0,
        }];
        assert_eq!(
            validate_chain(&descs, 0, MAX_QUEUE_SIZE),
            Err(DescError::UnexpectedWrite { index: 0 })
        );
    }

    #[test]
    fn feature_negotiation_returns_intersection() {
        let device = VIRTIO_BLK_F_READ_ONLY | VIRTIO_BLK_F_FLUSH | (1_u32 << 12);
        assert_eq!(
            negotiate_features(device, VIRTIO_BLK_F_READ_ONLY),
            VIRTIO_BLK_F_READ_ONLY
        );
        assert_eq!(
            negotiate_features(device, VIRTIO_BLK_F_READ_ONLY | VIRTIO_BLK_F_FLUSH),
            VIRTIO_BLK_F_READ_ONLY | VIRTIO_BLK_F_FLUSH
        );
        assert_eq!(negotiate_features(device, 0), 0);
    }

    #[test]
    fn block_request_roundtrips_through_encoding() {
        let header = BlkReqHeader {
            req_type: BLK_REQ_IN,
            sector: 42,
        };
        assert_eq!(header.encode().len(), super::BLK_REQ_HEADER_LEN);
        assert_eq!(BlkReqHeader::decode(header.encode()), header);
        let write = BlkReqHeader {
            req_type: BLK_REQ_OUT,
            sector: 0,
        };
        assert_eq!(BlkReqHeader::decode(write.encode()), write);
    }

    #[test]
    fn block_request_rejects_sector_out_of_bounds() {
        let at_end = BlkReqHeader {
            req_type: BLK_REQ_OUT,
            sector: 128,
        };
        assert_eq!(
            at_end.validate(128),
            Err(BlkError::SectorOutOfBounds {
                sector: 128,
                disk_sectors: 128
            })
        );
        let past_end = BlkReqHeader {
            req_type: BLK_REQ_IN,
            sector: 1000,
        };
        assert!(past_end.validate(128).is_err());
        let inside = BlkReqHeader {
            req_type: BLK_REQ_IN,
            sector: 127,
        };
        assert_eq!(inside.validate(128), Ok(()));
    }

    #[test]
    fn block_flush_request_is_accepted() {
        let flush = BlkReqHeader {
            req_type: BLK_REQ_FLUSH,
            sector: 0,
        };
        assert_eq!(flush.validate(1), Ok(()));
        // The sector field is ignored for flush requests.
        let flush_any_sector = BlkReqHeader {
            req_type: BLK_REQ_FLUSH,
            sector: u64::MAX,
        };
        assert_eq!(flush_any_sector.validate(1), Ok(()));
    }

    #[test]
    fn block_request_rejects_unknown_type() {
        let bogus = BlkReqHeader {
            req_type: 2,
            sector: 0,
        };
        assert_eq!(bogus.validate(128), Err(BlkError::UnknownRequestType(2)));
    }
}
