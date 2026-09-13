//! Address-space mapping and loading for validated user `ELF` binaries.
#![allow(
    clippy::cast_possible_truncation,
    clippy::ignored_unit_patterns,
    clippy::must_use_candidate,
    clippy::missing_errors_doc,
    clippy::result_unit_err,
    clippy::manual_let_else
)]

use super::elf::{ElfError, PAGE_SIZE, ValidatedImage};

/// Canonical base address for the user process stack.
pub const USER_STACK_BASE: u64 = 0x0000_0000_0080_0000;
/// Default user process stack size in bytes (64 KiB = 16 pages).
pub const USER_STACK_SIZE: u64 = 0x0001_0000;
/// Number of 4 KiB pages in the user process stack.
pub const USER_STACK_PAGES: u64 = USER_STACK_SIZE / PAGE_SIZE;
/// Canonical top of the user process stack (16-byte aligned).
pub const USER_STACK_TOP: u64 = USER_STACK_BASE + USER_STACK_SIZE;

/// Result of loading an `ELF` image into an address space.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LoadedImage {
    /// Entry point virtual address.
    pub entry: u64,
    /// Stack pointer initial top virtual address.
    pub stack_top: u64,
    /// Total number of user pages mapped for code, data, and stack.
    pub pages_mapped: usize,
}

/// Errors that may occur while mapping an `ELF` image into an address space.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MapError {
    /// Validation of the `ELF` image failed.
    Elf(ElfError),
    /// Allocation of a physical page frame failed.
    OutOfMemory,
    /// Mapping a page into the address space failed.
    MappingFailed,
    /// Address arithmetic overflowed.
    ArithmeticOverflow,
}

impl From<ElfError> for MapError {
    fn from(err: ElfError) -> Self {
        Self::Elf(err)
    }
}

/// Physical frame allocator trait for loading segments and stacks.
pub trait FrameAllocator {
    /// Allocate one 4 KiB physical frame and return its base physical address.
    fn allocate_frame(&mut self) -> Result<u64, ()>;
}

/// Address space mapping interface for staging and mapping user pages.
pub trait AddressSpaceMapper {
    /// Prepare a 4096-byte physical page with content: zeroes the page,
    /// copies `data` at `dest_offset`, and performs instruction cache
    /// invalidation if `executable` is true.
    fn stage_page_content(
        &mut self,
        phys: u64,
        dest_offset: usize,
        data: &[u8],
        executable: bool,
    ) -> Result<(), ()>;

    /// Map a physical page at `user_vaddr` with the specified W^X permissions.
    fn map_user_page(
        &mut self,
        user_vaddr: u64,
        phys: u64,
        readable: bool,
        writable: bool,
        executable: bool,
    ) -> Result<(), ()>;
}

/// Align a virtual address down to the nearest page boundary.
#[must_use]
pub const fn align_down(addr: u64) -> u64 {
    addr & !(PAGE_SIZE - 1)
}

/// Align a virtual address up to the nearest page boundary.
#[must_use]
pub const fn align_up(addr: u64) -> Option<u64> {
    match addr.checked_add(PAGE_SIZE - 1) {
        Some(sum) => Some(align_down(sum)),
        None => None,
    }
}

