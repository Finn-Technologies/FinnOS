//! Validation policy for user-space `ELF` binaries.
//!
//! This module is intentionally host-testable and performs no address-space
//! wiring. It validates structure, user-range containment, page congruence,
//! `W^X` policy, overlap, and entry-point containment. Mapping the validated
//! image and spawning a process are deferred to a later step.

use crate::syscall::{USER_SPACE_LIMIT, USER_SPACE_START};

/// Maximum number of `PT_LOAD` segments accepted in one image.
pub const MAX_LOAD_SEGMENTS: usize = 8;

/// Page size used for `vaddr`/`file_offset` congruence checks.
pub const PAGE_SIZE: u64 = 4096;

/// Size of a 64-bit `ELF` header accepted by this validator.
const ELF_HEADER_SIZE: usize = 64;

/// Size of a 64-bit program header.
const PROGRAM_HEADER_SIZE: usize = 56;

/// Program header type for loadable segments.
const PT_LOAD: u32 = 1;
/// Program header type for dynamic linking information (unsupported).
const PT_DYNAMIC: u32 = 2;
/// Program header type for interpreter paths (unsupported).
const PT_INTERP: u32 = 3;

/// Segment flag for executable permission.
const PF_X: u32 = 1;
/// Segment flag for writable permission.
const PF_W: u32 = 2;
/// Segment flag for readable permission.
const PF_R: u32 = 4;
/// Mask of all defined segment permission bits.
const PF_MASK: u32 = PF_X | PF_W | PF_R;

/// `ELF` type for executable files.
const ET_EXEC: u16 = 2;
/// `ELF` type for shared objects / position-independent executables.
const ET_DYN: u16 = 3;

/// Machine identifier for `x86-64`.
const EM_X86_64: u16 = 62;
/// Machine identifier for `AArch64`.
const EM_AARCH64: u16 = 183;

/// Failures reported while validating a user `ELF` binary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ElfError {
    /// `ELF` magic bytes (`0x7F E L F`) do not match.
    BadMagic,
    /// `EI_CLASS` is not 64-bit (`ELFCLASS64`).
    WrongClass,
    /// `EI_DATA` is not little-endian (`ELFDATA2LSB`).
    WrongEndian,
    /// `EI_VERSION` or `e_version` is not current (1).
    WrongVersion,
    /// `e_type` is neither `ET_EXEC` nor `ET_DYN`.
    UnsupportedType,
    /// `e_machine` is neither `EM_X86_64` nor `EM_AARCH64`.
    UnsupportedMachine,
    /// Input is truncated or program-header table bounds are invalid.
    TruncatedHeaders,
    /// More than [`MAX_LOAD_SEGMENTS`] loadable segments were found.
    TooManySegments,
    /// `vaddr`/`file_offset` page congruence or `p_align` is invalid.
    BadSegmentAlignment,
    /// Segment range, sizing, or file backing is outside allowed bounds.
    SegmentOutOfRange,
    /// Two loadable segments overlap in virtual memory.
    SegmentOverlap,
    /// Segment requests writable and executable permissions together.
    WritableExecutable,
    /// Entry point is not inside an executable segment.
    EntryOutOfRange,
    /// No `PT_LOAD` segments were present.
    NoLoadSegments,
    /// Unknown segment flags or unsupported interpreter/dynamic entries.
    UnsupportedFlags,
}

/// Parsed `ELF` header fields retained for validation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ElfHeader {
    /// Entry-point virtual address (`e_entry`).
    pub entry: u64,
    /// Program-header table file offset (`e_phoff`).
    pub phoff: u64,
    /// Number of program-header entries (`e_phnum`).
    pub phnum: u16,
}

/// A validated loadable segment.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LoadSegment {
    /// Virtual address the segment is linked to run at.
    pub vaddr: u64,
    /// Number of bytes backed by the file.
    pub filesz: u64,
    /// Number of bytes occupied in memory (`memsz >= filesz`).
    pub memsz: u64,
    /// Raw permission flags (`R = 4`, `W = 2`, `X = 1`).
    pub flags: u32,
    /// File offset backing this segment.
    pub file_offset: u64,
}

