//! x86-64 syscall and sysret implementation.

#![allow(unsafe_code)]

#[cfg(all(target_arch = "x86_64", target_os = "none"))]
const IA32_EFER: u32 = 0xc000_0080;
#[cfg(all(target_arch = "x86_64", target_os = "none"))]
const EFER_SCE: u64 = 1 << 0;
#[cfg(all(target_arch = "x86_64", target_os = "none"))]
const IA32_STAR: u32 = 0xc000_0081;
#[cfg(all(target_arch = "x86_64", target_os = "none"))]
const IA32_LSTAR: u32 = 0xc000_0082;
#[cfg(all(target_arch = "x86_64", target_os = "none"))]
const IA32_FMASK: u32 = 0xc000_0084;

#[repr(C, align(4096))]
struct SyscallStack {
    data: [u8; 16384],
}

static mut KERNEL_SYSCALL_STACK: SyscallStack = SyscallStack { data: [0; 16384] };

#[unsafe(no_mangle)]
static mut KERNEL_SYSCALL_STACK_TOP: u64 = 0;

#[unsafe(no_mangle)]
static mut KERNEL_SYSCALL_USER_RSP: u64 = 0;

#[cfg(all(target_arch = "x86_64", target_os = "none"))]
core::arch::global_asm!(
    r#"
    .section .text.finnos_x86_64_syscall_entry,"ax",@progbits
    .global finnos_x86_64_syscall_entry
    .type finnos_x86_64_syscall_entry,@function
finnos_x86_64_syscall_entry:
    // Switch from user RSP to dedicated kernel syscall stack
    mov [rip + KERNEL_SYSCALL_USER_RSP], rsp
    mov rsp, [rip + KERNEL_SYSCALL_STACK_TOP]

    // Save user context and callee-preserved registers
    push rcx // user RIP
    push r11 // user RFLAGS
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    // Arguments from user:
    // RAX: syscall number
    // RDI: arg1
    // RSI: arg2
    // RDX: arg3
    // R10: arg4
    // R8:  arg5
    // R9:  arg6
    //
    // Call SysV ABI:
    // RDI: num
    // RSI: arg1
    // RDX: arg2
    // RCX: arg3
    // R8:  arg4
    // R9:  arg5
    // Stack: arg6
    push r9      // 7th argument (arg6)
    push rax     // stack alignment padding (16-byte aligned before call)
    mov r9, r8   // arg5
    mov r8, r10  // arg4
    mov rcx, rdx // arg3
    mov rdx, rsi // arg2
    mov rsi, rdi // arg1
    mov rdi, rax // syscall number
    call x86_64_syscall_dispatch
    add rsp, 16  // pop arg6 and padding

    // Return value in RAX is preserved
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11      // restore user RFLAGS
    pop rcx      // restore user RIP

    // Restore user RSP
    mov rsp, [rip + KERNEL_SYSCALL_USER_RSP]
    sysretq
    .size finnos_x86_64_syscall_entry, .-finnos_x86_64_syscall_entry
"#
);

#[cfg(all(target_arch = "x86_64", target_os = "none"))]
unsafe extern "C" {
    fn finnos_x86_64_syscall_entry();
}

/// Dispatch an x86-64 syscall from the assembly entry stub.
#[unsafe(no_mangle)]
pub extern "C" fn x86_64_syscall_dispatch(
    num: u64,
    a1: u64,
    a2: u64,
    a3: u64,
    a4: u64,
    a5: u64,
    a6: u64,
) -> u64 {
    crate::syscall::dispatch(num, a1, a2, a3, a4, a5, a6) as u64
}

#[cfg(all(target_arch = "x86_64", target_os = "none"))]
fn wrmsr(msr: u32, value: u64) {
    unsafe {
        core::arch::asm!(
            "wrmsr",
            in("ecx") msr,
            in("eax") value as u32,
            in("edx") (value >> 32) as u32,
            options(nostack, preserves_flags)
        );
    }
}

#[cfg(all(target_arch = "x86_64", target_os = "none"))]
fn rdmsr(msr: u32) -> u64 {
    let low: u32;
    let high: u32;
    unsafe {
        core::arch::asm!(
            "rdmsr",
            in("ecx") msr,
            out("eax") low,
            out("edx") high,
            options(nostack, preserves_flags)
        );
    }
    ((high as u64) << 32) | (low as u64)
}

/// Initialize syscall MSRs on the BSP.
///
/// # Safety
///
/// Must be called during single-core initialization on bare metal.
pub unsafe fn init() {
    let stack_top = unsafe { core::ptr::addr_of!(KERNEL_SYSCALL_STACK.data) as u64 + 16384 };
    unsafe {
        KERNEL_SYSCALL_STACK_TOP = stack_top;
    }

    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    {
        // 1. Enable SCE in IA32_EFER
        let efer = rdmsr(IA32_EFER);
        wrmsr(IA32_EFER, efer | EFER_SCE);

        // 2. Set up IA32_STAR
        // Kernel CS = 0x08, Kernel SS = 0x10. User base = 0x10.
        // User CS = (0x10 + 16) | 3 = 0x23. User SS = (0x10 + 8) | 3 = 0x1B.
        let star = ((0x0010u64) << 48) | ((0x0008u64) << 32);
        wrmsr(IA32_STAR, star);

        // 3. Set up IA32_LSTAR to point to finnos_x86_64_syscall_entry
        let lstar = finnos_x86_64_syscall_entry as *const () as usize as u64;
        wrmsr(IA32_LSTAR, lstar);

        // 4. Set up IA32_FMASK to clear IF (0x200), DF (0x400), TF (0x100)
        wrmsr(IA32_FMASK, 0x700);
    }
}

/// Enter user mode (Ring 3) by executing an iretq instruction.
///
/// # Safety
///
/// The provided entry_rip and user_rsp must name valid, mapped user memory.
pub unsafe fn enter_user_mode(entry_rip: u64, user_rsp: u64) -> ! {
    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    unsafe {
        core::arch::asm!(
            "mov ax, 0x1b",
            "mov ds, ax",
            "mov es, ax",
            "mov fs, ax",
            "mov gs, ax",
            "push 0x1b",         // SS (User Data 0x18 | RPL 3)
            "push {rsp}",        // RSP
            "push 0x2",          // RFLAGS (IF=0, bit 1 set)
            "push 0x23",         // CS (User Code 0x20 | RPL 3)
            "push {rip}",        // RIP
            "iretq",
            rsp = in(reg) user_rsp,
            rip = in(reg) entry_rip,
            options(noreturn)
        );
    }

    #[cfg(not(all(target_arch = "x86_64", target_os = "none")))]
    {
        let _ = (entry_rip, user_rsp);
        loop {
            core::hint::spin_loop();
        }
    }
}
