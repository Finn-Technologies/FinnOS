#![no_std]
#![no_main]
#![allow(unsafe_code)]

extern crate alloc;

use alloc::vec::Vec;
use core::mem::size_of;
use core::ptr;

use finn_boot_protocol::{
    BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_MEMORY_MAP_PRESENT, BOOT_FLAG_RSDP_PRESENT, BootInfo,
    FramebufferInfo, MemoryMapInfo, PhysicalRange, PixelFormat,
};
use finn_boot_uefi::{ElfError, validate_elf};
use uefi::Identify;
use uefi::boot::{self, AllocateType, MemoryType, SearchType};
use uefi::cstr16;
use uefi::fs::FileSystem;
use uefi::mem::memory_map::MemoryMap;
use uefi::prelude::*;
use uefi::proto::console::gop::{GraphicsOutput, PixelFormat as UefiPixelFormat};
use uefi::system;
use uefi::table::cfg::{ACPI_GUID, ACPI2_GUID};

const MAX_KERNEL_SIZE: usize = 64 * 1024 * 1024;
const PAGE_SIZE: u64 = 4096;

type KernelEntry = unsafe extern "sysv64" fn(*const BootInfo) -> !;

#[entry]
fn main() -> Status {
    let _ = uefi::helpers::init();
    serial::init();
    serial::line("FINNOS:BOOTLOADER:START\n");

    let kernel = match read_kernel() {
        Ok(kernel) => {
            serial::line("FINNOS:BOOTLOADER:KERNEL_FOUND\n");
            kernel
        }
        Err(category) => return fail(category),
    };
    let validated = match validate_elf(&kernel) {
        Ok(value) => {
            serial::line("FINNOS:BOOTLOADER:KERNEL_VALID\n");
            value
        }
        Err(_) => return fail("INVALID_ELF"),
    };
    let loaded = match load_segments(&kernel) {
        Ok(range) => {
            serial::line("FINNOS:BOOTLOADER:KERNEL_LOADED\n");
            range
        }
        Err(_) => return fail("SEGMENT_ALLOCATION_FAILED"),
    };
    if validated.entry < loaded.start
        || validated.entry >= loaded.start.saturating_add(loaded.byte_len)
    {
        return fail("INVALID_ELF");
    }

    let framebuffer = match framebuffer_info() {
        Ok(info) => {
            serial::line("FINNOS:BOOTLOADER:FRAMEBUFFER_READY\n");
            info
        }
        Err(_) => return fail("FRAMEBUFFER_UNAVAILABLE"),
    };
    let rsdp = find_rsdp();
    let boot_info = match boot::allocate_pages(AllocateType::AnyPages, MemoryType::LOADER_DATA, 1) {
        Ok(page) => page,
        Err(_) => return fail("BOOTINFO_ALLOCATION_FAILED"),
    };
    let info_ptr = boot_info.as_ptr().cast::<BootInfo>();
    // SAFETY: The page was allocated by UEFI as loader data and is retained for kernel entry.
    unsafe {
        ptr::write(
            info_ptr,
            BootInfo {
                magic: finn_boot_protocol::BOOT_INFO_MAGIC,
                version: finn_boot_protocol::BOOT_PROTOCOL_VERSION,
                structure_size: size_of::<BootInfo>() as u32,
                flags: BOOT_FLAG_FRAMEBUFFER_PRESENT,
                memory_map: MemoryMapInfo::default(),
                framebuffer,
                kernel_image: loaded,
                rsdp_address: rsdp,
            },
        )
    };
    // SAFETY: The UEFI helper performs the required final-map retry and returns the map whose key exited boot services.
    let memory_map = unsafe { boot::exit_boot_services(MemoryType::LOADER_DATA) };
    let meta = memory_map.meta();
    // SAFETY: The BootInfo page remains allocated and exclusively owned by this loader until control transfers.
    unsafe {
        (*info_ptr).memory_map = MemoryMapInfo {
            address: memory_map.buffer().as_ptr() as u64,
            byte_len: meta.map_size as u64,
            descriptor_size: meta.desc_size as u64,
            descriptor_version: meta.desc_version,
            reserved: 0,
        };
        (*info_ptr).flags |= BOOT_FLAG_MEMORY_MAP_PRESENT;
        if rsdp != 0 {
            (*info_ptr).flags |= BOOT_FLAG_RSDP_PRESENT;
        }
    }
    serial::line("FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES\n");
    // SAFETY: Validation established an executable, loaded entry address and boot services are exited.
    let entry: KernelEntry = unsafe { core::mem::transmute(validated.entry as usize) };
    // SAFETY: The kernel ABI receives the persistent BootInfo page and never returns.
    unsafe { entry(info_ptr) }
}