impl LoadSegment {
    /// Flag bit for readable segments.
    pub const FLAG_READ: u32 = PF_R;
    /// Flag bit for writable segments.
    pub const FLAG_WRITE: u32 = PF_W;
    /// Flag bit for executable segments.
    pub const FLAG_EXECUTE: u32 = PF_X;

    /// Returns `true` when the segment is readable.
    #[must_use]
    pub const fn is_readable(self) -> bool {
        self.flags & PF_R != 0
    }

    /// Returns `true` when the segment is writable.
    #[must_use]
    pub const fn is_writable(self) -> bool {
        self.flags & PF_W != 0
    }

    /// Returns `true` when the segment is executable.
    #[must_use]
    pub const fn is_executable(self) -> bool {
        self.flags & PF_X != 0
    }
}

/// A fully validated user `ELF` image.
///
/// Address-space mapping is deliberately not performed here; consumers map
/// [`LoadSegment`] entries in a later step.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ValidatedImage {
    /// Validated entry-point virtual address.
    pub entry: u64,
    /// Fixed-size backing store for loadable segments.
    pub segments: [Option<LoadSegment>; MAX_LOAD_SEGMENTS],
    /// Number of initialized entries in [`Self::segments`].
    pub count: usize,
}

impl ValidatedImage {
    /// Returns the number of validated loadable segments.
    #[must_use]
    pub const fn len(self) -> usize {
        self.count
    }

    /// Returns `true` when the image contains no loadable segments.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.count == 0
    }

    /// Returns the segment at `index`, or `None` when out of range.
    #[must_use]
    pub const fn get(self, index: usize) -> Option<LoadSegment> {
        if index < self.count && index < MAX_LOAD_SEGMENTS {
            self.segments[index]
        } else {
            None
        }
    }
}

/// Read a little-endian `u16` at `offset`.
///
/// # Errors
///
/// Returns [`ElfError::TruncatedHeaders`] when the bytes are unavailable.
fn read_u16_le(bytes: &[u8], offset: usize) -> Result<u16, ElfError> {
    let end = offset.checked_add(2).ok_or(ElfError::TruncatedHeaders)?;
    let window = bytes.get(offset..end).ok_or(ElfError::TruncatedHeaders)?;
    let raw = <[u8; 2]>::try_from(window).map_err(|_| ElfError::TruncatedHeaders)?;
    Ok(u16::from_le_bytes(raw))
}

/// Read a little-endian `u32` at `offset`.
///
/// # Errors
///
/// Returns [`ElfError::TruncatedHeaders`] when the bytes are unavailable.
fn read_u32_le(bytes: &[u8], offset: usize) -> Result<u32, ElfError> {
    let end = offset.checked_add(4).ok_or(ElfError::TruncatedHeaders)?;
    let window = bytes.get(offset..end).ok_or(ElfError::TruncatedHeaders)?;
    let raw = <[u8; 4]>::try_from(window).map_err(|_| ElfError::TruncatedHeaders)?;
    Ok(u32::from_le_bytes(raw))
}

/// Read a little-endian `u64` at `offset`.
///
/// # Errors
///
/// Returns [`ElfError::TruncatedHeaders`] when the bytes are unavailable.
fn read_u64_le(bytes: &[u8], offset: usize) -> Result<u64, ElfError> {
    let end = offset.checked_add(8).ok_or(ElfError::TruncatedHeaders)?;
    let window = bytes.get(offset..end).ok_or(ElfError::TruncatedHeaders)?;
    let raw = <[u8; 8]>::try_from(window).map_err(|_| ElfError::TruncatedHeaders)?;
    Ok(u64::from_le_bytes(raw))
}

