#![cfg_attr(target_os = "uefi", no_std)]
#![deny(missing_docs)]

//! Host-testable ELF64 x86-64 validation used by the `FinnOS` boot manager.

/// ELF validation failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ElfError {
    /// Input is shorter than the ELF header.
    TruncatedHeader,
    /// Magic is not ELF.
    BadMagic,
    /// File is not ELF64.
    WrongClass,
    /// File is not little endian.
    WrongEndian,
    /// ELF version is unsupported.
    WrongVersion,
    /// Machine is not x86-64.
    WrongMachine,
    /// File is not executable.
    WrongType,
    /// Entry point is zero or invalid.
    BadEntry,
    /// No loadable segment exists.
    NoLoadSegments,
    /// A segment has invalid sizing.
    InvalidSegment,
    /// Arithmetic or range validation overflowed.
    Overflow,
    /// Load ranges overlap.
    Overlap,
    /// Entry is not in executable loaded memory.
    EntryOutsideExecutable,
}

/// A validated loadable ELF segment.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LoadSegment {
    /// Physical destination.
    pub address: u64,
    /// File offset.
    pub file_offset: u64,
    /// Bytes copied from file.
    pub file_size: u64,
    /// Bytes present in memory.
    pub memory_size: u64,
    /// Segment flags.
    pub flags: u32,
}

/// Validated ELF metadata.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ValidatedElf {
    /// Entry address.
    pub entry: u64,
    /// Lowest loaded address.
    pub load_start: u64,
    /// Exclusive end of loaded ranges.
    pub load_end: u64,
    /// Number of load segments.
    pub segment_count: usize,
}

const ELF_HEADER: usize = 64;
const PROGRAM_HEADER: usize = 56;
const PT_LOAD: u32 = 1;
const PF_X: u32 = 1;

fn read_u16(data: &[u8], offset: usize) -> Result<u16, ElfError> {
    let end = offset.checked_add(2).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::TruncatedHeader)?;
    Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
}

fn read_u32(data: &[u8], offset: usize) -> Result<u32, ElfError> {
    let end = offset.checked_add(4).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::TruncatedHeader)?;
    Ok(u32::from_le_bytes(
        bytes.try_into().map_err(|_| ElfError::Overflow)?,
    ))
}

fn read_u64(data: &[u8], offset: usize) -> Result<u64, ElfError> {
    let end = offset.checked_add(8).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::TruncatedHeader)?;
    Ok(u64::from_le_bytes(
        bytes.try_into().map_err(|_| ElfError::Overflow)?,
    ))
}

