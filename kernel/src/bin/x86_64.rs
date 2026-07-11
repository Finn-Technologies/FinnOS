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
    qemu::exit(0x10)
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

#[panic_handler]
fn panic(info: &PanicInfo<'_>) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PANIC {}\n", info);
    qemu::exit(0x11)
}
