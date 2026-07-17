#![no_std]
#![no_main]
#![allow(unsafe_code)]

use core::panic::PanicInfo;
use finn_boot_protocol::BootInfo;
#[cfg(feature = "qemu-test-exit")]
use finn_kernel::arch::aarch64::qemu;
use finn_kernel::arch::aarch64::serial;

core::arch::global_asm!(
    r#"
    .section .text._start
    .global _start
_start:
    mov x19, x0
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1
    mov x0, x19
    bl kernel_main
1:  wfe
    b 1b
"#
);

/// R3 AAPCS64 entry. The loader passes `BootInfo` in `x0`; R4 will validate
/// and consume the full handoff while adding the architecture-parity kernel.
#[unsafe(no_mangle)]
pub extern "C" fn kernel_main(boot_info: *const BootInfo) -> ! {
    serial::line("FINNOS:KERNEL:ARM64_ENTRY\n");
    if boot_info.is_null() || !boot_info.is_aligned() {
        serial::line("FINNOS:KERNEL:PANIC:INVALID_BOOTINFO_POINTER\n");
        #[cfg(feature = "qemu-test-exit")]
        qemu::failure();
        #[cfg(not(feature = "qemu-test-exit"))]
        halt();
    }
    serial::line("FINNOS:KERNEL:ARM64_SERIAL_READY\n");
    #[cfg(feature = "qemu-test-exit")]
    qemu::success();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    serial::line("FINNOS:KERNEL:PANIC\n");
    #[cfg(feature = "qemu-test-exit")]
    qemu::failure();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[cfg(not(feature = "qemu-test-exit"))]
fn halt() -> ! {
    loop {
        // SAFETY: WFE only waits for an event and does not access memory.
        unsafe { core::arch::asm!("wfe", options(nomem, nostack, preserves_flags)) }
    }
}