/// Validate an ELF64 x86-64 executable and its loadable segments.
///
/// # Errors
///
/// Returns a structured error for malformed headers, segments, ranges, or entry points.
#[allow(clippy::too_many_lines)]
pub fn validate_elf(data: &[u8]) -> Result<ValidatedElf, ElfError> {
    if data.len() < ELF_HEADER {
        return Err(ElfError::TruncatedHeader);
    }
    if &data[0..4] != b"\x7fELF" {
        return Err(ElfError::BadMagic);
    }
    if data[4] != 2 {
        return Err(ElfError::WrongClass);
    }
    if data[5] != 1 {
        return Err(ElfError::WrongEndian);
    }
    if data[6] != 1 {
        return Err(ElfError::WrongVersion);
    }
    if read_u16(data, 16)? != 2 {
        return Err(ElfError::WrongType);
    }
    if read_u16(data, 18)? != 62 {
        return Err(ElfError::WrongMachine);
    }
    let entry = read_u64(data, 24)?;
    if entry == 0 {
        return Err(ElfError::BadEntry);
    }
    let phoff = usize::try_from(read_u64(data, 32)?).map_err(|_| ElfError::Overflow)?;
    let phentsize = usize::from(read_u16(data, 54)?);
    let phnum = usize::from(read_u16(data, 56)?);
    if phentsize < PROGRAM_HEADER {
        return Err(ElfError::InvalidSegment);
    }
    let table_size = phentsize.checked_mul(phnum).ok_or(ElfError::Overflow)?;
    let table_end = phoff.checked_add(table_size).ok_or(ElfError::Overflow)?;
    if table_end > data.len() {
        return Err(ElfError::TruncatedHeader);
    }
    let mut segments = [LoadSegment {
        address: 0,
        file_offset: 0,
        file_size: 0,
        memory_size: 0,
        flags: 0,
    }; 32];
    let mut count = 0usize;
    let mut entry_is_executable = false;
    for index in 0..phnum {
        let offset = phoff
            .checked_add(index.checked_mul(phentsize).ok_or(ElfError::Overflow)?)
            .ok_or(ElfError::Overflow)?;
        if read_u32(data, offset)? != PT_LOAD {
            continue;
        }
        if count == segments.len() {
            return Err(ElfError::Overflow);
        }
        let flags = read_u32(data, offset + 4)?;
        let file_offset = read_u64(data, offset + 8)?;
        let address = read_u64(data, offset + 24)?;
        let file_size = read_u64(data, offset + 32)?;
        let memory_size = read_u64(data, offset + 40)?;
        let align = read_u64(data, offset + 48)?;
        if memory_size == 0
            || file_size > memory_size
            || (align != 0 && !align.is_power_of_two())
            || (align > 1 && address % align != file_offset % align)
            || address < 0x1000
        {
            return Err(ElfError::InvalidSegment);
        }
        let file_end = file_offset
            .checked_add(file_size)
            .ok_or(ElfError::Overflow)?;
        if file_end > data.len() as u64 {
            return Err(ElfError::InvalidSegment);
        }
        let address_end = address.checked_add(memory_size).ok_or(ElfError::Overflow)?;
        for previous in &segments[..count] {
            let previous_end = previous
                .address
                .checked_add(previous.memory_size)
                .ok_or(ElfError::Overflow)?;
            if address < previous_end && previous.address < address_end {
                return Err(ElfError::Overlap);
            }
        }
        if flags & PF_X != 0 {
            entry_is_executable |= entry >= address && entry < address_end;
        }
        segments[count] = LoadSegment {
            address,
            file_offset,
            file_size,
            memory_size,
            flags,
        };
        count += 1;
    }
    if count == 0 {
        return Err(ElfError::NoLoadSegments);
    }
    if !entry_is_executable {
        return Err(ElfError::EntryOutsideExecutable);
    }
    let load_start = segments[..count]
        .iter()
        .map(|s| s.address)
        .min()
        .ok_or(ElfError::NoLoadSegments)?;
    let mut load_end = 0;
    for segment in &segments[..count] {
        load_end = load_end.max(
            segment
                .address
                .checked_add(segment.memory_size)
                .ok_or(ElfError::Overflow)?,
        );
    }
    Ok(ValidatedElf {
        entry,
        load_start,
        load_end,
        segment_count: count,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Vec<u8> {
        let mut e = vec![0; 64 + 56];
        e[0..4].copy_from_slice(b"\x7fELF");
        e[4] = 2;
        e[5] = 1;
        e[6] = 1;
        e[16..18].copy_from_slice(&2u16.to_le_bytes());
        e[18..20].copy_from_slice(&62u16.to_le_bytes());
        e[24..32].copy_from_slice(&0x1000u64.to_le_bytes());
        e[32..40].copy_from_slice(&64u64.to_le_bytes());
        e[54..56].copy_from_slice(&56u16.to_le_bytes());
        e[56..58].copy_from_slice(&1u16.to_le_bytes());
        e[64..68].copy_from_slice(&1u32.to_le_bytes());
        e[68..72].copy_from_slice(&1u32.to_le_bytes());
        e[72..80].copy_from_slice(&120u64.to_le_bytes());
        e[88..96].copy_from_slice(&0x1000u64.to_le_bytes());
        e[96..104].copy_from_slice(&4u64.to_le_bytes());
        e[104..112].copy_from_slice(&8u64.to_le_bytes());
        e[112..120].copy_from_slice(&1u64.to_le_bytes());
        e.extend_from_slice(&[0xde, 0xad, 0xbe, 0xef]);
        e
    }

    fn add_second_segment(e: &mut Vec<u8>, address: u64) {
        e.resize(184, 0);
        e[56..58].copy_from_slice(&2u16.to_le_bytes());
        e[72..80].copy_from_slice(&176u64.to_le_bytes());

        e[120..124].copy_from_slice(&PT_LOAD.to_le_bytes());
        e[124..128].copy_from_slice(&PF_X.to_le_bytes());
        e[128..136].copy_from_slice(&180u64.to_le_bytes());
        e[144..152].copy_from_slice(&address.to_le_bytes());
        e[152..160].copy_from_slice(&4u64.to_le_bytes());
        e[160..168].copy_from_slice(&8u64.to_le_bytes());
        e[168..176].copy_from_slice(&1u64.to_le_bytes());
    }
    #[test]
    fn accepts_valid_fixture() {
        assert_eq!(validate_elf(&fixture()).unwrap().entry, 0x1000);
    }
    #[test]
    fn rejects_truncated_and_magic() {
        assert_eq!(validate_elf(&[]), Err(ElfError::TruncatedHeader));
        let mut e = fixture();
        e[0] = 0;
        assert_eq!(validate_elf(&e), Err(ElfError::BadMagic));
    }
    #[test]
    fn rejects_wrong_architecture_and_file_range() {
        let mut e = fixture();
        e[18..20].copy_from_slice(&3u16.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::WrongMachine));
        let mut e = fixture();
        e[96..104].copy_from_slice(&99_999u64.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::InvalidSegment));
    }
    #[test]
    fn rejects_no_load_and_entry_range() {
        let mut e = fixture();
        e[64..68].copy_from_slice(&0u32.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::NoLoadSegments));
        let mut e = fixture();
        e[24..32].copy_from_slice(&0x2000u64.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::EntryOutsideExecutable));
    }
    #[test]
    fn rejects_entry_in_gap_between_executable_segments() {
        let mut e = fixture();
        e[24..32].copy_from_slice(&0x2000u64.to_le_bytes());
        add_second_segment(&mut e, 0x3000);

        assert_eq!(validate_elf(&e), Err(ElfError::EntryOutsideExecutable));
    }

    #[test]
    fn rejects_overlapping_load_segments() {
        let mut e = fixture();
        add_second_segment(&mut e, 0x1004);
        assert_eq!(validate_elf(&e), Err(ElfError::Overlap));
    }

    #[test]
    fn rejects_overflowing_header_and_load_ranges() {
        let mut e = fixture();
        e[32..40].copy_from_slice(&u64::MAX.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::Overflow));

        let mut e = fixture();
        e[88..96].copy_from_slice(&(u64::MAX - 3).to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::Overflow));
    }

    #[test]
    fn rejects_invalid_segment_alignment() {
        let mut e = fixture();
        e[112..120].copy_from_slice(&3u64.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::InvalidSegment));

        let mut e = fixture();
        e[112..120].copy_from_slice(&0x1000u64.to_le_bytes());
        assert_eq!(validate_elf(&e), Err(ElfError::InvalidSegment));
    }

    #[test]
    fn deterministic_mutation_corpus_never_panics() {
        let original = fixture();

        for length in 0..=original.len() {
            let input = &original[..length];
            assert_eq!(validate_elf(input), validate_elf(input));
        }

        for index in 0..original.len() {
            let mut input = original.clone();
            input[index] ^= 0xff;
            let first = validate_elf(&input);
            assert_eq!(first, validate_elf(&input));
            if let Ok(validated) = first {
                assert!(validated.load_start < validated.load_end);
                assert!((1..=32).contains(&validated.segment_count));
            }
        }
    }
}
