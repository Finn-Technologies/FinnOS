//! Host-side tests for the UEFI memory-map parser.
#![allow(unsafe_code)]

use finn_boot_protocol::{
    BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_MEMORY_MAP_PRESENT, BootInfo, FramebufferInfo,
    MemoryMapInfo, PhysicalRange, PixelFormat,
};

use super::MemoryMapError;
use super::map::{
    parse_and_classify as parse_and_classify_production,
    parse_and_classify_without_range_containment_for_tests as parse_and_classify_raw,
    validate_table,
};
use super::region::{MemoryRegionKind, UefiMemoryType};
use super::uefi::UEFI_PAGE_SIZE;

use std::vec;
use std::vec::Vec;

fn parse_and_classify(
    info: &BootInfo,
) -> Result<(super::map::RegionTable, super::map::MemoryMapSummary), MemoryMapError> {
    // SAFETY: Every test keeps its backing byte array alive and unchanged for
    // the complete parser call.
    unsafe { parse_and_classify_raw(info) }
}

fn empty_info() -> BootInfo {
    let mut info = BootInfo::empty();
    info.kernel_image = PhysicalRange {
        start: 0x1000,
        byte_len: 0x1000,
    };
    info.boot_info_storage = PhysicalRange {
        start: 0x2000,
        byte_len: 0x1000,
    };
    info
}

fn with_memory_map(mut info: BootInfo, bytes: &[u8], descriptor_size: u64) -> BootInfo {
    info.flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
    info.memory_map = MemoryMapInfo {
        address: bytes.as_ptr() as u64,
        byte_len: bytes.len() as u64,
        descriptor_size,
        descriptor_version: 1,
        reserved: 0,
    };
    info
}

fn encode_descriptor(
    buffer: &mut [u8],
    offset: usize,
    memory_type: u32,
    physical_start: u64,
    page_count: u64,
    attributes: u64,
) {
    buffer[offset..offset + 4].copy_from_slice(&memory_type.to_le_bytes());
    buffer[offset + 8..offset + 16].copy_from_slice(&physical_start.to_le_bytes());
    buffer[offset + 16..offset + 24].copy_from_slice(&0u64.to_le_bytes());
    buffer[offset + 24..offset + 32].copy_from_slice(&page_count.to_le_bytes());
    buffer[offset + 32..offset + 40].copy_from_slice(&attributes.to_le_bytes());
}

fn conventional_descriptor(physical_start: u64, page_count: u64) -> [u8; 40] {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        physical_start,
        page_count,
        0,
    );
    buffer
}

fn contained_production_fixture() -> (Vec<u8>, BootInfo) {
    let mut bytes = vec![0u8; 80];
    encode_descriptor(
        &mut bytes,
        0,
        UefiMemoryType::Conventional as u32,
        0x10_0000,
        32,
        0,
    );
    let map_start = bytes.as_ptr() as u64;
    let descriptor_start = map_start & !(UEFI_PAGE_SIZE - 1);
    let descriptor_end = map_start
        .checked_add(bytes.len() as u64)
        .unwrap()
        .div_ceil(UEFI_PAGE_SIZE)
        * UEFI_PAGE_SIZE;
    encode_descriptor(
        &mut bytes,
        40,
        UefiMemoryType::LoaderData as u32,
        descriptor_start,
        (descriptor_end - descriptor_start) / UEFI_PAGE_SIZE,
        0,
    );
    let mut info = with_memory_map(empty_info(), &bytes, 40);
    info.kernel_image = PhysicalRange {
        start: 0x10_0000,
        byte_len: UEFI_PAGE_SIZE,
    };
    info.boot_info_storage = PhysicalRange {
        start: 0x10_1000,
        byte_len: UEFI_PAGE_SIZE,
    };
    (bytes, info)
}

#[test]
fn production_parser_accepts_fully_contained_protected_ranges() {
    let (bytes, info) = contained_production_fixture();
    // SAFETY: The returned vector owns the unchanged map bytes for this call.
    let (table, summary) = unsafe { parse_and_classify_production(&info) }.unwrap();
    assert!(!bytes.is_empty());
    assert!(validate_table(&table));
    assert_eq!(summary.kernel_bytes, UEFI_PAGE_SIZE);
    assert_eq!(summary.boot_info_bytes, UEFI_PAGE_SIZE);
    assert_eq!(summary.memory_map_storage_bytes, bytes.len() as u64);
}

