//! `FinnOS` physical memory-map parsing and classification.
//!
//! This module safely parses the raw UEFI memory map handed over by the boot
//! manager and converts it into architecture-neutral, classified memory
//! regions. It does not allocate memory; all tables use fixed-capacity arrays.

pub mod allocator;
pub mod heap;
pub mod map;
pub mod region;
pub mod uefi;

#[cfg(test)]
mod tests;

pub use allocator::{
    EarlyPhysicalPageAllocator, MAX_FREE_EXTENTS, MAX_MANAGED_EXTENTS, PAGE_SIZE,
    PageAllocationError, PageRange, PhysicalPage,
};
pub use map::{MemoryMapSummary, RegionTable, parse_and_classify, validate_table};
pub use region::{
    MAX_MEMORY_REGIONS, MemoryRegion, MemoryRegionKind, MemoryRegionSource, UefiMemoryType,
};

/// Errors that can occur while parsing or classifying the UEFI memory map.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryMapError {
    /// The memory-map flag is not present in `BootInfo`.
    MissingMemoryMap,
    /// The memory-map address is null.
    NullMemoryMapAddress,
    /// The descriptor size is zero.
    ZeroDescriptorSize,
    /// The descriptor size is smaller than the required base fields.
    DescriptorTooSmall,
    /// The map length is not divisible by the descriptor stride.
    MisalignedMapLength,
    /// The map address or length overflows.
    AddressOverflow,
    /// A descriptor page count overflows.
    PageCountOverflow,
    /// A descriptor start plus size wraps around.
    RegionOverflow,
    /// Firmware descriptors overlap each other.
    OverlappingFirmwareRegions,
    /// The output region table exceeded its fixed capacity.
    OutputCapacityExceeded,
    /// The kernel image range is invalid.
    InvalidKernelRange,
    /// The framebuffer range is invalid.
    InvalidFramebufferRange,
    /// The `BootInfo` range is invalid.
    InvalidBootInfoRange,
    /// The raw memory-map storage range is invalid.
    InvalidMemoryMapStorageRange,
    /// A protected range is not fully covered by the firmware memory map.
    ProtectedRangeOutsideFirmwareMap,
    /// Two FinnOS-protected ranges overlap.
    OverlappingProtectedRanges,
    /// An unknown UEFI memory type was encountered.
    UnknownFirmwareType,
}
