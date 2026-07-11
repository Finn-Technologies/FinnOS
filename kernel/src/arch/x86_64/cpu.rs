//! Small, isolated privileged CPU operations.

/// Halt repeatedly after disabling interrupts.
#[allow(unsafe_code)]
pub fn halt_loop() -> ! {
    loop {
        // SAFETY: CLI and HLT are valid in the kernel's ring-0 execution context.
        unsafe {
            core::arch::asm!("cli", "hlt", options(nomem, nostack, preserves_flags));
        }
    }
}
