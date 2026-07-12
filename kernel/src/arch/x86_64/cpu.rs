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

/// Disable maskable interrupts for a bounded diagnostic snapshot.
#[allow(unsafe_code)]
pub fn disable_interrupts() {
    // SAFETY: The caller immediately restores IF after its atomic snapshot.
    unsafe {
        core::arch::asm!("cli", options(nomem, nostack));
    }
}

/// Return whether maskable interrupts are enabled.
#[must_use]
#[allow(unsafe_code)]
pub fn interrupts_enabled() -> bool {
    let flags: u64;
    // SAFETY: PUSHFQ/POP reads flags without changing processor state.
    unsafe {
        core::arch::asm!("pushfq", "pop {}", out(reg) flags, options(nomem, preserves_flags));
    }
    flags & (1 << 9) != 0
}

/// Idle forever with IF left enabled so hardware interrupts can wake the CPU.
#[allow(unsafe_code)]
pub fn interruptible_idle_loop() -> ! {
    loop {
        halt_once();
    }
}