/// Load a validated `ELF` image into an address space using the provided allocator and mapper.
///
/// Maps each loadable segment according to its declared permissions and size,
/// zeroing unbacked BSS areas. Allocates and maps a 64 KiB user stack at
/// [`USER_STACK_BASE`]..[`USER_STACK_TOP`] with Read-Write No-Execute permissions.
/// Leaves guard pages around the stack unmapped.
///
/// # Errors
///
/// Returns [`MapError::OutOfMemory`] if the allocator fails, [`MapError::MappingFailed`]
/// if the mapper fails, or [`MapError::ArithmeticOverflow`] on invalid address calculations.
pub fn load_elf_image<A: FrameAllocator, M: AddressSpaceMapper>(
    validated: &ValidatedImage,
    elf_bytes: &[u8],
    allocator: &mut A,
    mapper: &mut M,
) -> Result<LoadedImage, MapError> {
    let mut total_pages_mapped = 0;

    for i in 0..validated.count {
        let segment = match validated.get(i) {
            Some(seg) => seg,
            None => break,
        };
        let seg_vaddr = segment.vaddr;
        let seg_memsz = segment.memsz;
        let seg_filesz = segment.filesz;
        let seg_file_offset = segment.file_offset;

        let seg_end_vaddr = seg_vaddr
            .checked_add(seg_memsz)
            .ok_or(MapError::ArithmeticOverflow)?;
        let start_page = align_down(seg_vaddr);
        let end_page = align_up(seg_end_vaddr).ok_or(MapError::ArithmeticOverflow)?;

        let mut current_page = start_page;
        while current_page < end_page {
            let phys = allocator
                .allocate_frame()
                .map_err(|_| MapError::OutOfMemory)?;

            let page_next = current_page
                .checked_add(PAGE_SIZE)
                .ok_or(MapError::ArithmeticOverflow)?;

            // Determine overlap with file-backed portion [seg_vaddr, seg_vaddr + seg_filesz)
            let file_backed_end = seg_vaddr
                .checked_add(seg_filesz)
                .ok_or(MapError::ArithmeticOverflow)?;
            let overlap_start = core::cmp::max(current_page, seg_vaddr);
            let overlap_end = core::cmp::min(page_next, file_backed_end);

            if overlap_start < overlap_end {
                let dest_offset = (overlap_start - current_page) as usize;
                let data_len = (overlap_end - overlap_start) as usize;
                let src_offset_u64 = seg_file_offset
                    .checked_add(overlap_start - seg_vaddr)
                    .ok_or(MapError::ArithmeticOverflow)?;
                let src_offset = src_offset_u64 as usize;
                let src_end = src_offset
                    .checked_add(data_len)
                    .ok_or(MapError::ArithmeticOverflow)?;

                if src_end > elf_bytes.len() {
                    return Err(MapError::ArithmeticOverflow);
                }

                let data_slice = &elf_bytes[src_offset..src_end];
                mapper
                    .stage_page_content(phys, dest_offset, data_slice, segment.is_executable())
                    .map_err(|_| MapError::MappingFailed)?;
            } else {
                // Completely in BSS: stage zeroed page
                mapper
                    .stage_page_content(phys, 0, &[], segment.is_executable())
                    .map_err(|_| MapError::MappingFailed)?;
            }

            mapper
                .map_user_page(
                    current_page,
                    phys,
                    segment.is_readable(),
                    segment.is_writable(),
                    segment.is_executable(),
                )
                .map_err(|_| MapError::MappingFailed)?;

            total_pages_mapped += 1;
            current_page = page_next;
        }
    }

    // Allocate and map user stack (64 KiB = 16 pages)
    let mut stack_page = USER_STACK_BASE;
    while stack_page < USER_STACK_TOP {
        let phys = allocator
            .allocate_frame()
            .map_err(|_| MapError::OutOfMemory)?;

        mapper
            .stage_page_content(phys, 0, &[], false)
            .map_err(|_| MapError::MappingFailed)?;

        mapper
            .map_user_page(stack_page, phys, true, true, false)
            .map_err(|_| MapError::MappingFailed)?;

        total_pages_mapped += 1;
        stack_page = stack_page
            .checked_add(PAGE_SIZE)
            .ok_or(MapError::ArithmeticOverflow)?;
    }

    Ok(LoadedImage {
        entry: validated.entry,
        stack_top: USER_STACK_TOP,
        pages_mapped: total_pages_mapped,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::loader::elf::validate_elf;
    use crate::syscall::USER_SPACE_START;
    use std::collections::HashMap;
    use std::vec;
    use std::vec::Vec;

    struct MockAllocator {
        next_phys: u64,
        fail_after: usize,
        allocations: usize,
    }

    impl MockAllocator {
        fn new() -> Self {
            Self {
                next_phys: 0x1000_0000,
                fail_after: usize::MAX,
                allocations: 0,
            }
        }
    }

    impl FrameAllocator for MockAllocator {
        fn allocate_frame(&mut self) -> Result<u64, ()> {
            if self.allocations >= self.fail_after {
                return Err(());
            }
            self.allocations += 1;
            let phys = self.next_phys;
            self.next_phys += PAGE_SIZE;
            Ok(phys)
        }
    }

    #[derive(Clone, Debug)]
    #[allow(dead_code)]
    struct PageRecord {
        phys: u64,
        data: Vec<u8>,
        readable: bool,
        writable: bool,
        executable: bool,
    }

    struct MockMapper {
        pages: HashMap<u64, PageRecord>,
        phys_data: HashMap<u64, Vec<u8>>,
        fail_mapping: bool,
    }

    impl MockMapper {
        fn new() -> Self {
            Self {
                pages: HashMap::new(),
                phys_data: HashMap::new(),
                fail_mapping: false,
            }
        }
    }

    impl AddressSpaceMapper for MockMapper {
        fn stage_page_content(
            &mut self,
            phys: u64,
            dest_offset: usize,
            data: &[u8],
            _executable: bool,
        ) -> Result<(), ()> {
            let page = self
                .phys_data
                .entry(phys)
                .or_insert_with(|| vec![0u8; PAGE_SIZE as usize]);
            page[dest_offset..dest_offset + data.len()].copy_from_slice(data);
            Ok(())
        }

        fn map_user_page(
            &mut self,
            user_vaddr: u64,
            phys: u64,
            readable: bool,
            writable: bool,
            executable: bool,
        ) -> Result<(), ()> {
            if self.fail_mapping {
                return Err(());
            }
            let data = self
                .phys_data
                .get(&phys)
                .cloned()
                .unwrap_or_else(|| vec![0u8; PAGE_SIZE as usize]);
            self.pages.insert(
                user_vaddr,
                PageRecord {
                    phys,
                    data,
                    readable,
                    writable,
                    executable,
                },
            );
            Ok(())
        }
    }

    fn write_u16_le(target: &mut [u8], offset: usize, value: u16) {
        target[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u32_le(target: &mut [u8], offset: usize, value: u32) {
        target[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u64_le(target: &mut [u8], offset: usize, value: u64) {
        target[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }

    fn build_test_elf() -> Vec<u8> {
        let ehdr_size = 64;
        let phdr_size = 56;
        let mut image = vec![0u8; ehdr_size + phdr_size + 64];
        image[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        image[4] = 2; // 64-bit
        image[5] = 1; // LE
        image[6] = 1; // Version
        write_u16_le(&mut image, 16, 2); // ET_EXEC
        write_u16_le(&mut image, 18, 62); // EM_X86_64
        write_u32_le(&mut image, 20, 1);
        write_u64_le(&mut image, 24, USER_SPACE_START); // entry
        write_u64_le(&mut image, 32, ehdr_size as u64); // phoff
        write_u16_le(&mut image, 52, ehdr_size as u16);
        write_u16_le(&mut image, 54, phdr_size as u16);
        write_u16_le(&mut image, 56, 1); // 1 phdr

        // Segment 0: RX code
        let ph = ehdr_size;
        write_u32_le(&mut image, ph, 1); // PT_LOAD
        write_u32_le(&mut image, ph + 4, 1 | 4); // PF_X | PF_R
        write_u64_le(&mut image, ph + 8, 0); // offset
        write_u64_le(&mut image, ph + 16, USER_SPACE_START); // vaddr
        write_u64_le(&mut image, ph + 24, USER_SPACE_START); // paddr
        write_u64_le(&mut image, ph + 32, (ehdr_size + phdr_size + 64) as u64); // filesz
        write_u64_le(&mut image, ph + 40, PAGE_SIZE); // memsz (has BSS up to PAGE_SIZE)
        write_u64_le(&mut image, ph + 48, PAGE_SIZE); // align

        // Put test payload bytes after headers
        image[ehdr_size + phdr_size..ehdr_size + phdr_size + 4]
            .copy_from_slice(&[0xaa, 0xbb, 0xcc, 0xdd]);

        image
    }

    #[test]
    fn test_load_elf_image_success() {
        let image = build_test_elf();
        let validated = validate_elf(&image).expect("validation succeeds");
        let mut allocator = MockAllocator::new();
        let mut mapper = MockMapper::new();

        let loaded = load_elf_image(&validated, &image, &mut allocator, &mut mapper)
            .expect("loading succeeds");

        assert_eq!(loaded.entry, USER_SPACE_START);
        assert_eq!(loaded.stack_top, USER_STACK_TOP);
        // 1 segment page + 16 stack pages = 17 pages
        assert_eq!(loaded.pages_mapped, 17);

        // Check code page mapping
        let code_page = mapper.pages.get(&USER_SPACE_START).expect("code mapped");
        assert!(code_page.readable);
        assert!(!code_page.writable);
        assert!(code_page.executable);
        assert_eq!(code_page.data[0..4], [0x7F, b'E', b'L', b'F']);
        assert_eq!(code_page.data[120..124], [0xaa, 0xbb, 0xcc, 0xdd]);
        // BSS portion should be 0
        assert_eq!(code_page.data[500], 0);

        // Check stack page mapping
        let stack_base = mapper
            .pages
            .get(&USER_STACK_BASE)
            .expect("stack base mapped");
        assert!(stack_base.readable);
        assert!(stack_base.writable);
        assert!(!stack_base.executable);

        let stack_last = mapper
            .pages
            .get(&(USER_STACK_TOP - PAGE_SIZE))
            .expect("stack top page mapped");
        assert!(stack_last.readable);
        assert!(stack_last.writable);
        assert!(!stack_last.executable);

        // Verify guard pages are NOT mapped
        assert!(!mapper.pages.contains_key(&(USER_STACK_BASE - PAGE_SIZE)));
        assert!(!mapper.pages.contains_key(&USER_STACK_TOP));
    }

    #[test]
    fn test_load_elf_image_out_of_memory() {
        let image = build_test_elf();
        let validated = validate_elf(&image).expect("validation succeeds");
        let mut allocator = MockAllocator::new();
        allocator.fail_after = 5; // Fail before completing stack allocation
        let mut mapper = MockMapper::new();

        let res = load_elf_image(&validated, &image, &mut allocator, &mut mapper);
        assert_eq!(res, Err(MapError::OutOfMemory));
    }

    #[test]
    fn test_load_elf_image_mapping_failure() {
        let image = build_test_elf();
        let validated = validate_elf(&image).expect("validation succeeds");
        let mut allocator = MockAllocator::new();
        let mut mapper = MockMapper::new();
        mapper.fail_mapping = true;

        let res = load_elf_image(&validated, &image, &mut allocator, &mut mapper);
        assert_eq!(res, Err(MapError::MappingFailed));
    }
}