#[test]
fn production_parser_rejects_protected_range_outside_firmware_map() {
    let (bytes, mut info) = contained_production_fixture();
    info.kernel_image.start = 0x80_0000;
    // SAFETY: The returned vector owns the unchanged map bytes for this call.
    assert!(matches!(
        unsafe { parse_and_classify_production(&info) },
        Err(MemoryMapError::ProtectedRangeOutsideFirmwareMap)
    ));
    assert!(!bytes.is_empty());
}

#[test]
fn production_parser_rejects_overlapping_protected_ranges() {
    let (bytes, mut info) = contained_production_fixture();
    info.boot_info_storage = info.kernel_image;
    // SAFETY: The returned vector owns the unchanged map bytes for this call.
    assert!(matches!(
        unsafe { parse_and_classify_production(&info) },
        Err(MemoryMapError::OverlappingProtectedRanges)
    ));
    assert!(!bytes.is_empty());
}

#[test]
fn production_parser_accepts_framebuffer_outside_firmware_map() {
    let (bytes, mut info) = contained_production_fixture();
    info.flags |= BOOT_FLAG_FRAMEBUFFER_PRESENT;
    info.framebuffer = FramebufferInfo {
        address: 0x8000_0000,
        byte_len: UEFI_PAGE_SIZE,
        width: 1,
        height: 1,
        stride: 1,
        pixel_format: PixelFormat::Rgb,
    };
    // SAFETY: The returned vector owns the unchanged map bytes for this call.
    let (table, summary) = unsafe { parse_and_classify_production(&info) }.unwrap();
    assert!(!bytes.is_empty());
    assert!(validate_table(&table));
    assert_eq!(summary.framebuffer_bytes, UEFI_PAGE_SIZE);
}

#[test]
fn production_parser_rejects_protected_range_spanning_descriptors() {
    let mut bytes = vec![0u8; 120];
    encode_descriptor(
        &mut bytes,
        0,
        UefiMemoryType::Conventional as u32,
        0x10_0000,
        2,
        0,
    );
    encode_descriptor(
        &mut bytes,
        40,
        UefiMemoryType::Conventional as u32,
        0x10_2000,
        4,
        0,
    );
    let map_start = bytes.as_ptr() as u64;
    let descriptor_start = map_start & !(UEFI_PAGE_SIZE - 1);
    let descriptor_end = map_start
        .checked_add(bytes.len() as u64)
        .unwrap()
        .div_ceil(UEFI_PAGE_SIZE)
        * UEFI_PAGE_SIZE;
    encode_descriptor(
        &mut bytes,
        80,
        UefiMemoryType::LoaderData as u32,
        descriptor_start,
        (descriptor_end - descriptor_start) / UEFI_PAGE_SIZE,
        0,
    );
    let mut info = with_memory_map(empty_info(), &bytes, 40);
    info.kernel_image = PhysicalRange {
        start: 0x10_1000,
        byte_len: 2 * UEFI_PAGE_SIZE,
    };
    info.boot_info_storage = PhysicalRange {
        start: 0x10_4000,
        byte_len: UEFI_PAGE_SIZE,
    };
    // SAFETY: `bytes` remains alive and unchanged for the complete call.
    assert_eq!(
        unsafe { parse_and_classify_production(&info) }.err(),
        Some(MemoryMapError::ProtectedRangeOutsideFirmwareMap)
    );
}

#[test]
fn rejects_zero_descriptor_size() {
    let mut info = empty_info();
    info.flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
    info.memory_map = MemoryMapInfo {
        address: 0x1000,
        byte_len: 40,
        descriptor_size: 0,
        descriptor_version: 1,
        reserved: 0,
    };
    assert!(matches!(
        parse_and_classify(&info),
        Err(MemoryMapError::ZeroDescriptorSize)
    ));
}

#[test]
fn rejects_descriptor_too_small() {
    let mut info = empty_info();
    info.flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
    info.memory_map = MemoryMapInfo {
        address: 0x1000,
        byte_len: 39,
        descriptor_size: 39,
        descriptor_version: 1,
        reserved: 0,
    };
    assert!(matches!(
        parse_and_classify(&info),
        Err(MemoryMapError::DescriptorTooSmall)
    ));
}

