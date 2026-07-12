//! x86-64 external interrupt entry and fixed vector policy.
#![allow(missing_docs)]
#![allow(clippy::all)]

use super::{idt, serial, timer};

/// First legacy PIC vector.
pub const PIC_VECTOR_START: u8 = 0x20;
/// Last legacy PIC vector.
pub const PIC_VECTOR_END: u8 = 0x2f;
/// BSP local APIC timer vector.
pub const TIMER_VECTOR: u8 = 0x40;
/// BSP local APIC spurious vector.
pub const SPURIOUS_VECTOR: u8 = 0xff;
/// Ring-0 interrupt gate with IST zero.
pub const EXTERNAL_GATE_IST: u8 = 0;

/// The common frame built by the external-interrupt stubs.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct InterruptFrame {
    /// Saved registers in assembly order.
    pub rax: u64,
    pub rbx: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rsi: u64,
    pub rdi: u64,
    /// Saved base pointer.
    pub rbp: u64,
    /// Saved extended registers.
    pub r8: u64,
    pub r9: u64,
    pub r10: u64,
    pub r11: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    /// Vector pushed by the stub.
    pub vector: u64,
    /// Synthetic zero error code.
    pub error_code: u64,
    /// CPU-pushed return frame.
    pub rip: u64,
    pub cs: u64,
    pub rflags: u64,
}

mod asm_stubs {
    #![allow(unsafe_code)]
    core::arch::global_asm!(
        r#"
        .macro SAVE_INTERRUPT_REGS
            push r15; push r14; push r13; push r12; push r11; push r10; push r9; push r8
            push rbp; push rdi; push rsi; push rdx; push rcx; push rbx; push rax
        .endm
        .macro RESTORE_INTERRUPT_REGS
            pop rax; pop rbx; pop rcx; pop rdx; pop rsi; pop rdi; pop rbp; pop r8
            pop r9; pop r10; pop r11; pop r12; pop r13; pop r14; pop r15
        .endm
        .align 16
        .globl external_interrupt_entry
        external_interrupt_entry:
            cld
            SAVE_INTERRUPT_REGS
            mov rdi, rsp
            call rust_interrupt_dispatch
            RESTORE_INTERRUPT_REGS
            add rsp, 16
            iretq
        .align 16
        .globl vector_0x40
        vector_0x40:
            push 0
            push 0x40
            jmp external_interrupt_entry
        .align 16
        .globl vector_0xff
        vector_0xff:
            push 0
            push 0xff
            jmp external_interrupt_entry
    "#
    );

    unsafe extern "C" {
        pub fn vector_0x40();
        pub fn vector_0xff();
    }
}

/// Return the timer entry address.
#[must_use]
pub fn timer_entry_address() -> u64 {
    asm_stubs::vector_0x40 as *const () as u64
}
/// Return the spurious entry address.
#[must_use]
pub fn spurious_entry_address() -> u64 {
    asm_stubs::vector_0xff as *const () as u64
}
/// Return the Rust dispatcher address.
#[must_use]
pub fn dispatcher_address() -> u64 {
    rust_interrupt_dispatch as *const () as u64
}

/// Install timer and spurious gates while IF remains clear.
#[allow(unsafe_code)]
pub unsafe fn install() {
    // SAFETY: The two symbols are executable ring-0 entry points and IDT storage is resident.
    unsafe {
        idt::set_handler(
            usize::from(TIMER_VECTOR),
            timer_entry_address(),
            EXTERNAL_GATE_IST,
            idt::IDT_INTERRUPT_GATE,
        );
        idt::set_handler(
            usize::from(SPURIOUS_VECTOR),
            spurious_entry_address(),
            EXTERNAL_GATE_IST,
            idt::IDT_INTERRUPT_GATE,
        );
    }
}

/// Validate the two external gates.
pub fn validate() -> bool {
    [TIMER_VECTOR, SPURIOUS_VECTOR].iter().all(|&vector| {
        idt::gate_diagnostic(usize::from(vector)).is_some_and(
            |(offset, selector, ist, attr, reserved)| {
                offset != 0
                    && selector == super::gdt::KERNEL_CODE_SELECTOR
                    && ist == 0
                    && attr == idt::IDT_INTERRUPT_GATE | 0x80
                    && reserved == 0
            },
        )
    })
}

/// Dispatcher reached by the real assembly entries.
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
extern "C" fn rust_interrupt_dispatch(frame: *const InterruptFrame) {
    // SAFETY: The entry stubs pass a pointer to their fixed, register-aligned frame.
    let frame = unsafe { &*frame };
    match frame.vector as u8 {
        TIMER_VECTOR => timer::handle_tick(),
        SPURIOUS_VECTOR => timer::handle_spurious(),
        vector => {
            serial::log(format_args!(
                "FINNOS:INTERRUPT:UNEXPECTED\nFINNOS:INTERRUPT:VECTOR={vector:#x}\n"
            ));
            unsafe {
                core::arch::asm!("cli", options(nomem, nostack, preserves_flags));
            }
            super::qemu::exit(0x11);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{offset_of, size_of};
    #[test]
    fn policy_does_not_collide() {
        assert!(PIC_VECTOR_END < TIMER_VECTOR);
        assert!((0..=0x1f).all(|v| v != TIMER_VECTOR));
        assert_ne!(TIMER_VECTOR, SPURIOUS_VECTOR);
    }
    #[test]
    fn frame_layout_is_stable() {
        assert_eq!(offset_of!(InterruptFrame, vector), 15 * 8);
        assert_eq!(offset_of!(InterruptFrame, error_code), 16 * 8);
        assert_eq!(offset_of!(InterruptFrame, rip), 17 * 8);
        assert_eq!(size_of::<InterruptFrame>(), 20 * 8);
    }
}