fn read_kernel() -> Result<Vec<u8>, &'static str> {
    let fs = boot::get_image_file_system(boot::image_handle()).map_err(|_| "KERNEL_NOT_FOUND")?;
    let mut fs = FileSystem::new(fs);
    let kernel = fs
        .read(cstr16!(r"\EFI\FINNOS\KERNEL.ELF"))
        .map_err(|_| "KERNEL_READ_FAILED")?;
    if kernel.is_empty() || kernel.len() > MAX_KERNEL_SIZE {
        return Err("KERNEL_READ_FAILED");
    }
    Ok(kernel)
}

fn load_segments(kernel: &[u8]) -> Result<PhysicalRange, ElfError> {
    let phoff = usize::try_from(read_u64(kernel, 32)?).map_err(|_| ElfError::Overflow)?;
    let phentsize = usize::from(read_u16(kernel, 54)?);
    let phnum = usize::from(read_u16(kernel, 56)?);
    let mut segments = [Segment {
        address: 0,
        file_size: 0,
        file_offset: 0,
    }; 32];
    let mut count = 0usize;
    let mut lowest = u64::MAX;
    let mut highest = 0u64;
    for index in 0..phnum {
        let offset = phoff
            .checked_add(index.checked_mul(phentsize).ok_or(ElfError::Overflow)?)
            .ok_or(ElfError::Overflow)?;
        if read_u32(kernel, offset)? != 1 {
            continue;
        }
        let address = read_u64(kernel, offset + 24)?;
        let memory_size = read_u64(kernel, offset + 40)?;
        let file_size = read_u64(kernel, offset + 32)?;
        let file_offset = read_u64(kernel, offset + 8)?;
        if memory_size == 0 || file_size > memory_size {
            return Err(ElfError::InvalidSegment);
        }
        let end = address.checked_add(memory_size).ok_or(ElfError::Overflow)?;
        if count == segments.len() {
            return Err(ElfError::Overflow);
        }
        segments[count] = Segment {
            address,
            file_size,
            file_offset,
        };
        count += 1;
        lowest = lowest.min(address);
        highest = highest.max(end);
    }
    if lowest == u64::MAX {
        return Err(ElfError::NoLoadSegments);
    }
    let page_start = lowest & !(PAGE_SIZE - 1);
    let page_end = (highest
        .checked_add(PAGE_SIZE - 1)
        .ok_or(ElfError::Overflow)?)
        & !(PAGE_SIZE - 1);
    let pages =
        usize::try_from((page_end - page_start) / PAGE_SIZE).map_err(|_| ElfError::Overflow)?;
    let allocation = match boot::allocate_pages(
        AllocateType::Address(page_start),
        MemoryType::LOADER_DATA,
        pages,
    ) {
        Ok(allocation) => allocation,
        Err(error) => {
            serial::hex("FINNOS:BOOTLOADER:ALLOC_ADDRESS=", page_start);
            serial::hex("FINNOS:BOOTLOADER:ALLOC_PAGES=", pages as u64);
            serial::hex("FINNOS:BOOTLOADER:ALLOC_STATUS=", error.status().0 as u64);
            return Err(ElfError::InvalidSegment);
        }
    };
    if allocation.as_ptr() as u64 != page_start {
        serial::hex(
            "FINNOS:BOOTLOADER:ALLOC_RETURNED=",
            allocation.as_ptr() as u64,
        );
        serial::hex("FINNOS:BOOTLOADER:ALLOC_EXPECTED=", page_start);
        return Err(ElfError::InvalidSegment);
    }
    // SAFETY: UEFI returned the complete checked page-aligned load range.
    unsafe {
        ptr::write_bytes(
            page_start as *mut u8,
            0,
            pages.checked_mul(4096).ok_or(ElfError::Overflow)?,
        );
    }
    for segment in &segments[..count] {
        let source_end = usize::try_from(
            segment
                .file_offset
                .checked_add(segment.file_size)
                .ok_or(ElfError::Overflow)?,
        )
        .map_err(|_| ElfError::Overflow)?;
        let source_start = usize::try_from(segment.file_offset).map_err(|_| ElfError::Overflow)?;
        if source_end > kernel.len() {
            return Err(ElfError::InvalidSegment);
        }
        // SAFETY: The ELF validator checked the file range and the combined allocation covers every segment.
        unsafe {
            ptr::copy_nonoverlapping(
                kernel.as_ptr().add(source_start),
                segment.address as *mut u8,
                usize::try_from(segment.file_size).map_err(|_| ElfError::Overflow)?,
            );
        }
    }
    Ok(PhysicalRange {
        start: lowest,
        byte_len: highest - lowest,
    })
}

