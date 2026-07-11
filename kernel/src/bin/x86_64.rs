#![no_std]
#![no_main]
#![allow(unsafe_code)]
#![allow(unreachable_code)]

use core::panic::PanicInfo;
use finn_boot_protocol::{BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_MEMORY_MAP_PRESENT, BootInfo};
use finn_kernel::{
    arch::x86_64::qemu,
    boot_validation::validate_pointer,
    framebuffer::{encode_pixel, pixel_offset},
    memory::{EarlyPhysicalPageAllocator, parse_and_classify},
};

core::arch::global_asm!(
    r#"
    .section .text._start
    .global _start
_start:
    cli
    mov r12, rdi
    lea rsp, [rip + __stack_top]
    and rsp, -16
    sub rsp, 8
    mov rdi, r12
    call kernel_main
    cli
1:  hlt
    jmp 1b
"#
);

#[unsafe(no_mangle)]
pub extern "sysv64" fn kernel_main(pointer: *const BootInfo) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:ENTRY\n");
    #[cfg(feature = "qemu-test-panic")]
    {
        let _ = pointer;
        panic!("controlled test panic");
    }

    // SAFETY: Called once on the BSP with interrupts disabled; the early stack is valid.
    unsafe {
        finn_kernel::arch::x86_64::exceptions::init_exception_foundation(current_stack_top());
    }

    let info = match validate_pointer(pointer) {
        Ok(info) => info,
        Err(_) => {
            finn_kernel::serial_log!("FINNOS:KERNEL:BOOTINFO_ERROR\n");
            failure();
        }
    };
    finn_kernel::serial_log!("FINNOS:KERNEL:BOOTINFO_OK version={}\n", info.version);
    if info.flags & BOOT_FLAG_MEMORY_MAP_PRESENT != 0 {
        finn_kernel::serial_log!(
            "FINNOS:KERNEL:MEMORY_MAP_OK address={:#x} length={} descriptor={}\n",
            info.memory_map.address,
            info.memory_map.byte_len,
            info.memory_map.descriptor_size
        );
        match parse_and_classify(info) {
            Ok((table, summary)) => {
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_PARSED\n");
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED\n");
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:DESCRIPTORS={}\n",
                    summary.descriptor_count
                );
                finn_kernel::serial_log!("FINNOS:MEMORY:REGIONS={}\n", summary.region_count);
                finn_kernel::serial_log!("FINNOS:MEMORY:USABLE_BYTES={}\n", summary.usable_bytes);
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:RESERVED_BYTES={}\n",
                    summary.reserved_bytes
                );
                finn_kernel::serial_log!("FINNOS:MEMORY:KERNEL_BYTES={}\n", summary.kernel_bytes);
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:FRAMEBUFFER_BYTES={}\n",
                    summary.framebuffer_bytes
                );
                #[cfg(feature = "qemu-test-memory-map")]
                {
                    if summary.usable_bytes == 0 {
                        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_USABLE\n");
                        failure();
                    }
                    if summary.kernel_bytes == 0 {
                        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_KERNEL\n");
                        failure();
                    }
                    if summary.framebuffer_bytes == 0 {
                        finn_kernel::serial_log!(
                            "FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_FRAMEBUFFER\n"
                        );
                        failure();
                    }
                }
                #[allow(unused_mut)]
                let mut allocator = match EarlyPhysicalPageAllocator::from_memory_regions(&table) {
                    Ok(allocator) => allocator,
                    Err(error) => {
                        finn_kernel::serial_log!(
                            "FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:{:?}\n",
                            error
                        );
                        failure();
                    }
                };
                if allocator.check_invariants().is_err() || allocator.free_pages() == 0 {
                    finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:INVALID_STATE\n");
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_READY\n");
                finn_kernel::serial_log!("FINNOS:MEMORY:TOTAL_PAGES={}\n", allocator.total_pages());
                finn_kernel::serial_log!("FINNOS:MEMORY:FREE_PAGES={}\n", allocator.free_pages());
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:ALLOCATED_PAGES={}\n",
                    allocator.allocated_pages()
                );
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:MANAGED_EXTENTS={}\n",
                    allocator.managed_extent_count()
                );
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:FREE_EXTENTS={}\n",
                    allocator.free_extent_count()
                );
                #[cfg(feature = "qemu-test-page-allocator")]
                run_page_allocator_test(&mut allocator);
            }
            Err(error) => {
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:{:?}\n", error);
                failure();
            }
        }
    } else {
        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_OK absent\n");
    }
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        draw(info);
        finn_kernel::serial_log!(
            "FINNOS:KERNEL:FRAMEBUFFER_OK address={:#x} width={} height={} stride={}\n",
            info.framebuffer.address,
            info.framebuffer.width,
            info.framebuffer.height,
            info.framebuffer.stride
        );
    } else {
        finn_kernel::serial_log!("FINNOS:KERNEL:FRAMEBUFFER_ERROR\n");
        failure();
    }
    finn_kernel::serial_log!("FINNOS:KERNEL:FIRST_BOOT_COMPLETE\n");

    #[cfg(feature = "qemu-test-exceptions")]
    {
        // SAFETY: The exception foundation is initialized and the IDT is loaded.
        unsafe {
            finn_kernel::arch::x86_64::exceptions::run_exception_tests();
        }
    }

    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-page-allocator")]