/// Parse and range-check the `ELF` header and program-header table bounds.
///
/// # Errors
///
/// Returns a specific [`ElfError`] for malformed identification bytes,
/// unsupported types/machines, or truncated header tables.
fn parse_header(bytes: &[u8]) -> Result<ElfHeader, ElfError> {
    if bytes.len() < ELF_HEADER_SIZE {
        return Err(ElfError::TruncatedHeaders);
    }
    let magic = bytes.get(0..4).ok_or(ElfError::TruncatedHeaders)?;
    if magic != [0x7F, b'E', b'L', b'F'] {
        return Err(ElfError::BadMagic);
    }
    let class = bytes.get(4).copied().ok_or(ElfError::TruncatedHeaders)?;
    if class != 2 {
        return Err(ElfError::WrongClass);
    }
    let data = bytes.get(5).copied().ok_or(ElfError::TruncatedHeaders)?;
    if data != 1 {
        return Err(ElfError::WrongEndian);
    }
    let ident_version = bytes.get(6).copied().ok_or(ElfError::TruncatedHeaders)?;
    if ident_version != 1 {
        return Err(ElfError::WrongVersion);
    }
    let file_type = read_u16_le(bytes, 16)?;
    if file_type != ET_EXEC && file_type != ET_DYN {
        return Err(ElfError::UnsupportedType);
    }
    let machine = read_u16_le(bytes, 18)?;
    if machine != EM_X86_64 && machine != EM_AARCH64 {
        return Err(ElfError::UnsupportedMachine);
    }
    let version = read_u32_le(bytes, 20)?;
    if version != 1 {
        return Err(ElfError::WrongVersion);
    }
    let entry = read_u64_le(bytes, 24)?;
    let phoff = read_u64_le(bytes, 32)?;
    let phentsize = read_u16_le(bytes, 54)?;
    let phnum = read_u16_le(bytes, 56)?;
    if phnum > 0 && usize::from(phentsize) < PROGRAM_HEADER_SIZE {
        return Err(ElfError::TruncatedHeaders);
    }
    let table_size = u64::from(phentsize)
        .checked_mul(u64::from(phnum))
        .ok_or(ElfError::TruncatedHeaders)?;
    let table_end = phoff
        .checked_add(table_size)
        .ok_or(ElfError::TruncatedHeaders)?;
    let file_len = u64::try_from(bytes.len()).map_err(|_| ElfError::TruncatedHeaders)?;
    if table_end > file_len {
        return Err(ElfError::TruncatedHeaders);
    }
    Ok(ElfHeader {
        entry,
        phoff,
        phnum,
    })
}

/// Check user-range containment and file backing for one segment.
///
/// # Errors
///
/// Returns [`ElfError::SegmentOutOfRange`] for sizing, wrap, range, or
/// backing violations, [`ElfError::WritableExecutable`] for `W+X` segments,
/// [`ElfError::UnsupportedFlags`] for unknown flag bits, and
/// [`ElfError::BadSegmentAlignment`] for congruence/`p_align` violations.
fn check_segment_fields(
    vaddr: u64,
    file_offset: u64,
    filesz: u64,
    memsz: u64,
    flags: u32,
    align: u64,
    file_len: u64,
) -> Result<(), ElfError> {
    if flags & !PF_MASK != 0 {
        return Err(ElfError::UnsupportedFlags);
    }
    if flags & PF_W != 0 && flags & PF_X != 0 {
        return Err(ElfError::WritableExecutable);
    }
    if memsz == 0 {
        return Err(ElfError::SegmentOutOfRange);
    }
    if filesz > memsz {
        return Err(ElfError::SegmentOutOfRange);
    }
    if !(USER_SPACE_START..USER_SPACE_LIMIT).contains(&vaddr) {
        return Err(ElfError::SegmentOutOfRange);
    }
    let vaddr_end = vaddr
        .checked_add(memsz)
        .ok_or(ElfError::SegmentOutOfRange)?;
    if vaddr_end > USER_SPACE_LIMIT {
        return Err(ElfError::SegmentOutOfRange);
    }
    let file_end = file_offset
        .checked_add(filesz)
        .ok_or(ElfError::SegmentOutOfRange)?;
    if file_end > file_len {
        return Err(ElfError::SegmentOutOfRange);
    }
    if align != 0 && align != 1 && !align.is_power_of_two() {
        return Err(ElfError::BadSegmentAlignment);
    }
    if align > 1 && vaddr % align != file_offset % align {
        return Err(ElfError::BadSegmentAlignment);
    }
    if vaddr % PAGE_SIZE != file_offset % PAGE_SIZE {
        return Err(ElfError::BadSegmentAlignment);
    }
    Ok(())
}

