//! The AAPCS64 cooperative context-switch ABI.
//!
//! A context owns its saved stack pointer. The stack stores, in ascending address
//! order, x19..x28, x29 (FP), and x30 (LR). This matches `finn_context_switch`.

#![allow(clippy::missing_const_for_fn)]
#![allow(unsafe_code)]

/// Saved state for the AAPCS64 cooperative switch routine.
#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskContext {
    /// Saved stack pointer.
    pub sp: u64,
}

/// Bytes in the initial context frame (12 64-bit words: x19..x28, FP, LR).
pub const INITIAL_FRAME_SIZE: usize = 96;

/// Errors from initial context-frame construction.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContextError {
    /// Stack bounds do not contain a full aligned initial frame.
    StackTooSmall,
    /// An entry point or return address is invalid.
    InvalidInstructionAddress,
}

#[cfg(target_os = "none")]
core::arch::global_asm!(
    r#"
    .section .text.finn_context_switch,"ax"
    .balign 16
    .global finn_context_switch
    .type finn_context_switch, %function
finn_context_switch:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x2, sp
    str x2, [x0]
    mov sp, x1
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret
    .size finn_context_switch, .-finn_context_switch
"#
);

#[cfg(target_os = "none")]
unsafe extern "C" {
    fn finn_context_switch(old_sp: *mut u64, new_sp: u64);
}

/// Switch from saved context pointed to by `old_sp` to `new_sp`.
///
/// # Safety
///
/// `old_sp` must point to stable task storage and `new_sp` must point to a valid
/// frame. No Rust references or guards may remain live across the call.
pub unsafe fn switch(old_sp: *mut u64, new_sp: u64) {
    #[cfg(target_os = "none")]
    unsafe {
        finn_context_switch(old_sp, new_sp);
    }
    #[cfg(not(target_os = "none"))]
    {
        let _ = (old_sp, new_sp);
    }
}

/// Build an initial task context at the top of a mapped stack.
///
/// # Errors
///
/// Returns an error if the stack cannot fit the initial frame or an address is invalid.
pub fn initialize_context(
    stack_start: u64,
    stack_end: u64,
    trampoline: u64,
    fatal_return: u64,
) -> Result<TaskContext, ContextError> {
    if trampoline == 0 || fatal_return == 0 || !trampoline.is_multiple_of(4) {
        return Err(ContextError::InvalidInstructionAddress);
    }
    let frame_start = stack_end
        .checked_sub(INITIAL_FRAME_SIZE as u64)
        .ok_or(ContextError::StackTooSmall)?;
    if frame_start < stack_start || !frame_start.is_multiple_of(16) {
        return Err(ContextError::StackTooSmall);
    }

    #[cfg(target_os = "none")]
    unsafe {
        let frame = frame_start as *mut u64;
        for i in 0..10 {
            core::ptr::write(frame.add(i), 0); // x19..x28
        }
        core::ptr::write(frame.add(10), 0); // x29 (FP)
        core::ptr::write(frame.add(11), trampoline); // x30 (LR)
    }

    Ok(TaskContext { sp: frame_start })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn initial_frame_size_is_16_byte_aligned() {
        assert_eq!(INITIAL_FRAME_SIZE % 16, 0);
        assert_eq!(INITIAL_FRAME_SIZE, 96);
    }

    #[test]
    fn initialize_context_bounds_checks() {
        let res = initialize_context(0x1000, 0x1020, 0x4020_0000, 0x4020_0004);
        assert_eq!(res, Err(ContextError::StackTooSmall));

        let res_aligned = initialize_context(0x1000, 0x2000, 0x4020_0000, 0x4020_0004);
        assert!(res_aligned.is_ok());
        assert_eq!(res_aligned.unwrap().sp, 0x2000 - 96);
    }
}