#[derive(Clone, Copy)]
struct Segment {
    address: u64,
    file_size: u64,
    file_offset: u64,
}

fn read_u16(data: &[u8], offset: usize) -> Result<u16, ElfError> {
    let end = offset.checked_add(2).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::InvalidSegment)?;
    Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
}

fn read_u32(data: &[u8], offset: usize) -> Result<u32, ElfError> {
    let end = offset.checked_add(4).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::InvalidSegment)?;
    Ok(u32::from_le_bytes(
        bytes.try_into().map_err(|_| ElfError::Overflow)?,
    ))
}

fn read_u64(data: &[u8], offset: usize) -> Result<u64, ElfError> {
    let end = offset.checked_add(8).ok_or(ElfError::Overflow)?;
    let bytes = data.get(offset..end).ok_or(ElfError::InvalidSegment)?;
    Ok(u64::from_le_bytes(
        bytes.try_into().map_err(|_| ElfError::Overflow)?,
    ))
}

fn framebuffer_info() -> Result<FramebufferInfo, ()> {
    let handles = boot::locate_handle_buffer(SearchType::ByProtocol(&GraphicsOutput::GUID))
        .map_err(|_| ())?;
    let handle = *handles.first().ok_or(())?;
    let mut gop = boot::open_protocol_exclusive::<GraphicsOutput>(handle).map_err(|_| ())?;
    let mode = gop.current_mode_info();
    let (width, height) = mode.resolution();
    let format = match mode.pixel_format() {
        UefiPixelFormat::Rgb => PixelFormat::Rgb,
        UefiPixelFormat::Bgr => PixelFormat::Bgr,
        _ => return Err(()),
    };
    let mut buffer = gop.frame_buffer();
    let address = buffer.as_mut_ptr() as u64;
    let byte_len = buffer.size() as u64;
    let width = u32::try_from(width).map_err(|_| ())?;
    let height = u32::try_from(height).map_err(|_| ())?;
    let stride = u32::try_from(mode.stride()).map_err(|_| ())?;
    let needed = u64::from(stride)
        .checked_mul(u64::from(height))
        .and_then(|v| v.checked_mul(4))
        .ok_or(())?;
    if address == 0 || width == 0 || height == 0 || stride < width || needed > byte_len {
        return Err(());
    }
    Ok(FramebufferInfo {
        address,
        byte_len,
        width,
        height,
        stride,
        pixel_format: format,
    })
}

fn find_rsdp() -> u64 {
    system::with_config_table(|tables| {
        tables
            .iter()
            .find(|table| table.guid == ACPI2_GUID)
            .or_else(|| tables.iter().find(|table| table.guid == ACPI_GUID))
            .map_or(0, |table| table.address as u64)
    })
}

fn fail(category: &str) -> Status {
    serial::line("FINNOS:BOOTLOADER:ERROR:");
    serial::line(category);
    serial::line("\n");
    Status::ABORTED
}

mod serial;
