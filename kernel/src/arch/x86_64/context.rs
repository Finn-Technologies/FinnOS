//! The narrow x86-64 cooperative context-switch ABI.
//!
//! A context owns only its saved stack pointer.  The stack stores, in ascending
//! address order, `r15`, `r14`, `r13`, `r12`, `rbx`, `rbp`, then the return
//! address consumed by `ret`.  This matches `finn_context_switch` exactly.
#![allow(unsafe_code)]

use super::paging::is_canonical;

/// Saved state for the x86-64 cooperative switch routine.
#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskContext {
    /// Stack pointer at the first saved `r15` value.
    pub rsp: u64,
}

/// Bytes in the synthetic first-run frame, including its fatal return sentinel.
pub const INITIAL_FRAME_SIZE: usize = 64;

/// Errors from initial context-frame construction.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContextError {
    /// Stack bounds do not contain a full aligned initial frame.
    StackTooSmall,
    /// A trampoline address is not canonical.
    InvalidInstructionAddress,
}

core::arch::global_asm!(
    r#"
    .section .text.finn_context_switch,"ax",@progbits
    .global finn_context_switch
    .type finn_context_switch,@function
finn_context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
    .size finn_context_switch, .-finn_context_switch
"#
);

#[allow(unsafe_code)]
unsafe extern "sysv64" {
    fn finn_context_switch(old_rsp: *mut u64, new_rsp: u64);
}

// Retain the assembly symbol in freestanding images even before the scheduler
// integration calls it; this permits an object-code audit of the ABI.
#[used]
static CONTEXT_SWITCH_SYMBOL: unsafe extern "sysv64" fn(*mut u64, u64) = finn_context_switch;

/// Switches from the saved context pointed to by `old_rsp` to `new_rsp`.
///
/// # Safety
///
/// `old_rsp` must point to stable task-table storage and `new_rsp` must name a
/// valid frame matching this module's documented layout. No Rust references or
/// guards may remain live across the call.
#[allow(unsafe_code)]
pub unsafe fn switch(old_rsp: *mut u64, new_rsp: u64) {
    // SAFETY: upheld by this function's caller contract; assembly has the same ABI.
    unsafe { finn_context_switch(old_rsp, new_rsp) };
}

/// Builds an initial task context at the top of a mapped stack.
///
/// # Errors
///
/// Returns an error if a full frame cannot fit or either executable address is
/// noncanonical.
#[allow(unsafe_code)]
pub fn initialize_context(
    stack_start: u64,
    stack_end: u64,
    trampoline: u64,
    fatal_return: u64,
) -> Result<TaskContext, ContextError> {
    if !is_canonical(trampoline) || !is_canonical(fatal_return) {
        return Err(ContextError::InvalidInstructionAddress);
    }
    let frame_start = stack_end
        .checked_sub(INITIAL_FRAME_SIZE as u64)
        .ok_or(ContextError::StackTooSmall)?;
    if frame_start < stack_start || !frame_start.is_multiple_of(16) {
        return Err(ContextError::StackTooSmall);
    }
    // SAFETY: the caller supplies an exclusively owned writable mapped stack;
    // the checked bounds place all eight words within it, and their order matches
    // `finn_context_switch`'s six pops followed by `ret`.
    unsafe {
        let frame = frame_start as *mut u64;
        core::ptr::write(frame.add(0), 0);
        core::ptr::write(frame.add(1), 0);
        core::ptr::write(frame.add(2), 0);
        core::ptr::write(frame.add(3), 0);
        core::ptr::write(frame.add(4), 0);
        core::ptr::write(frame.add(5), 0);
        core::ptr::write(frame.add(6), trampoline);
        core::ptr::write(frame.add(7), fatal_return);
    }
    Ok(TaskContext { rsp: frame_start })
}