/// Returns `true` when `candidate` overlaps any previously accepted segment.
fn overlaps_previous(
    candidate: &LoadSegment,
    accepted: &[Option<LoadSegment>],
    count: usize,
) -> bool {
    let Some(candidate_end) = candidate.vaddr.checked_add(candidate.memsz) else {
        return true;
    };
    let mut index = 0;
    while index < count {
        if let Some(previous) = accepted.get(index).and_then(|slot| *slot) {
            let Some(previous_end) = previous.vaddr.checked_add(previous.memsz) else {
                return true;
            };
            if candidate.vaddr < previous_end && previous.vaddr < candidate_end {
                return true;
            }
        }
        index += 1;
    }
    false
}

/// Returns `true` when `entry` lies inside an executable accepted segment.
fn entry_in_executable(entry: u64, accepted: &[Option<LoadSegment>], count: usize) -> bool {
    let mut index = 0;
    while index < count {
        if let Some(segment) = accepted.get(index).and_then(|slot| *slot)
            && segment.is_executable()
            && let Some(end) = segment.vaddr.checked_add(segment.memsz)
            && entry >= segment.vaddr
            && entry < end
        {
            return true;
        }
        index += 1;
    }
    false
}

/// Validate a user `ELF` binary without mapping it.
///
/// The validator accepts 64-bit little-endian `ET_EXEC`/`ET_DYN` images for
/// `EM_X86_64`/`EM_AARCH64` with up to [`MAX_LOAD_SEGMENTS`] `PT_LOAD`
/// segments. Interpreter (`PT_INTERP`) and dynamic (`PT_DYNAMIC`) entries are
/// rejected because relocations are not supported yet.
///
/// # Errors
///
/// Returns a specific [`ElfError`] for malformed headers, unsupported types,
/// out-of-range segments, `W^X` violations, overlaps, or an entry point that
/// is not inside an executable segment.
pub fn validate_elf(bytes: &[u8]) -> Result<ValidatedImage, ElfError> {
    let header = parse_header(bytes)?;
    let file_len = u64::try_from(bytes.len()).map_err(|_| ElfError::TruncatedHeaders)?;
    let phentsize = read_u16_le(bytes, 54)?;
    let stride = usize::from(phentsize);

    let mut segments: [Option<LoadSegment>; MAX_LOAD_SEGMENTS] = [None; MAX_LOAD_SEGMENTS];
    let mut count: usize = 0;
    let mut index: usize = 0;

    while usize::from(header.phnum) > index {
        let scaled = usize::checked_mul(index, stride).ok_or(ElfError::TruncatedHeaders)?;
        let phoff_usize = usize::try_from(header.phoff).map_err(|_| ElfError::TruncatedHeaders)?;
        let phdr_offset = scaled
            .checked_add(phoff_usize)
            .ok_or(ElfError::TruncatedHeaders)?;
        let phdr_end = phdr_offset
            .checked_add(PROGRAM_HEADER_SIZE)
            .ok_or(ElfError::TruncatedHeaders)?;
        if phdr_end > bytes.len() {
            return Err(ElfError::TruncatedHeaders);
        }

        let raw_type = read_u32_le(bytes, phdr_offset)?;
        if raw_type != PT_LOAD {
            if raw_type == PT_INTERP || raw_type == PT_DYNAMIC {
                return Err(ElfError::UnsupportedFlags);
            }
            index += 1;
            continue;
        }
        if count >= MAX_LOAD_SEGMENTS {
            return Err(ElfError::TooManySegments);
        }

        let flags = read_u32_le(
            bytes,
            phdr_offset
                .checked_add(4)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;
        let file_offset = read_u64_le(
            bytes,
            phdr_offset
                .checked_add(8)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;
        let vaddr = read_u64_le(
            bytes,
            phdr_offset
                .checked_add(16)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;
        let filesz = read_u64_le(
            bytes,
            phdr_offset
                .checked_add(32)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;
        let memsz = read_u64_le(
            bytes,
            phdr_offset
                .checked_add(40)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;
        let align = read_u64_le(
            bytes,
            phdr_offset
                .checked_add(48)
                .ok_or(ElfError::TruncatedHeaders)?,
        )?;

        check_segment_fields(vaddr, file_offset, filesz, memsz, flags, align, file_len)?;

        let candidate = LoadSegment {
            vaddr,
            filesz,
            memsz,
            flags,
            file_offset,
        };
        if overlaps_previous(&candidate, &segments, count) {
            return Err(ElfError::SegmentOverlap);
        }
        if segments.get_mut(count).is_none() {
            return Err(ElfError::TooManySegments);
        }
        segments[count] = Some(candidate);
        count += 1;
        index += 1;
    }

    if count == 0 {
        return Err(ElfError::NoLoadSegments);
    }
    if !entry_in_executable(header.entry, &segments, count) {
        return Err(ElfError::EntryOutOfRange);
    }
    Ok(ValidatedImage {
        entry: header.entry,
        segments,
        count,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::syscall::{USER_SPACE_LIMIT, USER_SPACE_START};

    const EHDR_SIZE: usize = 64;
    const PHDR_SIZE: usize = 56;

    fn write_u16_le(target: &mut [u8], offset: usize, value: u16) {
        target[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u32_le(target: &mut [u8], offset: usize, value: u32) {
        target[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u64_le(target: &mut [u8], offset: usize, value: u64) {
        target[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }

    fn minimal_elf_bytes() -> std::vec::Vec<u8> {
        let mut image = std::vec![0u8; EHDR_SIZE + PHDR_SIZE];
        image[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        image[4] = 2;
        image[5] = 1;
        image[6] = 1;
        write_u16_le(&mut image, 16, ET_EXEC);
        write_u16_le(&mut image, 18, EM_X86_64);
        write_u32_le(&mut image, 20, 1);
        write_u64_le(&mut image, 24, USER_SPACE_START);
        write_u64_le(
            &mut image,
            32,
            u64::try_from(EHDR_SIZE).expect("header size fits"),
        );
        write_u16_le(
            &mut image,
            52,
            u16::try_from(EHDR_SIZE).expect("header size fits"),
        );
        write_u16_le(
            &mut image,
            54,
            u16::try_from(PHDR_SIZE).expect("phdr size fits"),
        );
        write_u16_le(&mut image, 56, 1);
        let ph = EHDR_SIZE;
        write_u32_le(&mut image, ph, PT_LOAD);
        write_u32_le(&mut image, ph + 4, PF_R | PF_X);
        write_u64_le(&mut image, ph + 8, 0);
        write_u64_le(&mut image, ph + 16, USER_SPACE_START);
        write_u64_le(&mut image, ph + 24, USER_SPACE_START);
        write_u64_le(
            &mut image,
            ph + 32,
            u64::try_from(EHDR_SIZE + PHDR_SIZE).expect("headers fit"),
        );
        write_u64_le(&mut image, ph + 40, PAGE_SIZE);
        write_u64_le(&mut image, ph + 48, PAGE_SIZE);
        image
    }

    fn with_two_headers(second_vaddr: u64, second_flags: u32) -> std::vec::Vec<u8> {
        let mut image = minimal_elf_bytes();
        image.resize(EHDR_SIZE + 2 * PHDR_SIZE, 0);
        write_u16_le(&mut image, 56, 2);
        // Widen the first segment so an aligned second segment still overlaps.
        let first = EHDR_SIZE;
        write_u64_le(&mut image, first + 8, 0);
        write_u64_le(&mut image, first + 32, 8);
        write_u64_le(&mut image, first + 40, 2 * PAGE_SIZE);
        let second = EHDR_SIZE + PHDR_SIZE;
        write_u32_le(&mut image, second, PT_LOAD);
        write_u32_le(&mut image, second + 4, second_flags);
        write_u64_le(&mut image, second + 8, 0);
        write_u64_le(&mut image, second + 16, second_vaddr);
        write_u64_le(&mut image, second + 24, second_vaddr);
        write_u64_le(&mut image, second + 32, 8);
        write_u64_le(&mut image, second + 40, PAGE_SIZE);
        write_u64_le(&mut image, second + 48, PAGE_SIZE);
        // First segment must also keep page congruence: vaddr 0x1_0000 % 4096 == 0.
        let _ = second_vaddr;
        image
    }

    #[test]
    fn valid_minimal_elf_is_accepted() {
        let image = minimal_elf_bytes();
        let validated = validate_elf(&image).expect("minimal image validates");
        assert_eq!(validated.entry, USER_SPACE_START);
        assert_eq!(validated.count, 1);
        assert_eq!(validated.len(), 1);
        assert!(!validated.is_empty());
        let segment = validated.get(0).expect("first segment");
        assert_eq!(segment.vaddr, USER_SPACE_START);
        assert!(segment.is_executable());
        assert!(segment.is_readable());
        assert!(!segment.is_writable());
    }

    #[test]
    fn valid_dyn_aarch64_is_accepted() {
        let mut image = minimal_elf_bytes();
        write_u16_le(&mut image, 16, ET_DYN);
        write_u16_le(&mut image, 18, EM_AARCH64);
        let validated = validate_elf(&image).expect("dyn aarch64 validates");
        assert_eq!(validated.entry, USER_SPACE_START);
    }

    #[test]
    fn bad_magic_is_rejected() {
        let mut image = minimal_elf_bytes();
        image[0] = 0x00;
        assert_eq!(validate_elf(&image), Err(ElfError::BadMagic));
    }

    #[test]
    fn wrong_class_is_rejected() {
        let mut image = minimal_elf_bytes();
        image[4] = 1;
        assert_eq!(validate_elf(&image), Err(ElfError::WrongClass));
    }

    #[test]
    fn wrong_endian_is_rejected() {
        let mut image = minimal_elf_bytes();
        image[5] = 2;
        assert_eq!(validate_elf(&image), Err(ElfError::WrongEndian));
    }

    #[test]
    fn wrong_version_is_rejected() {
        let mut image = minimal_elf_bytes();
        image[6] = 2;
        assert_eq!(validate_elf(&image), Err(ElfError::WrongVersion));
    }

    #[test]
    fn unsupported_type_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u16_le(&mut image, 16, 1);
        assert_eq!(validate_elf(&image), Err(ElfError::UnsupportedType));
    }

    #[test]
    fn unsupported_machine_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u16_le(&mut image, 18, 3);
        assert_eq!(validate_elf(&image), Err(ElfError::UnsupportedMachine));
    }

    #[test]
    fn writable_executable_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u32_le(&mut image, EHDR_SIZE + 4, PF_R | PF_W | PF_X);
        assert_eq!(validate_elf(&image), Err(ElfError::WritableExecutable));
    }

    #[test]
    fn unsupported_flags_are_rejected() {
        let mut image = minimal_elf_bytes();
        write_u32_le(&mut image, EHDR_SIZE + 4, 0x8);
        assert_eq!(validate_elf(&image), Err(ElfError::UnsupportedFlags));
    }

    #[test]
    fn interpreter_segment_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u32_le(&mut image, EHDR_SIZE, PT_INTERP);
        assert_eq!(validate_elf(&image), Err(ElfError::UnsupportedFlags));
    }

    #[test]
    fn overlapping_segments_are_rejected() {
        let image = with_two_headers(USER_SPACE_START + PAGE_SIZE, PF_R);
        assert_eq!(validate_elf(&image), Err(ElfError::SegmentOverlap));
    }

    #[test]
    fn entry_outside_executable_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u64_le(&mut image, 24, USER_SPACE_START + 0x8000);
        assert_eq!(validate_elf(&image), Err(ElfError::EntryOutOfRange));
    }

    #[test]
    fn entry_in_non_executable_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u32_le(&mut image, EHDR_SIZE + 4, PF_R);
        assert_eq!(validate_elf(&image), Err(ElfError::EntryOutOfRange));
    }

    #[test]
    fn truncated_program_headers_are_rejected() {
        let mut image = minimal_elf_bytes();
        write_u16_le(&mut image, 56, 2);
        assert_eq!(validate_elf(&image), Err(ElfError::TruncatedHeaders));
    }

    #[test]
    fn truncated_file_is_rejected() {
        assert_eq!(validate_elf(&[0u8; 10]), Err(ElfError::TruncatedHeaders));
    }

    #[test]
    fn too_many_segments_are_rejected() {
        let count: usize = MAX_LOAD_SEGMENTS + 1;
        let mut image = std::vec![0u8; EHDR_SIZE + count * PHDR_SIZE];
        image[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
        image[4] = 2;
        image[5] = 1;
        image[6] = 1;
        write_u16_le(&mut image, 16, ET_EXEC);
        write_u16_le(&mut image, 18, EM_X86_64);
        write_u32_le(&mut image, 20, 1);
        write_u64_le(&mut image, 24, USER_SPACE_START);
        write_u64_le(
            &mut image,
            32,
            u64::try_from(EHDR_SIZE).expect("header size fits"),
        );
        write_u16_le(
            &mut image,
            52,
            u16::try_from(EHDR_SIZE).expect("header size fits"),
        );
        write_u16_le(
            &mut image,
            54,
            u16::try_from(PHDR_SIZE).expect("phdr size fits"),
        );
        write_u16_le(&mut image, 56, u16::try_from(count).expect("count fits"));
        for slot in 0..count {
            let ph = EHDR_SIZE + slot * PHDR_SIZE;
            let vaddr = USER_SPACE_START + u64::try_from(slot).expect("slot fits") * PAGE_SIZE;
            write_u32_le(&mut image, ph, PT_LOAD);
            write_u32_le(&mut image, ph + 4, PF_R | PF_X);
            write_u64_le(&mut image, ph + 8, 0);
            write_u64_le(&mut image, ph + 16, vaddr);
            write_u64_le(&mut image, ph + 24, vaddr);
            write_u64_le(&mut image, ph + 32, 8);
            write_u64_le(&mut image, ph + 40, PAGE_SIZE);
            write_u64_le(&mut image, ph + 48, PAGE_SIZE);
        }
        assert_eq!(validate_elf(&image), Err(ElfError::TooManySegments));
    }

    #[test]
    fn filesz_larger_than_memsz_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u64_le(&mut image, EHDR_SIZE + 32, PAGE_SIZE + 1);
        write_u64_le(&mut image, EHDR_SIZE + 40, PAGE_SIZE);
        // Extend backing so the failure is sizing, not file truncation.
        image.resize(usize::try_from(PAGE_SIZE + 1).expect("page size fits"), 0);
        assert_eq!(validate_elf(&image), Err(ElfError::SegmentOutOfRange));
    }

    #[test]
    fn file_backed_segment_out_of_range_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u64_le(&mut image, EHDR_SIZE + 8, 10_000);
        write_u64_le(&mut image, EHDR_SIZE + 32, 16);
        assert_eq!(validate_elf(&image), Err(ElfError::SegmentOutOfRange));
    }

    #[test]
    fn virtual_address_out_of_range_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u64_le(&mut image, EHDR_SIZE + 16, USER_SPACE_LIMIT);
        write_u64_le(&mut image, EHDR_SIZE + 24, USER_SPACE_LIMIT);
        write_u64_le(&mut image, EHDR_SIZE + 8, USER_SPACE_LIMIT % PAGE_SIZE);
        assert_eq!(validate_elf(&image), Err(ElfError::SegmentOutOfRange));
    }

    #[test]
    fn low_virtual_address_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u64_le(&mut image, EHDR_SIZE + 16, 0x1000);
        write_u64_le(&mut image, EHDR_SIZE + 24, 0x1000);
        write_u64_le(&mut image, EHDR_SIZE + 8, 0x1000);
        assert_eq!(validate_elf(&image), Err(ElfError::SegmentOutOfRange));
    }

    #[test]
    fn bad_page_congruence_is_rejected() {
        let mut image = minimal_elf_bytes();
        image.resize(512, 0);
        write_u64_le(&mut image, EHDR_SIZE + 8, 1);
        write_u64_le(&mut image, EHDR_SIZE + 32, 8);
        write_u64_le(&mut image, EHDR_SIZE + 40, PAGE_SIZE);
        assert_eq!(validate_elf(&image), Err(ElfError::BadSegmentAlignment));
    }

    #[test]
    fn no_load_segments_is_rejected() {
        let mut image = minimal_elf_bytes();
        write_u32_le(&mut image, EHDR_SIZE, 0);
        assert_eq!(validate_elf(&image), Err(ElfError::NoLoadSegments));
    }

    #[test]
    fn validated_image_accessors_behave() {
        let image = minimal_elf_bytes();
        let validated = validate_elf(&image).expect("valid");
        assert_eq!(validated.get(0).map(|s| s.vaddr), Some(USER_SPACE_START));
        assert_eq!(validated.get(1), None);
        assert_eq!(validated.get(MAX_LOAD_SEGMENTS), None);
    }
}
