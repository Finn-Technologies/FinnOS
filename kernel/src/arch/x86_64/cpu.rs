//! Small, isolated privileged CPU operations.

/// Halt repeatedly after disabling interrupts.
#[allow(unsafe_code)]
pub fn halt_loop() -> ! {
    loop {
        // SAFETY: CLI and HLT are valid in the kernel's ring-0 execution context.
        unsafe {
            core::arch::asm!("cli", "hlt", options(nomem, nostack));
        }
    }
}

/// Halt once. IF must already be set by the caller.
#[allow(unsafe_code)]
pub fn halt_once() {
    // SAFETY: HLT is used only after the interrupt foundation has enabled IF.
    unsafe {
        core::arch::asm!("hlt", options(nomem, nostack));
    }
}

/// Enable maskable interrupts after all interrupt subsystems are ready.
#[allow(unsafe_code)]
pub fn enable_interrupts() {
    // SAFETY: The caller has validated the IDT, PIC, APIC, and timer first.
    unsafe {
        core::arch::asm!("sti", options(nomem, nostack));
    }
}
