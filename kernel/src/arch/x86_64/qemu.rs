//! QEMU-only debug-exit support.

#[cfg(feature = "qemu-test-exit")]
#[allow(unsafe_code)]
/// Exit the QEMU debug-exit device with a test status code.
pub fn exit(code: u8) -> ! {
    // SAFETY: Port 0xf4 is reserved for QEMU's isa-debug-exit device in test mode.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") 0xf4u16, in("al") code, options(nomem, nostack, preserves_flags));
    }
    super::cpu::halt_loop()
}

#[cfg(not(feature = "qemu-test-exit"))]
/// Halt locally when no QEMU debug-exit device is enabled.
pub fn exit(_code: u8) -> ! {
    super::cpu::halt_loop()
}
