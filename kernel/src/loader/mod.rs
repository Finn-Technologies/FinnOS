//! User-space program loading policy.
//!
//! This module hosts the host-testable validation policy for user `ELF`
//! binaries. Address-space wiring and process spawning are intentionally
//! deferred to a later step; this module only validates structure, ranges,
//! and `W^X` policy.

/// `ELF` image validation for user-space programs.
pub mod elf;
/// Address-space mapping and loading for validated `ELF` images.
pub mod map;

pub use elf::{ElfError, ElfHeader, LoadSegment, ValidatedImage, validate_elf};
pub use map::{
    AddressSpaceMapper, FrameAllocator, LoadedImage, MapError, USER_STACK_BASE, USER_STACK_PAGES,
    USER_STACK_SIZE, USER_STACK_TOP, load_elf_image,
};