fn run_page_allocator_test(allocator: &mut EarlyPhysicalPageAllocator) -> ! {
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:BEGIN\n");
    let initial = allocator.free_pages();
    let first = allocator
        .allocate_page()
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert!(
        first
            .start_address()
            .is_multiple_of(finn_kernel::memory::PAGE_SIZE)
    );
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:SINGLE_ALLOC_OK\n");
    let contiguous = allocator
        .allocate_contiguous(4)
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert!(contiguous.start_address() != first.start_address());
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:CONTIGUOUS_ALLOC_OK\n");
    allocator
        .deallocate(page_range_from_page(first))
        .unwrap_or_else(|error| page_allocator_failure(error));
    let reused = allocator
        .allocate_page()
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert_eq!(reused, first);
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:REUSE_OK\n");
    allocator
        .deallocate(page_range_from_page(reused))
        .unwrap_or_else(|error| page_allocator_failure(error));
    allocator
        .deallocate(contiguous)
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert_eq!(allocator.free_pages(), initial);
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:FREE_OK\n");
    if allocator.deallocate(page_range_from_page(reused)).is_ok() {
        page_allocator_failure(finn_kernel::memory::PageAllocationError::CorruptAllocatorState);
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:DOUBLE_FREE_REJECTED\n");
    allocator
        .check_invariants()
        .unwrap_or_else(|error| page_allocator_failure(error));
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:INVARIANTS_OK\n");
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:PASS\n");
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-page-allocator")]
fn page_allocator_failure(error: finn_kernel::memory::PageAllocationError) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:{:?}\n", error);
    failure()
}

#[cfg(feature = "qemu-test-page-allocator")]
fn page_range_from_page(page: finn_kernel::memory::PhysicalPage) -> finn_kernel::memory::PageRange {
    finn_kernel::memory::PageRange::new(page.start_address(), 1).expect("validated page")
}

fn draw(info: &BootInfo) {
    let format = info.framebuffer.pixel_format;
    let base = info.framebuffer.address as *mut u8;
    for y in 0..info.framebuffer.height {
        for x in 0..info.framebuffer.width {
            let color = if y < info.framebuffer.height / 5 {
                encode_pixel(format, 35, 75, 115)
            } else {
                encode_pixel(format, 10, 15, 25)
            };
            if let (Some(offset), Some(pixel)) = (
                pixel_offset(
                    x,
                    y,
                    info.framebuffer.width,
                    info.framebuffer.height,
                    info.framebuffer.stride,
                    info.framebuffer.byte_len,
                ),
                color,
            ) {
                // SAFETY: BootInfo validation checked the 32-bit pixel range; each write is volatile within the GOP buffer.
                unsafe {
                    core::ptr::write_volatile(base.add(offset).cast::<u32>(), pixel);
                }
            }
        }
    }
}

fn failure() -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:FAILURE\n");
    qemu::exit(0x11)
}

/// Return the top of the early kernel stack set up by `_start`.
#[allow(unsafe_code)]
fn current_stack_top() -> u64 {
    // SAFETY: `__stack_top` is defined by the linker script and marks the top of the
    // boot-allocated kernel stack.
    unsafe extern "C" {
        static __stack_top: u8;
    }
    unsafe { &__stack_top as *const u8 as u64 }
}

#[panic_handler]
fn panic(info: &PanicInfo<'_>) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PANIC {}\n", info);
    qemu::exit(0x11)
}