#[test]
fn rejects_misaligned_map_length() {
    let mut info = empty_info();
    info.flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
    info.memory_map = MemoryMapInfo {
        address: 0x1000,
        byte_len: 41,
        descriptor_size: 40,
        descriptor_version: 1,
        reserved: 0,
    };
    assert!(matches!(
        parse_and_classify(&info),
        Err(MemoryMapError::MisalignedMapLength)
    ));
}

#[test]
fn rejects_null_address() {
    let mut info = empty_info();
    info.flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
    info.memory_map = MemoryMapInfo {
        address: 0,
        byte_len: 40,
        descriptor_size: 40,
        descriptor_version: 1,
        reserved: 0,
    };
    assert!(matches!(
        parse_and_classify(&info),
        Err(MemoryMapError::NullMemoryMapAddress)
    ));
}

#[test]
fn rejects_descriptor_page_count_and_end_overflow() {
    let mut bytes = [0u8; 40];
    encode_descriptor(
        &mut bytes,
        0,
        UefiMemoryType::Conventional as u32,
        0,
        (u64::MAX / UEFI_PAGE_SIZE) + 1,
        0,
    );
    let info = with_memory_map(empty_info(), &bytes, 40);
    assert_eq!(
        parse_and_classify(&info).err(),
        Some(MemoryMapError::PageCountOverflow)
    );

    encode_descriptor(
        &mut bytes,
        0,
        UefiMemoryType::Conventional as u32,
        u64::MAX - UEFI_PAGE_SIZE + 1,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &bytes, 40);
    assert_eq!(
        parse_and_classify(&info).err(),
        Some(MemoryMapError::RegionOverflow)
    );
}

#[test]
fn parses_one_conventional_descriptor() {
    let bytes = conventional_descriptor(0x20_0000, 4);
    let info = with_memory_map(empty_info(), &bytes, 40);
    let (table, summary) = parse_and_classify(&info).unwrap();
    assert_eq!(table.len(), 1);
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Usable);
    assert_eq!(table.as_slice()[0].start, 0x20_0000);
    assert_eq!(table.as_slice()[0].byte_len, 4 * UEFI_PAGE_SIZE);
    assert_eq!(summary.descriptor_count, 1);
    assert_eq!(summary.usable_bytes, 4 * UEFI_PAGE_SIZE);
}

#[test]
fn handles_descriptor_stride_larger_than_fields() {
    let mut bytes = [0u8; 48];
    encode_descriptor(
        &mut bytes,
        0,
        UefiMemoryType::Conventional as u32,
        0x10_0000,
        2,
        0,
    );
    let info = with_memory_map(empty_info(), &bytes, 48);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.len(), 1);
    assert_eq!(table.as_slice()[0].byte_len, 2 * UEFI_PAGE_SIZE);
}

#[test]
fn skips_zero_page_descriptor() {
    let bytes = conventional_descriptor(0x10_0000, 0);
    let info = with_memory_map(empty_info(), &bytes, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert!(table.is_empty());
}

#[test]
fn classifies_reserved_memory() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Reserved as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Reserved);
}

#[test]
fn classifies_runtime_memory() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::RuntimeServicesCode as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::RuntimeServices);
}

#[test]
fn classifies_boot_services_memory() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::BootServicesData as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::BootServices);
}

#[test]
fn classifies_acpi_reclaimable() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::AcpiReclaimable as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::AcpiReclaimable);
}

#[test]
fn classifies_acpi_nonvolatile() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::AcpiNonVolatile as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::AcpiNonVolatile);
}

#[test]
fn classifies_mmio() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::MemoryMappedIo as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::MemoryMappedIo);
}

#[test]
fn classifies_persistent_memory() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::PersistentMemory as u32,
        0x10_0000,
        1,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Persistent);
}

#[test]
fn unknown_type_becomes_unknown_firmware_type() {
    let mut buffer = [0u8; 40];
    encode_descriptor(&mut buffer, 0, 99, 0x10_0000, 1, 0);
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(
        table.as_slice()[0].kind,
        MemoryRegionKind::UnknownFirmwareType
    );
}

