//! `AArch64` syscall and user mode transition.

#![allow(unsafe_code)]

/// Enter user mode (EL0) via eret.
///
/// # Safety
///
/// `entry_pc` and `user_sp` must name valid, mapped user memory.
pub unsafe fn enter_user_mode(entry_pc: u64, user_sp: u64) -> ! {
    #[cfg(target_os = "none")]
    unsafe {
        let spsr: u64 = 0x3c0; // EL0t, 64-bit mode, masked DAIF
        core::arch::asm!(
            "msr sp_el0, {sp}",
            "msr elr_el1, {pc}",
            "msr spsr_el1, {spsr}",
            "isb",
            "eret",
            sp = in(reg) user_sp,
            pc = in(reg) entry_pc,
            spsr = in(reg) spsr,
            options(noreturn)
        );
    }

    #[cfg(not(target_os = "none"))]
    {
        let _ = (entry_pc, user_sp);
        loop {
            core::hint::spin_loop();
        }
    }
}