#[test]
fn kernel_exclusion_at_beginning() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        16,
        0,
    );
    let mut info = with_memory_map(empty_info(), &buffer, 40);
    info.kernel_image = PhysicalRange {
        start: 0x20_0000,
        byte_len: 0x8_000,
    };
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Kernel);
    assert_eq!(table.as_slice()[0].byte_len, 0x8_000);
    assert_eq!(table.as_slice()[1].kind, MemoryRegionKind::Usable);
    assert_eq!(table.as_slice()[1].start, 0x20_8000);
}

#[test]
fn kernel_exclusion_in_middle() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        16,
        0,
    );
    let mut info = with_memory_map(empty_info(), &buffer, 40);
    info.kernel_image = PhysicalRange {
        start: 0x20_4000,
        byte_len: 0x4_000,
    };
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Usable);
    assert_eq!(table.as_slice()[0].start, 0x20_0000);
    assert_eq!(table.as_slice()[0].byte_len, 0x4_000);
    assert_eq!(table.as_slice()[1].kind, MemoryRegionKind::Kernel);
    assert_eq!(table.as_slice()[1].start, 0x20_4000);
    assert_eq!(table.as_slice()[2].kind, MemoryRegionKind::Usable);
    assert_eq!(table.as_slice()[2].start, 0x20_8000);
}

#[test]
fn kernel_exclusion_at_end() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        16,
        0,
    );
    let mut info = with_memory_map(empty_info(), &buffer, 40);
    info.kernel_image = PhysicalRange {
        start: 0x20_8000,
        byte_len: 0x8_000,
    };
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].kind, MemoryRegionKind::Usable);
    assert_eq!(table.as_slice()[0].byte_len, 0x8_000);
    assert_eq!(table.as_slice()[1].kind, MemoryRegionKind::Kernel);
    assert_eq!(table.as_slice()[1].start, 0x20_8000);
}

#[test]
fn framebuffer_exclusion_inside_conventional() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        16,
        0,
    );
    let mut info = with_memory_map(empty_info(), &buffer, 40);
    info.flags |= BOOT_FLAG_FRAMEBUFFER_PRESENT;
    info.framebuffer = FramebufferInfo {
        address: 0x20_4000,
        byte_len: 0x1_0000,
        width: 1,
        height: 1,
        stride: 1,
        pixel_format: PixelFormat::Rgb,
    };
    let (table, _) = parse_and_classify(&info).unwrap();
    let kinds: Vec<_> = table.as_slice().iter().map(|r| r.kind).collect();
    assert!(kinds.contains(&MemoryRegionKind::Framebuffer));
    assert!(kinds.contains(&MemoryRegionKind::Usable));
}

#[test]
fn adjacent_same_kinds_merge() {
    let mut buffer = [0u8; 80];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        4,
        0,
    );
    encode_descriptor(
        &mut buffer,
        40,
        UefiMemoryType::Conventional as u32,
        0x20_4000,
        4,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.len(), 1);
    assert_eq!(table.as_slice()[0].byte_len, 8 * UEFI_PAGE_SIZE);
}

#[test]
fn unlike_adjacent_regions_do_not_merge() {
    let mut buffer = [0u8; 80];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        4,
        0,
    );
    encode_descriptor(
        &mut buffer,
        40,
        UefiMemoryType::Reserved as u32,
        0x24_0000,
        4,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.len(), 2);
}

#[test]
fn sorted_output_is_normalized() {
    let mut buffer = [0u8; 80];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x30_0000,
        4,
        0,
    );
    encode_descriptor(
        &mut buffer,
        40,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        4,
        0,
    );
    let info = with_memory_map(empty_info(), &buffer, 40);
    let (table, _) = parse_and_classify(&info).unwrap();
    assert_eq!(table.as_slice()[0].start, 0x20_0000);
    assert_eq!(table.as_slice()[1].start, 0x30_0000);
    assert!(validate_table(&table));
}

#[test]
fn summary_totals_are_accurate() {
    let mut buffer = [0u8; 40];
    encode_descriptor(
        &mut buffer,
        0,
        UefiMemoryType::Conventional as u32,
        0x20_0000,
        16,
        0,
    );
    let mut info = with_memory_map(empty_info(), &buffer, 40);
    info.kernel_image = PhysicalRange {
        start: 0x20_0000,
        byte_len: 0x4_000,
    };
    let (_, summary) = parse_and_classify(&info).unwrap();
    assert_eq!(summary.usable_bytes, 12 * UEFI_PAGE_SIZE);
    assert_eq!(summary.kernel_bytes, 0x4_000);
}
