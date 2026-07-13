#![no_std]
#![no_main]
#![allow(unsafe_code)]
#![allow(unreachable_code)]

extern crate alloc;

#[cfg(feature = "qemu-test-heap")]
use alloc::{boxed::Box, string::String, vec::Vec};
#[cfg(feature = "qemu-test-heap")]
use core::alloc::Layout;
use core::panic::PanicInfo;
#[cfg(feature = "qemu-test-cooperative-tasks")]
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use finn_boot_protocol::{BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_MEMORY_MAP_PRESENT, BootInfo};
use finn_kernel::memory::heap::LockedHeap;
use finn_kernel::{
    arch::x86_64::{heap::KernelHeapMapping, paging, qemu, scheduler},
    boot_validation::validate_pointer,
    framebuffer::{encode_pixel, pixel_offset},
    memory::{EarlyPhysicalPageAllocator, parse_and_classify},
};

#[global_allocator]
static GLOBAL_HEAP: LockedHeap = LockedHeap::empty();

#[cfg(feature = "qemu-test-cooperative-tasks")]
static COOPERATIVE_EVENT_COUNT: AtomicUsize = AtomicUsize::new(0);
#[cfg(feature = "qemu-test-cooperative-tasks")]
static mut COOPERATIVE_EVENTS: [u8; 9] = [0; 9];
#[cfg(feature = "qemu-test-cooperative-tasks")]
static COOPERATIVE_REUSE_RUNS: AtomicUsize = AtomicUsize::new(0);
#[cfg(feature = "qemu-test-cooperative-tasks")]
static COOPERATIVE_SENTINELS: [AtomicU64; 3] = [const { AtomicU64::new(0) }; 3];
#[cfg(feature = "qemu-test-cooperative-tasks")]
static COOPERATIVE_SENTINEL_CHECKS: [AtomicUsize; 3] = [const { AtomicUsize::new(0) }; 3];

#[cfg(feature = "qemu-test-heap")]
const HEAP_TEST_POINTER_CAPACITY: usize = 1024;
#[cfg(feature = "qemu-test-heap")]
static mut HEAP_TEST_POINTERS: [*mut u8; 1024] = [core::ptr::null_mut(); 1024];

#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_SOFTWARE_REGISTERS: [u64; 15] = [0; 15];
#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_SOFTWARE_EXPECTED_RSP: u64 = 0;
#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_SOFTWARE_POST_RSP: u64 = 0;
#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_TIMER_REGISTERS: [u64; 15] = [0; 15];
#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_TIMER_EXPECTED_RSP: u64 = 0;
#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
#[unsafe(no_mangle)]
static mut PREEMPTION_TIMER_POST_RSP: u64 = 0;
#[cfg(feature = "qemu-test-preemption-context")]
static PREEMPTION_WORKER_DONE: core::sync::atomic::AtomicBool =
    core::sync::atomic::AtomicBool::new(false);
#[cfg(feature = "qemu-test-preemption-context")]
static PREEMPTION_WORKER_RELEASE: core::sync::atomic::AtomicBool =
    core::sync::atomic::AtomicBool::new(false);
#[cfg(feature = "qemu-test-preemption-context")]
static PREEMPTION_WORKER_SLOT: core::sync::atomic::AtomicU64 =
    core::sync::atomic::AtomicU64::new(0);
#[cfg(feature = "qemu-test-preemption-context")]
static PREEMPTION_WORKER_GENERATION: core::sync::atomic::AtomicU64 =
    core::sync::atomic::AtomicU64::new(0);

#[cfg(feature = "qemu-test-preemption-context")]
core::arch::global_asm!(
    r#"
    .global finnos_preemption_software_test
    .global software_after_int
finnos_preemption_software_test:
    push rbp; push rbx; push r12; push r13; push r14; push r15
    sub rsp, 8
    movabs rax, 0x1111111111111111
    movabs rbx, 0x2222222222222222
    movabs rcx, 0x3333333333333333
    movabs rdx, 0x4444444444444444
    movabs rsi, 0x5555555555555555
    movabs rdi, 0x6666666666666666
    movabs rbp, 0x7777777777777777
    movabs r8,  0x8888888888888888
    movabs r9,  0x9999999999999999
    movabs r10, 0xaaaaaaaaaaaaaaaa
    movabs r11, 0xbbbbbbbbbbbbbbbb
    movabs r12, 0xcccccccccccccccc
    movabs r13, 0xdddddddddddddddd
    movabs r14, 0xeeeeeeeeeeeeeeee
    movabs r15, 0xffffffffffffffff
    mov [rip + PREEMPTION_SOFTWARE_EXPECTED_RSP], rsp
    int 0x41
software_after_int:
    mov [rip + PREEMPTION_SOFTWARE_POST_RSP], rsp
    mov [rip + PREEMPTION_SOFTWARE_REGISTERS + 0], rax
    lea rax, [rip + PREEMPTION_SOFTWARE_REGISTERS]
    mov [rax + 8], rbx; mov [rax + 16], rcx; mov [rax + 24], rdx
    mov [rax + 32], rsi; mov [rax + 40], rdi; mov [rax + 48], rbp; mov [rax + 56], r8
    mov [rax + 64], r9; mov [rax + 72], r10; mov [rax + 80], r11; mov [rax + 88], r12
    mov [rax + 96], r13; mov [rax + 104], r14; mov [rax + 112], r15
    xor eax, eax
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 0]; movabs rdx, 0x1111111111111111; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 8]; movabs rdx, 0x2222222222222222; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 16]; movabs rdx, 0x3333333333333333; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 24]; movabs rdx, 0x4444444444444444; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 32]; movabs rdx, 0x5555555555555555; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 40]; movabs rdx, 0x6666666666666666; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 48]; movabs rdx, 0x7777777777777777; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 56]; movabs rdx, 0x8888888888888888; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 64]; movabs rdx, 0x9999999999999999; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 72]; movabs rdx, 0xaaaaaaaaaaaaaaaa; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 80]; movabs rdx, 0xbbbbbbbbbbbbbbbb; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 88]; movabs rdx, 0xcccccccccccccccc; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 96]; movabs rdx, 0xdddddddddddddddd; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 104]; movabs rdx, 0xeeeeeeeeeeeeeeee; cmp r11, rdx; jne software_fail
    mov r11, [rip + PREEMPTION_SOFTWARE_REGISTERS + 112]; movabs rdx, 0xffffffffffffffff; cmp r11, rdx; jne software_fail
    mov eax, 1
software_fail:
    add rsp, 8
    pop r15; pop r14; pop r13; pop r12; pop rbx; pop rbp
    ret
    "#
);

#[cfg(feature = "qemu-test-preemption-context")]
unsafe extern "C" {
    fn finnos_preemption_software_test() -> u64;
    fn software_after_int();
    fn preemption_timer_spin_start();
    fn preemption_timer_spin_end();
}

#[cfg(feature = "qemu-test-preemption-context")]
core::arch::global_asm!(
    r#"
    .global finnos_preemption_timer_test
    .global preemption_timer_spin_start
    .global preemption_timer_spin_end
finnos_preemption_timer_test:
    push rbp; push rbx; push r12; push r13; push r14; push r15
    sub rsp, 8
    movabs rax, 0x1111111111111111; movabs rbx, 0x2222222222222222
    movabs rcx, 0x3333333333333333; movabs rdx, 0x4444444444444444
    movabs rsi, 0x5555555555555555; movabs rdi, 0x6666666666666666
    movabs rbp, 0x7777777777777777; movabs r8,  0x8888888888888888
    movabs r9,  0x9999999999999999; movabs r10, 0xaaaaaaaaaaaaaaaa
    movabs r11, 0xbbbbbbbbbbbbbbbb; movabs r12, 0xcccccccccccccccc
    movabs r13, 0xdddddddddddddddd; movabs r14, 0xeeeeeeeeeeeeeeee
    movabs r15, 0xffffffffffffffff
    mov [rip + PREEMPTION_TIMER_EXPECTED_RSP], rsp
preemption_timer_spin_start:
    cmp byte ptr [rip + PREEMPTION_TIMER_OBSERVED], 0
    je preemption_timer_spin_start
preemption_timer_spin_end:
    mov [rip + PREEMPTION_TIMER_POST_RSP], rsp
    mov [rip + PREEMPTION_TIMER_REGISTERS + 0], rax
    lea rax, [rip + PREEMPTION_TIMER_REGISTERS]
    mov [rax + 8], rbx; mov [rax + 16], rcx; mov [rax + 24], rdx
    mov [rax + 32], rsi; mov [rax + 40], rdi; mov [rax + 48], rbp; mov [rax + 56], r8
    mov [rax + 64], r9; mov [rax + 72], r10; mov [rax + 80], r11; mov [rax + 88], r12
    mov [rax + 96], r13; mov [rax + 104], r14; mov [rax + 112], r15
    mov eax, 1
    add rsp, 8
    pop r15; pop r14; pop r13; pop r12; pop rbx; pop rbp
    ret
    "#
);

#[cfg(feature = "qemu-test-preemption-context")]
unsafe extern "C" {
    fn finnos_preemption_timer_test() -> u64;
}

core::arch::global_asm!(
    r#"
    .section .text._start
    .global _start
_start:
    cli
    mov r12, rdi
    lea rsp, [rip + __stack_top]
    and rsp, -16
    sub rsp, 8
    mov rdi, r12
    call kernel_main
    cli
1:  hlt
    jmp 1b
"#
);

#[cfg(feature = "qemu-test-cooperative-tasks")]
core::arch::global_asm!(
    r#"
    .global finnos_test_callee_saved
finnos_test_callee_saved:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    movabs rbx, 0x1122334455667788
    movabs rbp, 0x8877665544332211
    movabs r12, 0x13579bdf2468ace0
    movabs r13, 0x0eca8642fdb97531
    movabs r14, 0xa5a55a5af0f00f0f
    movabs r15, 0x5a5aa5a50f0ff0f0
    call finnos_cooperative_register_yield
    test rax, rax
    jz 1f
    movabs rax, 0x1122334455667788
    cmp rbx, rax
    jne 1f
    movabs rax, 0x8877665544332211
    cmp rbp, rax
    jne 1f
    movabs rax, 0x13579bdf2468ace0
    cmp r12, rax
    jne 1f
    movabs rax, 0x0eca8642fdb97531
    cmp r13, rax
    jne 1f
    movabs rax, 0xa5a55a5af0f00f0f
    cmp r14, rax
    jne 1f
    movabs rax, 0x5a5aa5a50f0ff0f0
    cmp r15, rax
    jne 1f
    mov eax, 1
    jmp 2f
1:  xor eax, eax
2:  add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
"#
);

#[cfg(feature = "qemu-test-cooperative-tasks")]
unsafe extern "sysv64" {
    fn finnos_test_callee_saved() -> u64;
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
#[unsafe(no_mangle)]
extern "sysv64" fn finnos_cooperative_register_yield() -> u64 {
    u64::from(scheduler::yield_now().is_ok())
}

#[unsafe(no_mangle)]
pub extern "sysv64" fn kernel_main(pointer: *const BootInfo) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:ENTRY\n");
    #[cfg(feature = "qemu-test-panic")]
    {
        let _ = pointer;
        panic!("controlled test panic");
    }

    // SAFETY: Called once on the BSP with interrupts disabled; the early stack is valid.
    unsafe {
        finn_kernel::arch::x86_64::exceptions::init_exception_foundation(current_stack_top());
    }

    let info = match validate_pointer(pointer) {
        Ok(info) => info,
        Err(_) => {
            finn_kernel::serial_log!("FINNOS:KERNEL:BOOTINFO_ERROR\n");
            failure();
        }
    };
    finn_kernel::serial_log!("FINNOS:KERNEL:BOOTINFO_OK version={}\n", info.version);
    if info.flags & BOOT_FLAG_MEMORY_MAP_PRESENT != 0 {
        finn_kernel::serial_log!(
            "FINNOS:KERNEL:MEMORY_MAP_OK address={:#x} length={} descriptor={}\n",
            info.memory_map.address,
            info.memory_map.byte_len,
            info.memory_map.descriptor_size
        );
        match parse_and_classify(info) {
            Ok((table, summary)) => {
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_PARSED\n");
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED\n");
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:DESCRIPTORS={}\n",
                    summary.descriptor_count
                );
                finn_kernel::serial_log!("FINNOS:MEMORY:REGIONS={}\n", summary.region_count);
                finn_kernel::serial_log!("FINNOS:MEMORY:USABLE_BYTES={}\n", summary.usable_bytes);
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:RESERVED_BYTES={}\n",
                    summary.reserved_bytes
                );
                finn_kernel::serial_log!("FINNOS:MEMORY:KERNEL_BYTES={}\n", summary.kernel_bytes);
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:FRAMEBUFFER_BYTES={}\n",
                    summary.framebuffer_bytes
                );
                #[cfg(feature = "qemu-test-memory-map")]
                {
                    if summary.usable_bytes == 0 {
                        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_USABLE\n");
                        failure();
                    }
                    if summary.kernel_bytes == 0 {
                        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_KERNEL\n");
                        failure();
                    }
                    if summary.framebuffer_bytes == 0 {
                        finn_kernel::serial_log!(
                            "FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_FRAMEBUFFER\n"
                        );
                        failure();
                    }
                }
                #[allow(unused_mut)]
                let mut allocator = match EarlyPhysicalPageAllocator::from_memory_regions(&table) {
                    Ok(allocator) => allocator,
                    Err(error) => {
                        finn_kernel::serial_log!(
                            "FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:{:?}\n",
                            error
                        );
                        failure();
                    }
                };
                if allocator.check_invariants().is_err() || allocator.free_pages() == 0 {
                    finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:INVALID_STATE\n");
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_READY\n");
                finn_kernel::serial_log!("FINNOS:MEMORY:TOTAL_PAGES={}\n", allocator.total_pages());
                finn_kernel::serial_log!("FINNOS:MEMORY:FREE_PAGES={}\n", allocator.free_pages());
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:ALLOCATED_PAGES={}\n",
                    allocator.allocated_pages()
                );
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:MANAGED_EXTENTS={}\n",
                    allocator.managed_extent_count()
                );
                finn_kernel::serial_log!(
                    "FINNOS:MEMORY:FREE_EXTENTS={}\n",
                    allocator.free_extent_count()
                );
                #[cfg(feature = "qemu-test-page-allocator")]
                run_page_allocator_test(&mut allocator);
                let zero_reservation = allocator.allocate_page().unwrap_or_else(|_| failure());
                if zero_reservation.start_address() != 0 {
                    allocator
                        .deallocate(
                            finn_kernel::memory::PageRange::new(
                                zero_reservation.start_address(),
                                1,
                            )
                            .unwrap_or_else(|_| failure()),
                        )
                        .unwrap_or_else(|_| failure());
                }
                #[cfg(feature = "qemu-test-page-tables")]
                let scratch = Some(allocator.allocate_page().unwrap_or_else(|_| failure()));
                #[cfg(not(feature = "qemu-test-page-tables"))]
                let scratch = None;
                #[cfg(feature = "qemu-test-page-tables")]
                finn_kernel::serial_log!(
                    "FINNOS:PAGING:SCRATCH_PHYSICAL={:#x}\n",
                    scratch.expect("scratch").start_address()
                );
                if let Some((offset, selector, ist, attr, reserved)) =
                    finn_kernel::arch::x86_64::idt::gate_diagnostic(14)
                {
                    finn_kernel::serial_log!(
                        "FINNOS:PAGING:PREBUILD_IDT14={:#x}:{:#x}:IST{}:ATTR{:#x}:RES{:#x}\n",
                        offset,
                        selector,
                        ist,
                        attr,
                        reserved
                    );
                }
                #[allow(unused_mut)]
                let mut address_space = match build_page_tables(info, &mut allocator, scratch) {
                    Ok(space) => space,
                    Err(error) => {
                        finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_TABLE_ERROR:{:?}\n", error);
                        failure();
                    }
                };
                let _zero_reservation = zero_reservation;
                let old_cr3 = paging::cpu_paging_info()
                    .map(|info| info.old_cr3)
                    .unwrap_or(0);
                finn_kernel::serial_log!("FINNOS:PAGING:OLD_CR3={:#x}\n", old_cr3);
                log_cpu_transition_state();
                if validate_required_mappings(&address_space, info).is_err() {
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:PAGE_TABLE_ERROR:RequiredMappingMissing\n"
                    );
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_TABLES_BUILT\n");
                finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_TABLES_ACTIVATING\n");
                // SAFETY: `build_page_tables` validated every mapping, kept the stack and
                // instruction identity-mapped, and interrupts remain disabled from entry.
                if unsafe { paging::activate(&mut address_space) }.is_err() {
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:PAGE_TABLE_ERROR:Cr3ActivationFailed\n"
                    );
                    failure();
                }
                finn_kernel::serial_log!(
                    "FINNOS:PAGING:NEW_CR3={:#x}\n",
                    address_space.root().address()
                );
                finn_kernel::serial_log!(
                    "FINNOS:PAGING:POOL_RESERVED={}\n",
                    address_space.pool().reserved_count()
                );
                finn_kernel::serial_log!(
                    "FINNOS:PAGING:TABLE_PAGES_USED={}\n",
                    address_space.pool().used_count()
                );
                finn_kernel::serial_log!(
                    "FINNOS:PAGING:MAPPED_PAGES={}\n",
                    address_space.mapped_pages()
                );
                if validate_required_mappings(&address_space, info).is_err()
                    || paging::current_cr3() != address_space.root().address()
                    || paging::current_cr0() & paging::CR0_WP == 0
                    || paging::current_efer() & paging::EFER_NXE == 0
                {
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:PAGE_TABLE_ERROR:RequiredPermissionMismatch\n"
                    );
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_TABLES_ACTIVE\n");
                finn_kernel::serial_log!("FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED\n");
                if !interrupts_disabled() {
                    finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_ERROR:InterruptsEnabled\n");
                    failure();
                }
                let mut heap_mapping = KernelHeapMapping::empty();
                if let Err(error) = heap_mapping.initialize(&mut address_space, &mut allocator) {
                    finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_ERROR:{:?}\n", error);
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_MAPPED\n");
                if let Err(error) = GLOBAL_HEAP.initialize(
                    finn_kernel::arch::x86_64::heap::KERNEL_HEAP_START as usize,
                    finn_kernel::arch::x86_64::heap::KERNEL_HEAP_END as usize,
                ) {
                    finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_ERROR:{:?}\n", error);
                    failure();
                }
                if let Err(error) = GLOBAL_HEAP.check_invariants() {
                    finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_ERROR:{:?}\n", error);
                    failure();
                }
                let heap_stats = GLOBAL_HEAP.stats();
                finn_kernel::serial_log!("FINNOS:KERNEL:HEAP_READY\n");
                finn_kernel::serial_log!(
                    "FINNOS:HEAP:VIRTUAL_START={:#x}\nFINNOS:HEAP:SIZE_BYTES={}\nFINNOS:HEAP:BACKING_PAGES={}\nFINNOS:HEAP:FREE_BYTES={}\nFINNOS:HEAP:ALLOCATED_BYTES={}\nFINNOS:HEAP:FREE_REGIONS={}\nFINNOS:HEAP:GUARD_PAGES=2\n",
                    finn_kernel::arch::x86_64::heap::KERNEL_HEAP_START,
                    finn_kernel::arch::x86_64::heap::KERNEL_HEAP_SIZE,
                    heap_mapping.mapped_count(),
                    heap_stats.free_bytes,
                    heap_stats.allocated_bytes,
                    heap_stats.free_region_count,
                );
                if !interrupts_disabled() {
                    failure();
                }
                // SAFETY: IF is still clear and the resident IDT is already loaded.
                unsafe {
                    finn_kernel::arch::x86_64::interrupts::install();
                }
                if !finn_kernel::arch::x86_64::interrupts::validate() {
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:INTERRUPT_IDT_READY\n");
                let (master_mask, slave_mask) =
                    finn_kernel::arch::x86_64::pic::initialize().unwrap_or_else(|_| failure());
                finn_kernel::serial_log!(
                    "FINNOS:KERNEL:PIC_REMAPPED\nFINNOS:KERNEL:PIC_MASKED\nFINNOS:INTERRUPTS:PIC_MASTER_MASK={master_mask:#x}\nFINNOS:INTERRUPTS:PIC_SLAVE_MASK={slave_mask:#x}\n"
                );
                let width = paging::cpu_paging_info()
                    .unwrap_or_else(|_| failure())
                    .physical_address_width;
                let local_apic = finn_kernel::arch::x86_64::apic::LocalApic::initialize(
                    &mut address_space,
                    width,
                )
                .unwrap_or_else(|_| failure());
                let apic_id = local_apic.id().unwrap_or_else(|_| failure());
                let apic_version = local_apic.version().unwrap_or_else(|_| failure());
                finn_kernel::serial_log!(
                    "FINNOS:KERNEL:LOCAL_APIC_MAPPED\nFINNOS:APIC:PHYSICAL_BASE={:#x}\nFINNOS:APIC:VIRTUAL_BASE=0x0000300000000000\nFINNOS:KERNEL:LOCAL_APIC_READY\nFINNOS:APIC:ID={apic_id}\nFINNOS:APIC:VERSION={:#x}\nFINNOS:APIC:MODE=xapic\nFINNOS:INTERRUPTS:SPURIOUS_VECTOR=0xff\n",
                    local_apic.physical_base(),
                    apic_version
                );
                let (pit_reference, apic_elapsed, apic_initial) =
                    finn_kernel::arch::x86_64::timer::initialize(local_apic)
                        .unwrap_or_else(|_| failure());
                finn_kernel::serial_log!(
                    "FINNOS:KERNEL:TIMER_CALIBRATED\nFINNOS:TIMER:FREQUENCY_HZ=100\nFINNOS:TIMER:TICK_MILLISECONDS=10\nFINNOS:TIMER:PIT_REFERENCE_COUNT={pit_reference}\nFINNOS:TIMER:APIC_CALIBRATION_ELAPSED_COUNTS={apic_elapsed}\nFINNOS:TIMER:APIC_INITIAL_COUNT={apic_initial}\nFINNOS:TIMER:APIC_DIVIDE=16\nFINNOS:KERNEL:TIMER_STARTED\nFINNOS:INTERRUPTS:TIMER_VECTOR=0x40\n"
                );
                finn_kernel::arch::x86_64::interrupts::publish_task_stack(
                    finn_kernel::task::TaskId::new(0, 1).unwrap_or_else(|_| failure()),
                    current_stack_bottom(),
                    current_stack_top(),
                )
                .unwrap_or_else(|_| failure());
                finn_kernel::arch::x86_64::cpu::enable_interrupts();
                if interrupts_disabled() {
                    failure();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:INTERRUPTS_ENABLED\n");
                let target = finn_kernel::arch::x86_64::timer::ticks().saturating_add(1);
                while finn_kernel::arch::x86_64::timer::ticks() < target {
                    finn_kernel::arch::x86_64::cpu::halt_once();
                }
                finn_kernel::serial_log!("FINNOS:KERNEL:TIMER_READY\n");
                let (bootstrap_id, idle_id) =
                    match scheduler::initialize(&mut address_space, &mut allocator) {
                        Ok(ids) => ids,
                        Err(error) => {
                            finn_kernel::serial_log!(
                                "FINNOS:KERNEL:TASK_STACK_ERROR:{:?}\n",
                                error
                            );
                            failure();
                        }
                    };
                scheduler::check_runtime_invariants(&address_space).unwrap_or_else(|_| failure());
                finn_kernel::serial_log!(
                    "FINNOS:KERNEL:TASK_STACKS_READY\nFINNOS:KERNEL:SCHEDULER_READY\nFINNOS:TASKS:CAPACITY=8\nFINNOS:TASKS:STACK_SIZE_BYTES=65536\nFINNOS:TASKS:STACK_REGION_BASE=0x0000280000000000\nFINNOS:TASKS:BOOTSTRAP_ID={}:{}\nFINNOS:TASKS:IDLE_ID={}:{}\n",
                    bootstrap_id.slot(),
                    bootstrap_id.generation(),
                    idle_id.slot(),
                    idle_id.generation(),
                );
                #[cfg(feature = "qemu-test-cooperative-tasks")]
                {
                    draw(info);
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:FRAMEBUFFER_OK address={:#x} width={} height={} stride={}\nFINNOS:KERNEL:FIRST_BOOT_COMPLETE\n",
                        info.framebuffer.address,
                        info.framebuffer.width,
                        info.framebuffer.height,
                        info.framebuffer.stride
                    );
                    run_cooperative_task_test(&mut address_space, &mut allocator);
                }
                #[cfg(feature = "qemu-test-page-tables")]
                {
                    draw(info);
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:FRAMEBUFFER_OK address={:#x} width={} height={} stride={}\n",
                        info.framebuffer.address,
                        info.framebuffer.width,
                        info.framebuffer.height,
                        info.framebuffer.stride
                    );
                    finn_kernel::serial_log!("FINNOS:KERNEL:FIRST_BOOT_COMPLETE\n");
                    run_page_table_test(
                        &mut address_space,
                        &mut allocator,
                        scratch.expect("scratch page allocated for page-table test"),
                    );
                }
                #[cfg(feature = "qemu-test-preemption-context")]
                {
                    draw(info);
                    finn_kernel::serial_log!(
                        "FINNOS:KERNEL:FRAMEBUFFER_OK address={:#x} width={} height={} stride={}\nFINNOS:KERNEL:FIRST_BOOT_COMPLETE\n",
                        info.framebuffer.address,
                        info.framebuffer.width,
                        info.framebuffer.height,
                        info.framebuffer.stride
                    );
                    run_preemption_context_test(&mut address_space, &mut allocator);
                }
            }
            Err(error) => {
                finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_ERROR:{:?}\n", error);
                failure();
            }
        }
    } else {
        finn_kernel::serial_log!("FINNOS:KERNEL:MEMORY_MAP_OK absent\n");
    }
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        draw(info);
        finn_kernel::serial_log!(
            "FINNOS:KERNEL:FRAMEBUFFER_OK address={:#x} width={} height={} stride={}\n",
            info.framebuffer.address,
            info.framebuffer.width,
            info.framebuffer.height,
            info.framebuffer.stride
        );
    } else {
        finn_kernel::serial_log!("FINNOS:KERNEL:FRAMEBUFFER_ERROR\n");
        failure();
    }
    finn_kernel::serial_log!("FINNOS:KERNEL:FIRST_BOOT_COMPLETE\n");

    #[cfg(feature = "qemu-test-heap")]
    run_heap_test();

    #[cfg(feature = "qemu-test-exceptions")]
    {
        // SAFETY: The exception foundation is initialized and the IDT is loaded.
        unsafe {
            finn_kernel::arch::x86_64::exceptions::run_exception_tests();
        }
    }

    #[cfg(feature = "qemu-test-timer-interrupts")]
    run_timer_interrupt_test();

    #[cfg(feature = "qemu-test-exit")]
    qemu::exit(0x10);
    #[cfg(not(feature = "qemu-test-exit"))]
    {
        if !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
            || !finn_kernel::arch::x86_64::timer::is_initialized()
            || scheduler::check_invariants().is_err()
        {
            failure();
        }
        scheduler::park_bootstrap_and_run_idle()
    }
}

#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
fn run_preemption_context_test(
    address_space: &mut finn_kernel::arch::x86_64::paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    use finn_kernel::arch::x86_64::{apic, interrupts, paging, timer};
    use finn_kernel::preemption::{self, PreemptionGuard};
    finn_kernel::serial_log!("FINNOS:TEST:PREEMPTION_CONTEXT:BEGIN\n");
    if core::mem::size_of::<finn_kernel::arch::x86_64::interrupts::KernelInterruptFrame>() != 184
        || core::mem::align_of::<finn_kernel::arch::x86_64::interrupts::KernelInterruptFrame>() != 8
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:FRAME_SIZE=184\nFINNOS:PREEMPT:FRAME_PREFIX_SIZE=160\nFINNOS:PREEMPT:FRAME_IRET_SIZE=176\nFINNOS:PREEMPT:FRAME_FOOTPRINT_SIZE=184\nFINNOS:TEST:PREEMPTION_CONTEXT:FRAME_LAYOUT_OK\n"
    );
    let bootstrap_id = scheduler::current_task().unwrap_or_else(|_| failure());
    finn_kernel::serial_log!("FINNOS:TEST:PREEMPTION_CONTEXT:SOFTWARE_INTERRUPT_BEGIN\n");
    interrupts::begin_capture(interrupts::PREEMPTION_TEST_VECTOR, bootstrap_id);
    let software_ok = unsafe { finnos_preemption_software_test() } != 0;
    let software = interrupts::snapshot().unwrap_or_else(|| failure());
    interrupts::end_capture();
    let patterns = [
        0x1111_1111_1111_1111,
        0x2222_2222_2222_2222,
        0x3333_3333_3333_3333,
        0x4444_4444_4444_4444,
        0x5555_5555_5555_5555,
        0x6666_6666_6666_6666,
        0x7777_7777_7777_7777,
        0x8888_8888_8888_8888,
        0x9999_9999_9999_9999,
        0xaaaa_aaaa_aaaa_aaaa,
        0xbbbb_bbbb_bbbb_bbbb,
        0xcccc_cccc_cccc_cccc,
        0xdddd_dddd_dddd_dddd,
        0xeeee_eeee_eeee_eeee,
        0xffff_ffff_ffff_ffff,
    ];
    let saved = [
        software.registers.rax,
        software.registers.rbx,
        software.registers.rcx,
        software.registers.rdx,
        software.registers.rsi,
        software.registers.rdi,
        software.registers.rbp,
        software.registers.r8,
        software.registers.r9,
        software.registers.r10,
        software.registers.r11,
        software.registers.r12,
        software.registers.r13,
        software.registers.r14,
        software.registers.r15,
    ];
    let expected_rsp = unsafe { PREEMPTION_SOFTWARE_EXPECTED_RSP };
    let post_rsp = unsafe { PREEMPTION_SOFTWARE_POST_RSP };
    let software_post = unsafe { PREEMPTION_SOFTWARE_REGISTERS };
    log_preemption_registers("SOFTWARE_SAVED", &saved);
    log_preemption_registers("SOFTWARE_POST", &software_post);
    let expected_rip = software_after_int as *const () as u64;
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:SOFTWARE_DEBUG_OK={} VECTOR={} TASK_SLOT={} CS={:#x} FLAGS={:#x} RIP={:#x} EXPECTED_RIP={:#x} RSP={:#x} EXPECTED_RSP={:#x} POST_RSP={:#x} FRAME={:#x} RETURN={:#x}\n",
        software_ok as u8,
        software.vector,
        software.task_id.slot(),
        software.cs,
        software.rflags,
        software.rip,
        expected_rip,
        software.interrupted_rsp,
        expected_rsp,
        post_rsp,
        software.frame_pointer,
        software.returned_frame_pointer
    );
    if !software_ok
        || software.vector != 0x41
        || software.task_id != bootstrap_id
        || saved != patterns
        || software_post != patterns
        || software.rip != expected_rip
        || software.interrupted_rsp != expected_rsp
        || post_rsp != expected_rsp
        || software.saved_ss != u64::from(finn_kernel::arch::x86_64::gdt::KERNEL_DATA_SELECTOR)
        || software.frame_pointer != software.returned_frame_pointer
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:SOFTWARE_FRAME={:#x}\nFINNOS:PREEMPT:SOFTWARE_RETURN_FRAME={:#x}\nFINNOS:PREEMPT:SOFTWARE_VECTOR=0x41\nFINNOS:PREEMPT:SOFTWARE_CS={:#x}\nFINNOS:PREEMPT:SOFTWARE_RFLAGS={:#x}\nFINNOS:PREEMPT:SOFTWARE_SAVED_RIP={:#x}\nFINNOS:PREEMPT:SOFTWARE_EXPECTED_RIP={:#x}\nFINNOS:PREEMPT:SOFTWARE_INTERRUPTED_RSP={:#x}\nFINNOS:PREEMPT:SOFTWARE_EXPECTED_RSP={:#x}\nFINNOS:PREEMPT:SOFTWARE_POST_RSP={:#x}\nFINNOS:PREEMPT:SOFTWARE_SAVED_RSP_FIELD={:#x}\nFINNOS:PREEMPT:SOFTWARE_SAVED_SS={:#x}\nFINNOS:TEST:PREEMPTION_CONTEXT:SOFTWARE_INTERRUPT_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:ALL_GPRS_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:EXACT_RIP_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:EXACT_RSP_OK\n",
        software.frame_pointer,
        software.returned_frame_pointer,
        software.cs,
        software.rflags,
        software.rip,
        expected_rip,
        software.interrupted_rsp,
        expected_rsp,
        post_rsp,
        software.saved_rsp_field_address,
        software.saved_ss
    );
    finn_kernel::serial_log!("FINNOS:TEST:PREEMPTION_CONTEXT:REAL_TIMER_BEGIN\n");
    interrupts::begin_timer_test(bootstrap_id);
    let timer_start = timer::ticks();
    let deliveries_start = timer::real_deliveries();
    let eoi_start = apic::eoi_count();
    let cr3_before = paging::current_cr3();
    let timer_ok = unsafe { finnos_preemption_timer_test() } != 0;
    interrupts::end_timer_test();
    let timer_snapshot = interrupts::snapshot().unwrap_or_else(|| failure());
    let timer_saved = [
        timer_snapshot.registers.rax,
        timer_snapshot.registers.rbx,
        timer_snapshot.registers.rcx,
        timer_snapshot.registers.rdx,
        timer_snapshot.registers.rsi,
        timer_snapshot.registers.rdi,
        timer_snapshot.registers.rbp,
        timer_snapshot.registers.r8,
        timer_snapshot.registers.r9,
        timer_snapshot.registers.r10,
        timer_snapshot.registers.r11,
        timer_snapshot.registers.r12,
        timer_snapshot.registers.r13,
        timer_snapshot.registers.r14,
        timer_snapshot.registers.r15,
    ];
    let timer_expected_rsp = unsafe { PREEMPTION_TIMER_EXPECTED_RSP };
    let timer_post_rsp = unsafe { PREEMPTION_TIMER_POST_RSP };
    let timer_post = unsafe { PREEMPTION_TIMER_REGISTERS };
    log_preemption_registers("TIMER_SAVED", &timer_saved);
    log_preemption_registers("TIMER_POST", &timer_post);
    let loop_start = preemption_timer_spin_start as *const () as u64;
    let loop_end = preemption_timer_spin_end as *const () as u64;
    let timer_end = timer::ticks();
    let deliveries_end = timer::real_deliveries();
    let eoi_end = apic::eoi_count();
    let cr3_after = paging::current_cr3();
    if !timer_ok
        || timer_snapshot.vector != 0x40
        || timer_snapshot.rip < loop_start
        || timer_snapshot.rip >= loop_end
        || timer_saved != patterns
        || timer_post != patterns
        || timer_snapshot.interrupted_rsp != timer_expected_rsp
        || timer_post_rsp != timer_expected_rsp
        || timer_snapshot.saved_ss
            != u64::from(finn_kernel::arch::x86_64::gdt::KERNEL_DATA_SELECTOR)
        || timer_snapshot.frame_pointer != timer_snapshot.returned_frame_pointer
        || timer_end <= timer_start
        || deliveries_end <= deliveries_start
        || eoi_end - eoi_start != deliveries_end - deliveries_start
        || cr3_before != cr3_after
        || !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
        || finn_kernel::interrupt::interrupt_depth() != 0
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:TIMER_FRAME={:#x}\nFINNOS:PREEMPT:TIMER_RETURN_FRAME={:#x}\nFINNOS:PREEMPT:TIMER_VECTOR=0x40\nFINNOS:PREEMPT:TIMER_CS={:#x}\nFINNOS:PREEMPT:TIMER_RFLAGS={:#x}\nFINNOS:PREEMPT:TIMER_SAVED_RIP={:#x}\nFINNOS:PREEMPT:TIMER_LOOP_START={:#x}\nFINNOS:PREEMPT:TIMER_LOOP_END={:#x}\nFINNOS:PREEMPT:TIMER_INTERRUPTED_RSP={:#x}\nFINNOS:PREEMPT:TIMER_EXPECTED_RSP={:#x}\nFINNOS:PREEMPT:TIMER_POST_RSP={:#x}\nFINNOS:PREEMPT:TIMER_SAVED_RSP_FIELD={:#x}\nFINNOS:PREEMPT:TIMER_SAVED_SS={:#x}\nFINNOS:TEST:PREEMPTION_CONTEXT:REAL_TIMER_OK\n",
        timer_snapshot.frame_pointer,
        timer_snapshot.returned_frame_pointer,
        timer_snapshot.cs,
        timer_snapshot.rflags,
        timer_snapshot.rip,
        loop_start,
        loop_end,
        timer_snapshot.interrupted_rsp,
        timer_expected_rsp,
        timer_post_rsp,
        timer_snapshot.saved_rsp_field_address,
        timer_snapshot.saved_ss
    );
    let worker_id =
        scheduler::spawn(preemption_worker, address_space, allocator).unwrap_or_else(|_| failure());
    while !PREEMPTION_WORKER_DONE.load(core::sync::atomic::Ordering::Acquire) {
        scheduler::yield_now().unwrap_or_else(|_| failure());
    }
    if PREEMPTION_WORKER_SLOT.load(core::sync::atomic::Ordering::Acquire) != worker_id.slot() as u64
        || PREEMPTION_WORKER_GENERATION.load(core::sync::atomic::Ordering::Acquire)
            != u64::from(worker_id.generation())
    {
        failure();
    }
    PREEMPTION_WORKER_RELEASE.store(true, core::sync::atomic::Ordering::Release);
    scheduler::yield_now().unwrap_or_else(|_| failure());
    scheduler::reap(worker_id, address_space, allocator).unwrap_or_else(|_| failure());
    let idle_id = scheduler::idle_task_id().unwrap_or_else(|_| failure());
    interrupts::begin_timer_test(idle_id);
    scheduler::probe_idle_once().unwrap_or_else(|_| failure());
    interrupts::end_timer_test();
    let idle_snapshot = interrupts::snapshot().unwrap_or_else(|| failure());
    if idle_snapshot.vector != 0x40 || idle_snapshot.task_id != idle_id {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:IDLE_FRAME={:#x}\nFINNOS:PREEMPT:IDLE_INTERRUPTED_RSP={:#x}\nFINNOS:PREEMPT:IDLE_SAVED_RSP_FIELD={:#x}\nFINNOS:PREEMPT:IDLE_SAVED_SS={:#x}\nFINNOS:PREEMPT:TEST_IDLE_SLOT={}\nFINNOS:PREEMPT:TEST_IDLE_GENERATION={}\nFINNOS:TEST:PREEMPTION_CONTEXT:IDLE_ATTRIBUTION_OK\n",
        idle_snapshot.frame_pointer,
        idle_snapshot.interrupted_rsp,
        idle_snapshot.saved_rsp_field_address,
        idle_snapshot.saved_ss,
        idle_snapshot.task_id.slot(),
        idle_snapshot.task_id.generation()
    );
    finn_kernel::serial_log!("FINNOS:TEST:PREEMPTION_CONTEXT:TASK_ATTRIBUTION_OK\n");
    preemption::configure_quantum_ticks(1);
    let before_ticks = timer::ticks();
    let before_deliveries = timer::real_deliveries();
    let before_eois = apic::eoi_count();
    let before_task = scheduler::current_task().unwrap_or_else(|_| failure());
    let before_switches = scheduler::stats()
        .unwrap_or_else(|_| failure())
        .context_switch_count;
    let outer = PreemptionGuard::enter().unwrap_or_else(|_| failure());
    let inner = PreemptionGuard::enter().unwrap_or_else(|_| failure());
    while timer::ticks() == before_ticks {
        finn_kernel::arch::x86_64::cpu::halt_once();
    }
    if !preemption::reschedule_requested() || preemption::preemption_depth() != 2 {
        failure();
    }
    drop(inner);
    if preemption::preemption_depth() != 1 || preemption::take_reschedule_request() {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:PREEMPTION_CONTEXT:REQUEST_DEFERRED_OK\n");
    drop(outer);
    if preemption::preemption_depth() != 0
        || !preemption::take_reschedule_request()
        || preemption::reschedule_requested()
    {
        failure();
    }
    let after_ticks = timer::ticks();
    let after_deliveries = timer::real_deliveries();
    let after_eois = apic::eoi_count();
    let after_task = scheduler::current_task().unwrap_or_else(|_| failure());
    let after_switches = scheduler::stats()
        .unwrap_or_else(|_| failure())
        .context_switch_count;
    if after_switches != before_switches
        || after_deliveries <= before_deliveries
        || after_eois <= before_eois
        || after_ticks <= before_ticks
        || before_task != after_task
        || preemption::preemption_faulted()
    {
        failure();
    }
    let bootstrap = scheduler::current_task().unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:BOOTSTRAP_SLOT={}\nFINNOS:PREEMPT:BOOTSTRAP_GENERATION={}\nFINNOS:PREEMPT:WORKER_SLOT={}\nFINNOS:PREEMPT:WORKER_GENERATION={}\nFINNOS:PREEMPT:IDLE_SLOT={}\nFINNOS:PREEMPT:IDLE_GENERATION={}\nFINNOS:PREEMPT:DEPTH_NESTED=2\nFINNOS:PREEMPT:DEPTH_INNER_DROPPED=1\nFINNOS:PREEMPT:DEPTH_OUTER_DROPPED=0\nFINNOS:PREEMPT:REQUEST_WHILE_NESTED=1\nFINNOS:PREEMPT:REQUEST_AFTER_INNER_DROP=1\nFINNOS:PREEMPT:REQUEST_AFTER_OUTER_DROP=1\nFINNOS:PREEMPT:REQUEST_TAKEN=1\nFINNOS:PREEMPT:REQUEST_AFTER_TAKE=0\nFINNOS:PREEMPT:TICK_DELTA={}\nFINNOS:PREEMPT:DELIVERY_DELTA={}\nFINNOS:PREEMPT:EOI_DELTA={}\nFINNOS:PREEMPT:SWITCHES_BEFORE={}\nFINNOS:PREEMPT:SWITCHES_AFTER={}\nFINNOS:PREEMPT:CR3_BEFORE={:#x}\nFINNOS:PREEMPT:CR3_AFTER={:#x}\nFINNOS:PREEMPT:CURRENT_TASK_BEFORE_SLOT={}\nFINNOS:PREEMPT:CURRENT_TASK_BEFORE_GENERATION={}\nFINNOS:PREEMPT:CURRENT_TASK_AFTER_SLOT={}\nFINNOS:PREEMPT:CURRENT_TASK_AFTER_GENERATION={}\nFINNOS:PREEMPT:IF_ENABLED=1\nFINNOS:PREEMPT:INTERRUPT_DEPTH=0\nFINNOS:PREEMPT:FAULTED=0\nFINNOS:PREEMPT:INTERRUPT_CONTEXT_FAULT=0\nFINNOS:PREEMPT:SCHEDULER_ISR_ENTRIES={}\n",
        bootstrap.slot(),
        bootstrap.generation(),
        worker_id.slot(),
        worker_id.generation(),
        idle_id.slot(),
        idle_id.generation(),
        after_ticks - before_ticks,
        after_deliveries - before_deliveries,
        after_eois - before_eois,
        before_switches,
        after_switches,
        cr3_before,
        cr3_after,
        before_task.slot(),
        before_task.generation(),
        after_task.slot(),
        after_task.generation(),
        scheduler::interrupt_context_entry_count()
    );
    preemption::configure_quantum_ticks(0);
    if scheduler::check_invariants().is_err()
        || scheduler::check_runtime_invariants(address_space).is_err()
        || preemption::preemption_depth() != 0
        || preemption::reschedule_requested()
        || preemption::preemption_faulted()
        || finn_kernel::interrupt::interrupt_depth() != 0
        || finn_kernel::interrupt::interrupt_context_faulted()
        || scheduler::interrupt_context_entry_count() != 0
        || !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
        || paging::current_cr3() != cr3_before
        || !interrupts::task_stack_published(bootstrap_id)
        || !interrupts::task_stack_published(idle_id)
        || interrupts::task_stack_published(worker_id)
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TEST:PREEMPTION_CONTEXT:REQUEST_CONSUMED_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:NO_SWITCH_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:INVARIANTS_OK\nFINNOS:TEST:PREEMPTION_CONTEXT:PASS\n"
    );
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-preemption-context")]
fn log_preemption_registers(prefix: &str, values: &[u64; 15]) {
    for (index, value) in values.iter().enumerate() {
        finn_kernel::serial_log!("FINNOS:PREEMPT:{prefix}_R{index}={value:#x}\n");
    }
}

#[cfg(feature = "qemu-test-preemption-context")]
#[allow(unsafe_code)]
fn preemption_worker() {
    let id = scheduler::current_task().unwrap_or_else(|_| failure());
    PREEMPTION_WORKER_SLOT.store(id.slot() as u64, core::sync::atomic::Ordering::Release);
    PREEMPTION_WORKER_GENERATION.store(
        u64::from(id.generation()),
        core::sync::atomic::Ordering::Release,
    );
    finn_kernel::arch::x86_64::interrupts::begin_capture(
        finn_kernel::arch::x86_64::interrupts::PREEMPTION_TEST_VECTOR,
        id,
    );
    let software_ok = unsafe { finnos_preemption_software_test() } != 0;
    let software_snapshot =
        finn_kernel::arch::x86_64::interrupts::snapshot().unwrap_or_else(|| failure());
    finn_kernel::arch::x86_64::interrupts::end_capture();
    if !software_ok || software_snapshot.task_id != id {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:WORKER_SOFTWARE_FRAME={:#x}\nFINNOS:PREEMPT:WORKER_SOFTWARE_TASK_SLOT={}\nFINNOS:PREEMPT:WORKER_SOFTWARE_GENERATION={}\nFINNOS:PREEMPT:WORKER_SOFTWARE_RSP={:#x}\nFINNOS:PREEMPT:WORKER_SOFTWARE_SAVED_SS={:#x}\n",
        software_snapshot.frame_pointer,
        software_snapshot.task_id.slot(),
        software_snapshot.task_id.generation(),
        software_snapshot.interrupted_rsp,
        software_snapshot.saved_ss
    );
    finn_kernel::arch::x86_64::interrupts::begin_timer_test(id);
    let timer_ok = unsafe { finnos_preemption_timer_test() } != 0;
    let timer_snapshot =
        finn_kernel::arch::x86_64::interrupts::snapshot().unwrap_or_else(|| failure());
    if !timer_ok || timer_snapshot.task_id != id {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:PREEMPT:WORKER_TIMER_FRAME={:#x}\nFINNOS:PREEMPT:WORKER_TIMER_TASK_SLOT={}\nFINNOS:PREEMPT:WORKER_TIMER_GENERATION={}\nFINNOS:PREEMPT:WORKER_TIMER_RSP={:#x}\nFINNOS:PREEMPT:WORKER_TIMER_SAVED_SS={:#x}\n",
        timer_snapshot.frame_pointer,
        timer_snapshot.task_id.slot(),
        timer_snapshot.task_id.generation(),
        timer_snapshot.interrupted_rsp,
        timer_snapshot.saved_ss
    );
    finn_kernel::arch::x86_64::interrupts::end_timer_test();
    PREEMPTION_WORKER_DONE.store(true, core::sync::atomic::Ordering::Release);
    while !PREEMPTION_WORKER_RELEASE.load(core::sync::atomic::Ordering::Acquire) {
        scheduler::yield_now().unwrap_or_else(|_| failure());
    }
}

fn interrupts_disabled() -> bool {
    let flags: u64;
    // SAFETY: `pushfq`/`pop` reads processor flags without changing control state.
    unsafe {
        core::arch::asm!("pushfq", "pop {}", out(reg) flags, options(nomem, preserves_flags));
    }
    flags & (1 << 9) == 0
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn cooperative_worker() {
    let id = scheduler::current_task().unwrap_or_else(|_| failure());
    if !(2..=4).contains(&id.slot()) {
        failure();
    }
    let pattern = u8::try_from(id.slot())
        .unwrap_or_else(|_| failure())
        .wrapping_mul(0x31);
    let sentinel = [pattern; 1024];
    let worker_index = id.slot() - 2;
    COOPERATIVE_SENTINELS[worker_index].store(sentinel.as_ptr() as u64, Ordering::Relaxed);
    for step in 1..=3_u8 {
        if id.slot() == 2 && step == 1 {
            finn_kernel::arch::x86_64::cpu::halt_once();
        }
        let index = COOPERATIVE_EVENT_COUNT.fetch_add(1, Ordering::Relaxed);
        if index >= 9 || sentinel.iter().any(|byte| *byte != pattern) {
            failure();
        }
        // SAFETY: each worker writes one distinct monotonically assigned event
        // index on the single BSP; the test reads only after all workers exit.
        unsafe {
            COOPERATIVE_EVENTS[index] = (id.slot() as u8 - 1) * 10 + step;
        }
        if step != 3 {
            scheduler::yield_now().unwrap_or_else(|_| failure());
        }
        if sentinel.iter().any(|byte| *byte != pattern) {
            failure();
        }
        COOPERATIVE_SENTINEL_CHECKS[worker_index].fetch_add(1, Ordering::Relaxed);
    }
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn cooperative_reuse_worker() {
    COOPERATIVE_REUSE_RUNS.fetch_add(1, Ordering::Relaxed);
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn cooperative_register_peer() {}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn run_cooperative_task_test(
    address_space: &mut paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    finn_kernel::serial_log!(
        "FINNOS:TEST:COOPERATIVE_TASKS:BEGIN\nFINNOS:TEST:COOPERATIVE_TASKS:BOOTSTRAP_OK\n"
    );
    let start_ticks = finn_kernel::arch::x86_64::timer::ticks();
    let start_deliveries = finn_kernel::arch::x86_64::timer::real_deliveries();
    let start_eois = finn_kernel::arch::x86_64::apic::eoi_count();
    let cr3_before = paging::current_cr3();
    let free_baseline = allocator.free_pages();
    let mapped_baseline = address_space.mapped_pages();
    let stats_baseline = scheduler::stats().unwrap_or_else(|_| failure());
    let a = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure());
    let b = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure());
    let c = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure());
    let idle = finn_kernel::task::TaskId::new(1, 1).unwrap_or_else(|_| failure());
    let a_stack = scheduler::task_diagnostics(a).unwrap_or_else(|_| failure());
    let b_stack = scheduler::task_diagnostics(b).unwrap_or_else(|_| failure());
    let c_stack = scheduler::task_diagnostics(c).unwrap_or_else(|_| failure());
    let idle_stack = scheduler::task_diagnostics(idle).unwrap_or_else(|_| failure());
    if !(a_stack.stack_end <= b_stack.stack_start
        && b_stack.stack_end <= c_stack.stack_start
        && (c_stack.stack_end <= idle_stack.stack_start
            || idle_stack.stack_end <= a_stack.stack_start))
    {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:A_STACK_START={:#x}\nFINNOS:TASKS:A_STACK_END={:#x}\nFINNOS:TASKS:B_STACK_START={:#x}\nFINNOS:TASKS:B_STACK_END={:#x}\nFINNOS:TASKS:C_STACK_START={:#x}\nFINNOS:TASKS:C_STACK_END={:#x}\nFINNOS:TASKS:IDLE_STACK_START={:#x}\nFINNOS:TASKS:IDLE_STACK_END={:#x}\nFINNOS:TEST:COOPERATIVE_TASKS:STACKS_OK\nFINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_BEGIN\n",
        a_stack.stack_start,
        a_stack.stack_end,
        b_stack.stack_start,
        b_stack.stack_end,
        c_stack.stack_start,
        c_stack.stack_end,
        idle_stack.stack_start,
        idle_stack.stack_end
    );
    for _ in 0..3 {
        scheduler::yield_now().unwrap_or_else(|_| failure());
    }
    let expected = [11, 21, 31, 12, 22, 32, 13, 23, 33];
    if COOPERATIVE_EVENT_COUNT.load(Ordering::Relaxed) != expected.len() {
        failure();
    }
    for (index, value) in expected.iter().enumerate() {
        // SAFETY: workers have exited and no other code writes the fixed test buffer.
        let actual = unsafe { COOPERATIVE_EVENTS[index] };
        finn_kernel::serial_log!("FINNOS:TASKS:EVENT_{index}={actual}\n");
        if actual != *value {
            failure();
        }
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:EVENT_COUNT=9\nFINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_OK\n"
    );
    let register_peer = scheduler::spawn(cooperative_register_peer, address_space, allocator)
        .unwrap_or_else(|_| failure());
    // SAFETY: the helper preserves its caller's callee-saved registers and
    // follows the SysV64 stack-alignment contract around the real yield call.
    if unsafe { finnos_test_callee_saved() } != 1 {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!("FINNOS:TEST:COOPERATIVE_TASKS:REGISTER_STATE_OK\n");
    let sentinels = core::array::from_fn::<_, 3, _>(|index| {
        COOPERATIVE_SENTINELS[index].load(Ordering::Relaxed)
    });
    for (index, (sentinel, stack)) in sentinels
        .iter()
        .zip([a_stack, b_stack, c_stack])
        .enumerate()
    {
        if *sentinel < stack.stack_start
            || *sentinel + 1024 > stack.stack_end
            || COOPERATIVE_SENTINEL_CHECKS[index].load(Ordering::Relaxed) != 3
            || sentinels
                .iter()
                .enumerate()
                .any(|(other, value)| other != index && value == sentinel)
        {
            failure();
        }
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:A_SENTINEL={:#x}\nFINNOS:TASKS:B_SENTINEL={:#x}\nFINNOS:TASKS:C_SENTINEL={:#x}\nFINNOS:TEST:COOPERATIVE_TASKS:STACK_ISOLATION_OK\n",
        sentinels[0],
        sentinels[1],
        sentinels[2]
    );
    let before_reap = scheduler::stats().unwrap_or_else(|_| failure());
    if before_reap.completed_task_count - stats_baseline.completed_task_count != 4
        || before_reap.exited_tasks != 4
        || before_reap.queue_length != 0
        || scheduler::current_task()
            .unwrap_or_else(|_| failure())
            .slot()
            != 0
        || [a, b, c, register_peer].iter().any(|id| {
            scheduler::task_diagnostics(*id).map_or(true, |diagnostic| {
                diagnostic.state != finn_kernel::task::TaskState::Exited
                    || diagnostic.queued
                    || diagnostic.stack_start == 0
            })
        })
    {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:COMPLETED_DELTA=4\nFINNOS:TASKS:EXITED_BEFORE_REAP=4\nFINNOS:TASKS:QUEUE_LENGTH_BEFORE_REAP=0\nFINNOS:TEST:COOPERATIVE_TASKS:TASK_EXIT_OK\n"
    );
    for id in [a, b, c, register_peer] {
        scheduler::reap(id, address_space, allocator).unwrap_or_else(|_| failure());
    }
    if allocator.free_pages() != free_baseline || address_space.mapped_pages() != mapped_baseline {
        failure();
    }
    let after_reap = scheduler::stats().unwrap_or_else(|_| failure());
    if after_reap.vacant_tasks != stats_baseline.vacant_tasks
        || after_reap.reaped_task_count - stats_baseline.reaped_task_count != 4
    {
        failure();
    }
    for stack in [a_stack, b_stack, c_stack] {
        let mut address = stack.stack_start;
        while address < stack.stack_end {
            if address_space
                .translate(address)
                .unwrap_or_else(|_| failure())
                .is_some()
            {
                failure();
            }
            address += finn_kernel::memory::PAGE_SIZE;
        }
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:PHYSICAL_FREE_BASELINE={free_baseline}\nFINNOS:TASKS:PHYSICAL_FREE_AFTER_REAP={}\nFINNOS:TASKS:MAPPED_BASELINE={mapped_baseline}\nFINNOS:TASKS:MAPPED_AFTER_REAP={}\nFINNOS:TASKS:VACANT_BASELINE={}\nFINNOS:TASKS:VACANT_AFTER_REAP={}\nFINNOS:TASKS:REAPED_DELTA=4\nFINNOS:TEST:COOPERATIVE_TASKS:STACK_RECLAIM_OK\n",
        allocator.free_pages(),
        address_space.mapped_pages(),
        stats_baseline.vacant_tasks,
        after_reap.vacant_tasks
    );
    let d = scheduler::spawn(cooperative_reuse_worker, address_space, allocator)
        .unwrap_or_else(|_| failure());
    if d.slot() != a.slot() || d.generation() == a.generation() {
        failure();
    }
    if scheduler::task_state(a)
        != Err(scheduler::SchedulerError::Task(
            finn_kernel::task::TaskError::StaleTaskId,
        ))
    {
        failure();
    }
    scheduler::yield_now().unwrap_or_else(|_| failure());
    if COOPERATIVE_REUSE_RUNS.load(Ordering::Relaxed) != 1 {
        failure();
    }
    scheduler::reap(d, address_space, allocator).unwrap_or_else(|_| failure());
    if allocator.free_pages() != free_baseline || address_space.mapped_pages() != mapped_baseline {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:REUSED_SLOT={}\nFINNOS:TASKS:OLD_GENERATION={}\nFINNOS:TASKS:NEW_GENERATION={}\nFINNOS:TASKS:STALE_ID_REJECTED=1\nFINNOS:TASKS:REUSE_RUNS=1\nFINNOS:TEST:COOPERATIVE_TASKS:SLOT_REUSE_OK\n",
        d.slot(),
        a.generation(),
        d.generation()
    );
    let idle_start_ticks = finn_kernel::arch::x86_64::timer::ticks();
    let heap_before_idle = GLOBAL_HEAP.stats();
    scheduler::probe_idle_once().unwrap_or_else(|_| failure());
    let idle_diagnostic = scheduler::task_diagnostics(idle).unwrap_or_else(|_| failure());
    let idle_rsp = scheduler::idle_rsp();
    let idle_tick_delta = finn_kernel::arch::x86_64::timer::ticks() - idle_start_ticks;
    if idle_tick_delta == 0
        || idle_rsp < idle_diagnostic.stack_start
        || idle_rsp >= idle_diagnostic.stack_end
        || GLOBAL_HEAP.stats() != heap_before_idle
        || scheduler::current_task()
            .unwrap_or_else(|_| failure())
            .slot()
            != 0
    {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:IDLE_RSP={idle_rsp:#x}\nFINNOS:TASKS:IDLE_TICK_DELTA={idle_tick_delta}\nFINNOS:TEST:COOPERATIVE_TASKS:IDLE_CONTEXT_OK\n"
    );
    let end_ticks = finn_kernel::arch::x86_64::timer::ticks();
    let end_deliveries = finn_kernel::arch::x86_64::timer::real_deliveries();
    let end_eois = finn_kernel::arch::x86_64::apic::eoi_count();
    let cr3_after = paging::current_cr3();
    if end_ticks <= start_ticks
        || end_deliveries <= start_deliveries
        || end_eois <= start_eois
        || end_eois - start_eois != end_deliveries - start_deliveries
        || cr3_after != cr3_before
        || scheduler::interrupt_context_entry_count() != 0
        || finn_kernel::arch::x86_64::apic::timer_in_service()
        || finn_kernel::interrupt::interrupt_context_faulted()
        || !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
        || finn_kernel::interrupt::interrupt_depth() != 0
    {
        failure();
    }
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TASKS:TIMER_START_TICKS={start_ticks}\nFINNOS:TASKS:TIMER_END_TICKS={end_ticks}\nFINNOS:TASKS:TICK_DELTA={}\nFINNOS:TASKS:DELIVERY_DELTA={}\nFINNOS:TASKS:EOI_DELTA={}\nFINNOS:TASKS:CR3_BEFORE={cr3_before:#x}\nFINNOS:TASKS:CR3_AFTER={cr3_after:#x}\nFINNOS:TASKS:SCHEDULER_ISR_ENTRIES=0\nFINNOS:TEST:COOPERATIVE_TASKS:TIMER_CONTINUITY_OK\n",
        end_ticks - start_ticks,
        end_deliveries - start_deliveries,
        end_eois - start_eois
    );
    scheduler::check_runtime_invariants(address_space).unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TEST:COOPERATIVE_TASKS:INVARIANTS_OK\nFINNOS:TEST:COOPERATIVE_TASKS:PASS\n"
    );
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-timer-interrupts")]
fn run_timer_interrupt_test() -> ! {
    use core::alloc::Layout;
    use finn_kernel::arch::x86_64::{apic, pic, pit, timer};
    use finn_kernel::interrupt::{
        InterruptContextGuard, in_interrupt_context, interrupt_context_faulted, interrupt_depth,
    };
    let (master_mask, slave_mask) = pic::masks();
    if master_mask != 0xff || slave_mask != 0xff {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TEST:TIMER_INTERRUPTS:BEGIN\nFINNOS:TEST:TIMER_INTERRUPTS:PIC_MASK_OK\n"
    );
    let width = finn_kernel::arch::x86_64::paging::cpu_paging_info()
        .unwrap_or_else(|_| failure())
        .physical_address_width;
    if !apic::runtime_mode_valid(width) {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:TIMER_INTERRUPTS:APIC_MODE_OK\n");
    if !finn_kernel::arch::x86_64::interrupts::validate() {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:TIMER_INTERRUPTS:IDT_GATES_OK\n");
    if !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
        || in_interrupt_context()
        || interrupt_depth() != 0
    {
        failure();
    }
    if finn_kernel::arch::x86_64::interrupts::call_site_alignment() != 0 {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TEST:TIMER_INTERRUPTS:IF_ENABLED_OK\nFINNOS:INTERRUPTS:CALL_ALIGNMENT=0\nFINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_BEGIN\n"
    );
    finn_kernel::arch::x86_64::cpu::disable_interrupts();
    let start = timer::ticks();
    let start_deliveries = timer::real_deliveries();
    let start_eois = apic::eoi_count();
    finn_kernel::arch::x86_64::cpu::enable_interrupts();
    let target = start.saturating_add(8);
    while timer::ticks() < target {
        finn_kernel::arch::x86_64::cpu::halt_once();
    }
    let end = timer::ticks();
    finn_kernel::arch::x86_64::cpu::disable_interrupts();
    let end_deliveries = timer::real_deliveries();
    let end_eois = apic::eoi_count();
    finn_kernel::arch::x86_64::cpu::enable_interrupts();
    if end < target
        || end_deliveries.saturating_sub(start_deliveries) < 8
        || end_eois.saturating_sub(start_eois) < 8
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TIMER:TEST_START_TICKS={start}\nFINNOS:TIMER:TEST_END_TICKS={end}\nFINNOS:TIMER:TEST_ELAPSED_TICKS={}\nFINNOS:TIMER:TEST_DELIVERY_DELTA={}\nFINNOS:TIMER:TEST_EOI_DELTA={}\nFINNOS:TIMER:TEST_UPTIME_MS={}\nFINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_OK\n",
        end - start,
        end_deliveries - start_deliveries,
        end_eois - start_eois,
        timer::uptime_milliseconds()
    );
    let window_start = timer::ticks();
    let pit_count = pit::duration_count(50).unwrap_or_else(|_| failure());
    let window_end = pit::wait_reference(50).unwrap_or_else(|_| failure());
    let window_ticks = timer::ticks().saturating_sub(window_start);
    finn_kernel::serial_log!(
        "FINNOS:TIMER:FREQUENCY_WINDOW_MS=50\nFINNOS:TIMER:FREQUENCY_WINDOW_PIT_COUNT={pit_count}\nFINNOS:TIMER:FREQUENCY_WINDOW_TICKS={window_ticks}\n"
    );
    if window_end != pit_count || !timer::frequency_window_valid(window_ticks) {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TEST:TIMER_INTERRUPTS:FREQUENCY_OK\nFINNOS:TEST:TIMER_INTERRUPTS:MONOTONIC_OK\n"
    );
    if apic::timer_in_service() {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:TEST:TIMER_INTERRUPTS:EOI_OK\nFINNOS:TEST:TIMER_INTERRUPTS:SPURIOUS_BEGIN\n"
    );
    let before = timer::spurious_count();
    let before_eoi = apic::eoi_count();
    // SAFETY: This software interrupt exercises only the spurious return path.
    unsafe {
        core::arch::asm!("int 0xff");
    }
    if timer::spurious_count() != before + 1
        || apic::eoi_count() != before_eoi
        || !finn_kernel::arch::x86_64::cpu::interrupts_enabled()
        || in_interrupt_context()
        || interrupt_depth() != 0
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:TIMER_INTERRUPTS:SPURIOUS_OK\n");
    if !timer::context_observed()
        || in_interrupt_context()
        || interrupt_depth() != 0
        || interrupt_context_faulted()
        || timer::real_deliveries() < 8
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:TIMER_INTERRUPTS:INTERRUPT_CONTEXT_OK\n");
    let layout = Layout::from_size_align(32, 8).unwrap_or_else(|_| failure());
    let pointer = GLOBAL_HEAP.allocate(layout).unwrap_or_else(|_| failure());
    let baseline = GLOBAL_HEAP.stats();
    let guard = InterruptContextGuard::enter().unwrap_or_else(|_| failure());
    if GLOBAL_HEAP.allocate(layout)
        != Err(finn_kernel::memory::heap::HeapError::InterruptContextAllocationForbidden)
    {
        failure();
    }
    let null = unsafe { core::alloc::GlobalAlloc::alloc(&GLOBAL_HEAP, layout) };
    if !null.is_null() {
        failure();
    }
    if unsafe { GLOBAL_HEAP.deallocate(pointer, layout) }
        != Err(finn_kernel::memory::heap::HeapError::InterruptContextAllocationForbidden)
    {
        failure();
    }
    if GLOBAL_HEAP.stats() != baseline {
        failure();
    }
    drop(guard);
    if in_interrupt_context() || interrupt_depth() != 0 {
        failure();
    }
    unsafe {
        GLOBAL_HEAP
            .deallocate(pointer, layout)
            .unwrap_or_else(|_| failure());
    }
    if GLOBAL_HEAP.stats().free_bytes != baseline.free_bytes + baseline.allocated_bytes {
        failure();
    }
    GLOBAL_HEAP.check_invariants().unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:TEST:TIMER_INTERRUPTS:HEAP_INTERRUPT_GUARD_OK\nFINNOS:TEST:TIMER_INTERRUPTS:PASS\n"
    );
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-heap")]
fn run_heap_test() -> ! {
    use finn_kernel::arch::x86_64::heap::{KERNEL_HEAP_END, KERNEL_HEAP_START};
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:BEGIN\n");
    let baseline = GLOBAL_HEAP.stats();
    let alignments = [1usize, 2, 4, 8, 16, 32, 64, 256, 4096, 65_536];
    for alignment in alignments {
        let layout = Layout::from_size_align(37, alignment).unwrap_or_else(|_| failure());
        let pointer = GLOBAL_HEAP.allocate(layout).unwrap_or_else(|_| failure());
        let address = pointer as usize;
        if address < KERNEL_HEAP_START as usize
            || address >= KERNEL_HEAP_END as usize
            || !address.is_multiple_of(alignment)
        {
            failure();
        }
        // SAFETY: The global heap returned this live allocation and the exact layout is used.
        unsafe {
            core::ptr::write_bytes(pointer, 0xa5, layout.size());
            if core::ptr::read_volatile(pointer) != 0xa5 {
                failure();
            }
            GLOBAL_HEAP
                .deallocate(pointer, layout)
                .unwrap_or_else(|_| failure());
        }
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:ALIGNMENT_OK\n");
    {
        let value = Box::new([0x5a_u8; 128]);
        if value.iter().any(|byte| *byte != 0x5a)
            || !(value.as_ptr() as usize >= KERNEL_HEAP_START as usize
                && (value.as_ptr() as usize) < KERNEL_HEAP_END as usize)
        {
            failure();
        }
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:BOX_OK\n");
    {
        let mut values = Vec::<u64>::new();
        values.try_reserve_exact(1024).unwrap_or_else(|_| failure());
        for value in 0..1024 {
            values.push(value as u64 * 3);
        }
        if values.len() != 1024
            || values
                .iter()
                .enumerate()
                .any(|(i, value)| *value != i as u64 * 3)
        {
            failure();
        }
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:VEC_OK\n");
    {
        let mut text = String::new();
        text.try_reserve_exact(32).unwrap_or_else(|_| failure());
        text.push_str("FinnOS early heap");
        if text != "FinnOS early heap" {
            failure();
        }
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:STRING_OK\n");
    let block = Layout::from_size_align(128, 8).unwrap();
    let a = GLOBAL_HEAP.allocate(block).unwrap_or_else(|_| failure());
    let b = GLOBAL_HEAP.allocate(block).unwrap_or_else(|_| failure());
    let c = GLOBAL_HEAP.allocate(block).unwrap_or_else(|_| failure());
    unsafe {
        GLOBAL_HEAP
            .deallocate(b, block)
            .unwrap_or_else(|_| failure())
    };
    let small = Layout::from_size_align(64, 8).unwrap();
    let d = GLOBAL_HEAP.allocate(small).unwrap_or_else(|_| failure());
    if d != b {
        failure();
    }
    unsafe {
        GLOBAL_HEAP
            .deallocate(d, small)
            .unwrap_or_else(|_| failure());
        GLOBAL_HEAP
            .deallocate(a, block)
            .unwrap_or_else(|_| failure());
        GLOBAL_HEAP
            .deallocate(c, block)
            .unwrap_or_else(|_| failure());
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:FRAGMENTATION_OK\n");
    let exhaustion_layout = Layout::from_size_align(1025, 8).unwrap();
    let mut count = 0usize;
    while count < HEAP_TEST_POINTER_CAPACITY {
        match GLOBAL_HEAP.allocate(exhaustion_layout) {
            Ok(pointer) => {
                // SAFETY: This test runs once on the BSP and the fixed array is dedicated to
                // its pointer bookkeeping.
                unsafe { HEAP_TEST_POINTERS[count] = pointer };
                count += 1;
            }
            Err(_) => break,
        }
    }
    if count == 0
        || count == HEAP_TEST_POINTER_CAPACITY
        || GLOBAL_HEAP.allocate(exhaustion_layout).is_ok()
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:EXHAUSTION_OK\n");
    for index in 0..count {
        let pointer = unsafe { HEAP_TEST_POINTERS[index] };
        unsafe {
            GLOBAL_HEAP
                .deallocate(pointer, exhaustion_layout)
                .unwrap_or_else(|_| failure())
        };
    }
    let reused = GLOBAL_HEAP
        .allocate(exhaustion_layout)
        .unwrap_or_else(|_| failure());
    unsafe {
        GLOBAL_HEAP
            .deallocate(reused, exhaustion_layout)
            .unwrap_or_else(|_| failure())
    };
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:REUSE_OK\n");
    let stats = GLOBAL_HEAP.stats();
    if stats.free_bytes != baseline.free_bytes || stats.allocated_bytes != baseline.allocated_bytes
    {
        failure();
    }
    finn_kernel::serial_log!(
        "FINNOS:HEAP:TOTAL_BYTES={} FREE_BYTES={} ALLOCATED_BYTES={} PEAK_ALLOCATED_BYTES={} ALLOCATIONS={} DEALLOCATIONS={} FAILED_ALLOCATIONS={} FREE_REGIONS={} LARGEST_FREE_REGION={}\n",
        stats.total_bytes,
        stats.free_bytes,
        stats.allocated_bytes,
        stats.peak_allocated_bytes,
        stats.allocation_count,
        stats.deallocation_count,
        stats.failed_allocation_count,
        stats.free_region_count,
        stats.largest_free_region,
    );
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:STATS_OK\n");
    GLOBAL_HEAP.check_invariants().unwrap_or_else(|_| failure());
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:INVARIANTS_OK\n");
    finn_kernel::serial_log!("FINNOS:TEST:HEAP:PASS\n");
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-page-tables")]
fn run_page_table_test(
    address_space: &mut paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
    scratch: finn_kernel::memory::PhysicalPage,
) -> ! {
    unsafe extern "C" {
        static __stack_guard_low_start: u8;
        static __stack_guard_low_end: u8;
        static __stack_guard_high_start: u8;
        static __stack_guard_high_end: u8;
        static __stack_bottom: u8;
        static __stack_top: u8;
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:BEGIN\n");
    if address_space.root().address() == 0 {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:CR3_OK\n");
    let translation = address_space
        .translate(paging::SCRATCH_VIRTUAL_ADDRESS)
        .unwrap_or_else(|_| failure())
        .unwrap_or_else(|| failure());
    if !translation.effective_writable
        || translation.effective_user
        || translation.effective_executable
        || translation.physical_address & !0xfff != scratch.start_address()
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:PERMISSIONS_OK\n");
    let low_start = unsafe { &__stack_guard_low_start as *const u8 as u64 };
    let low_end = unsafe { &__stack_guard_low_end as *const u8 as u64 };
    let high_start = unsafe { &__stack_guard_high_start as *const u8 as u64 };
    let high_end = unsafe { &__stack_guard_high_end as *const u8 as u64 };
    let stack_bottom = unsafe { &__stack_bottom as *const u8 as u64 };
    let stack_top = unsafe { &__stack_top as *const u8 as u64 };
    if !low_start.is_multiple_of(finn_kernel::memory::PAGE_SIZE)
        || !low_end.is_multiple_of(finn_kernel::memory::PAGE_SIZE)
        || !high_start.is_multiple_of(finn_kernel::memory::PAGE_SIZE)
        || !high_end.is_multiple_of(finn_kernel::memory::PAGE_SIZE)
        || low_end <= low_start
        || high_end <= high_start
        || !(low_end < current_stack_pointer() && current_stack_pointer() < high_start)
        || current_stack_pointer() <= stack_bottom
        || current_stack_pointer() >= stack_top
    {
        failure();
    }
    let mut guard_page = low_start;
    while guard_page < low_end {
        if address_space
            .translate(guard_page)
            .unwrap_or_else(|_| failure())
            .is_some()
        {
            failure();
        }
        guard_page += finn_kernel::memory::PAGE_SIZE;
    }
    guard_page = high_start;
    while guard_page < high_end {
        if address_space
            .translate(guard_page)
            .unwrap_or_else(|_| failure())
            .is_some()
        {
            failure();
        }
        guard_page += finn_kernel::memory::PAGE_SIZE;
    }
    if address_space
        .translate(0)
        .unwrap_or_else(|_| failure())
        .is_some()
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:GUARD_PAGES_OK\n");
    // SAFETY: the mapping was installed before activation and the software walker
    // verified its writable, supervisor-only, NX permissions.
    unsafe {
        core::ptr::write_volatile(
            paging::SCRATCH_VIRTUAL_ADDRESS as *mut u64,
            0xf1_f0_0bad_cafe_beef,
        );
        if core::ptr::read_volatile(paging::SCRATCH_VIRTUAL_ADDRESS as *const u64)
            != 0xf1_f0_0bad_cafe_beef
        {
            failure();
        }
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:SCRATCH_MAP_OK\n");
    address_space
        .unmap_page(
            paging::VirtualPage::new(paging::SCRATCH_VIRTUAL_ADDRESS).unwrap_or_else(|_| failure()),
        )
        .unwrap_or_else(|_| failure());
    if address_space
        .translate(paging::SCRATCH_VIRTUAL_ADDRESS)
        .unwrap_or_else(|_| failure())
        .is_some()
    {
        failure();
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:SCRATCH_UNMAP_OK\n");
    allocator
        .deallocate(
            finn_kernel::memory::PageRange::new(scratch.start_address(), 1)
                .unwrap_or_else(|_| failure()),
        )
        .unwrap_or_else(|_| failure());
    allocator.check_invariants().unwrap_or_else(|_| failure());
    finn_kernel::arch::x86_64::exceptions::expect_non_present_read(paging::SCRATCH_VIRTUAL_ADDRESS);
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_TABLES:PAGE_FAULT_BEGIN\n");
    // SAFETY: the test has removed the leaf, invalidated the address, and armed
    // the page-fault handler for exactly this supervisor read.
    unsafe {
        let _ = core::ptr::read_volatile(paging::SCRATCH_VIRTUAL_ADDRESS as *const u8);
    }
    failure()
}

fn build_page_tables(
    info: &BootInfo,
    allocator: &mut EarlyPhysicalPageAllocator,
    scratch: Option<finn_kernel::memory::PhysicalPage>,
) -> Result<paging::ActiveAddressSpace, paging::PagingError> {
    #[cfg(not(feature = "qemu-test-page-tables"))]
    let _ = scratch;
    let cpu = paging::cpu_paging_info()?;
    let mut plan = paging::MappingPlan::new();
    unsafe extern "C" {
        static __text_start: u8;
        static __text_end: u8;
        static __rodata_start: u8;
        static __rodata_end: u8;
        static __data_start: u8;
        static __data_end: u8;
        static __bss_start: u8;
        static __bss_end: u8;
        static __kernel_after_stack_start: u8;
        static __kernel_after_stack_end: u8;
        static __stack_bottom: u8;
        static __stack_top: u8;
        static __stack_guard_low_start: u8;
        static __stack_guard_low_end: u8;
        static __stack_guard_high_start: u8;
        static __stack_guard_high_end: u8;
    }
    let section = |start: u64,
                   end: u64,
                   permissions: paging::MappingPermissions,
                   purpose: paging::MappingPurpose,
                   plan: &mut paging::MappingPlan|
     -> Result<(), paging::PagingError> {
        if start == end {
            return Ok(());
        }
        let page_start = paging::align_down(start);
        let page_end = paging::align_up(end)?;
        plan.push(paging::MappingRequest {
            virtual_start: page_start,
            physical_start: page_start,
            page_count: (page_end - page_start) / finn_kernel::memory::PAGE_SIZE,
            permissions,
            purpose,
        })
    };
    // SAFETY: these symbols are page-aligned linker boundaries in this kernel image.
    unsafe {
        section(
            &__text_start as *const u8 as u64,
            &__text_end as *const u8 as u64,
            paging::MappingPermissions::kernel_rx(),
            paging::MappingPurpose::KernelText,
            &mut plan,
        )?;
        section(
            &__rodata_start as *const u8 as u64,
            &__rodata_end as *const u8 as u64,
            paging::MappingPermissions::kernel_r_nx(),
            paging::MappingPurpose::KernelReadOnly,
            &mut plan,
        )?;
        section(
            &__data_start as *const u8 as u64,
            &__data_end as *const u8 as u64,
            paging::MappingPermissions::kernel_rw_nx(),
            paging::MappingPurpose::KernelData,
            &mut plan,
        )?;
        section(
            &__bss_start as *const u8 as u64,
            &__bss_end as *const u8 as u64,
            paging::MappingPermissions::kernel_rw_nx(),
            paging::MappingPurpose::KernelBss,
            &mut plan,
        )?;
        section(
            &__kernel_after_stack_start as *const u8 as u64,
            &__kernel_after_stack_end as *const u8 as u64,
            paging::MappingPermissions::kernel_rw_nx(),
            paging::MappingPurpose::KernelBss,
            &mut plan,
        )?;
        section(
            &__stack_bottom as *const u8 as u64,
            &__stack_top as *const u8 as u64,
            paging::MappingPermissions::kernel_rw_nx(),
            paging::MappingPurpose::KernelStack,
            &mut plan,
        )?;
        let _guards = (
            &__stack_guard_low_start as *const u8 as u64,
            &__stack_guard_low_end as *const u8 as u64,
            &__stack_guard_high_start as *const u8 as u64,
            &__stack_guard_high_end as *const u8 as u64,
        );
    }
    let resource = |range: finn_boot_protocol::PhysicalRange,
                    permissions: paging::MappingPermissions,
                    purpose: paging::MappingPurpose,
                    plan: &mut paging::MappingPlan|
     -> Result<(), paging::PagingError> {
        if range.start == 0 || range.byte_len == 0 {
            return Err(paging::PagingError::InvalidBootInfoRange);
        }
        let end = range
            .start
            .checked_add(range.byte_len)
            .ok_or(paging::PagingError::AddressOverflow)?;
        let start = paging::align_down(range.start);
        let finish = paging::align_up(end)?;
        plan.push(paging::MappingRequest {
            virtual_start: start,
            physical_start: start,
            page_count: (finish - start) / finn_kernel::memory::PAGE_SIZE,
            permissions,
            purpose,
        })
    };
    resource(
        info.boot_info_storage,
        paging::MappingPermissions::kernel_r_nx(),
        paging::MappingPurpose::BootInfo,
        &mut plan,
    )?;
    resource(
        finn_boot_protocol::PhysicalRange {
            start: info.memory_map.address,
            byte_len: info.memory_map.byte_len,
        },
        paging::MappingPermissions::kernel_r_nx(),
        paging::MappingPurpose::MemoryMapStorage,
        &mut plan,
    )?;
    resource(
        finn_boot_protocol::PhysicalRange {
            start: info.framebuffer.address,
            byte_len: info.framebuffer.byte_len,
        },
        paging::MappingPermissions::framebuffer(),
        paging::MappingPurpose::Framebuffer,
        &mut plan,
    )?;
    if info.flags & finn_boot_protocol::BOOT_FLAG_RSDP_PRESENT != 0 && info.rsdp_address != 0 {
        resource(
            finn_boot_protocol::PhysicalRange {
                start: info.rsdp_address,
                byte_len: 4096,
            },
            paging::MappingPermissions::kernel_r_nx(),
            paging::MappingPurpose::AcpiRsdp,
            &mut plan,
        )?;
    }
    #[cfg(feature = "qemu-test-page-tables")]
    if let Some(scratch) = scratch {
        let page = paging::VirtualPage::new(paging::SCRATCH_VIRTUAL_ADDRESS)?;
        if page.address() == 0 {
            return Err(paging::PagingError::InvalidCanonicalAddress);
        }
        plan.push(paging::MappingRequest {
            virtual_start: page.address(),
            physical_start: scratch.start_address(),
            page_count: 1,
            permissions: paging::MappingPermissions::kernel_rw_nx(),
            purpose: paging::MappingPurpose::TestScratch,
        })?;
    }
    paging::build(&plan, allocator, cpu.physical_address_width)
}

fn validate_required_mappings(
    address_space: &paging::ActiveAddressSpace,
    info: &BootInfo,
) -> Result<(), paging::PagingError> {
    unsafe extern "C" {
        static __stack_guard_low_start: u8;
        static __stack_guard_low_end: u8;
        static __stack_guard_high_start: u8;
        static __stack_guard_high_end: u8;
        static __stack_bottom: u8;
        static __stack_top: u8;
        static _start: u8;
    }
    let check =
        |address: u64, writable: bool, executable: bool| -> Result<(), paging::PagingError> {
            let translation = address_space
                .translate(address)?
                .ok_or(paging::PagingError::RequiredMappingMissing)?;
            if translation.effective_writable != writable
                || translation.effective_executable != executable
                || translation.effective_user
            {
                return Err(paging::PagingError::RequiredPermissionMismatch);
            }
            Ok(())
        };
    unsafe {
        check(&__stack_bottom as *const u8 as u64, true, false)?;
        check(current_stack_pointer(), true, false)?;
        check(&_start as *const u8 as u64, false, true)?;
        check(kernel_main as *const () as u64, false, true)?;
    }
    for vector in [3, 6, 14] {
        check(
            finn_kernel::arch::x86_64::exceptions::handler_address(vector)
                .ok_or(paging::PagingError::RequiredMappingMissing)?,
            false,
            true,
        )?;
    }
    check(
        finn_kernel::arch::x86_64::exceptions::dispatcher_address(),
        false,
        true,
    )?;
    check(
        finn_kernel::arch::x86_64::gdt::storage_address(),
        true,
        false,
    )?;
    check(
        finn_kernel::arch::x86_64::idt::storage_address(),
        true,
        false,
    )?;
    check(
        finn_kernel::arch::x86_64::exceptions::storage_address(),
        true,
        false,
    )?;
    check(
        finn_kernel::arch::x86_64::tss::double_fault_stack_start(),
        true,
        false,
    )?;
    let low_start = unsafe { &__stack_guard_low_start as *const u8 as u64 };
    let low_end = unsafe { &__stack_guard_low_end as *const u8 as u64 };
    let high_start = unsafe { &__stack_guard_high_start as *const u8 as u64 };
    let high_end = unsafe { &__stack_guard_high_end as *const u8 as u64 };
    if !low_start.is_multiple_of(4096)
        || !low_end.is_multiple_of(4096)
        || !high_start.is_multiple_of(4096)
        || !high_end.is_multiple_of(4096)
        || low_end <= low_start
        || high_end <= high_start
        || address_space.translate(low_start)?.is_some()
        || address_space.translate(high_start)?.is_some()
        || !(low_end < current_stack_pointer() && current_stack_pointer() < high_start)
    {
        return Err(paging::PagingError::GuardPageMapped);
    }
    if address_space.translate(0)?.is_some() {
        return Err(paging::PagingError::NullPageMapped);
    }
    let boot = address_space
        .translate(info.boot_info_storage.start)?
        .ok_or(paging::PagingError::RequiredMappingMissing)?;
    if boot.effective_writable || boot.effective_executable {
        return Err(paging::PagingError::RequiredPermissionMismatch);
    }
    let map = address_space
        .translate(info.memory_map.address)?
        .ok_or(paging::PagingError::RequiredMappingMissing)?;
    if map.effective_writable || map.effective_executable {
        return Err(paging::PagingError::RequiredPermissionMismatch);
    }
    let fb = address_space
        .translate(info.framebuffer.address)?
        .ok_or(paging::PagingError::RequiredMappingMissing)?;
    if !fb.effective_writable || fb.effective_executable || !fb.cache_disable || !fb.write_through {
        return Err(paging::PagingError::RequiredPermissionMismatch);
    }
    for &page in address_space.pool().pages() {
        let translation = address_space
            .translate(page)?
            .ok_or(paging::PagingError::RequiredMappingMissing)?;
        if !translation.effective_writable
            || translation.effective_executable
            || translation.effective_user
        {
            return Err(paging::PagingError::RequiredPermissionMismatch);
        }
    }
    Ok(())
}

fn current_stack_pointer() -> u64 {
    let value: u64;
    // SAFETY: reading RSP is a side-effect-free diagnostic operation in ring zero.
    unsafe {
        core::arch::asm!("mov {}, rsp", out(reg) value, options(nomem, nostack, preserves_flags));
    }
    value
}

fn current_instruction_pointer() -> u64 {
    let value: u64;
    // SAFETY: LEA reads the current RIP without touching memory.
    unsafe {
        core::arch::asm!("lea {}, [rip]", out(reg) value, options(nomem, nostack, preserves_flags));
    }
    value
}

fn log_cpu_transition_state() {
    let cr = paging::cpu_paging_info().unwrap_or_else(|_| failure());
    finn_kernel::serial_log!(
        "FINNOS:PAGING:CR0={:#x} CR4={:#x} EFER={:#x} RIP={:#x} RSP={:#x}\n",
        cr.cr0,
        cr.cr4,
        cr.efer,
        current_instruction_pointer(),
        current_stack_pointer()
    );
    finn_kernel::serial_log!(
        "FINNOS:PAGING:GDT={:#x} IDT={:#x} TSS={:#x} IST={:#x}\n",
        finn_kernel::arch::x86_64::gdt::storage_address(),
        finn_kernel::arch::x86_64::idt::storage_address(),
        finn_kernel::arch::x86_64::exceptions::storage_address(),
        finn_kernel::arch::x86_64::tss::double_fault_stack_start()
    );
    for vector in [3, 6, 14] {
        if let Some((offset, selector, ist, attr, reserved)) =
            finn_kernel::arch::x86_64::idt::gate_diagnostic(vector)
        {
            finn_kernel::serial_log!(
                "FINNOS:PAGING:IDT{}={:#x}:{:#x}:IST{}:ATTR{:#x}:RES{:#x}\n",
                vector,
                offset,
                selector,
                ist,
                attr,
                reserved
            );
        }
    }
}

#[cfg(feature = "qemu-test-page-allocator")]
fn run_page_allocator_test(allocator: &mut EarlyPhysicalPageAllocator) -> ! {
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:BEGIN\n");
    let initial = allocator.free_pages();
    let first = allocator
        .allocate_page()
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert!(
        first
            .start_address()
            .is_multiple_of(finn_kernel::memory::PAGE_SIZE)
    );
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:SINGLE_ALLOC_OK\n");
    let contiguous = allocator
        .allocate_contiguous(4)
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert!(contiguous.start_address() != first.start_address());
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:CONTIGUOUS_ALLOC_OK\n");
    allocator
        .deallocate(page_range_from_page(first))
        .unwrap_or_else(|error| page_allocator_failure(error));
    let reused = allocator
        .allocate_page()
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert_eq!(reused, first);
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:REUSE_OK\n");
    allocator
        .deallocate(page_range_from_page(reused))
        .unwrap_or_else(|error| page_allocator_failure(error));
    allocator
        .deallocate(contiguous)
        .unwrap_or_else(|error| page_allocator_failure(error));
    assert_eq!(allocator.free_pages(), initial);
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:FREE_OK\n");
    if allocator.deallocate(page_range_from_page(reused)).is_ok() {
        page_allocator_failure(finn_kernel::memory::PageAllocationError::CorruptAllocatorState);
    }
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:DOUBLE_FREE_REJECTED\n");
    allocator
        .check_invariants()
        .unwrap_or_else(|error| page_allocator_failure(error));
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:INVARIANTS_OK\n");
    finn_kernel::serial_log!("FINNOS:TEST:PAGE_ALLOCATOR:PASS\n");
    qemu::exit(0x10)
}

#[cfg(feature = "qemu-test-page-allocator")]
fn page_allocator_failure(error: finn_kernel::memory::PageAllocationError) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:{:?}\n", error);
    failure()
}

#[cfg(feature = "qemu-test-page-allocator")]
fn page_range_from_page(page: finn_kernel::memory::PhysicalPage) -> finn_kernel::memory::PageRange {
    finn_kernel::memory::PageRange::new(page.start_address(), 1).expect("validated page")
}

fn draw(info: &BootInfo) {
    let format = info.framebuffer.pixel_format;
    let base = info.framebuffer.address as *mut u8;
    for y in 0..info.framebuffer.height {
        for x in 0..info.framebuffer.width {
            let color = if y < info.framebuffer.height / 5 {
                encode_pixel(format, 35, 75, 115)
            } else {
                encode_pixel(format, 10, 15, 25)
            };
            if let (Some(offset), Some(pixel)) = (
                pixel_offset(
                    x,
                    y,
                    info.framebuffer.width,
                    info.framebuffer.height,
                    info.framebuffer.stride,
                    info.framebuffer.byte_len,
                ),
                color,
            ) {
                // SAFETY: BootInfo validation checked the 32-bit pixel range; each write is volatile within the GOP buffer.
                unsafe {
                    core::ptr::write_volatile(base.add(offset).cast::<u32>(), pixel);
                }
            }
        }
    }
}

fn failure() -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:FAILURE\n");
    qemu::exit(0x11)
}

/// Return the top of the early kernel stack set up by `_start`.
#[allow(unsafe_code)]
fn current_stack_top() -> u64 {
    // SAFETY: `__stack_top` is defined by the linker script and marks the top of the
    // boot-allocated kernel stack.
    unsafe extern "C" {
        static __stack_top: u8;
    }
    unsafe { &__stack_top as *const u8 as u64 }
}

/// Return the bottom of the early kernel stack defined by the linker.
#[allow(unsafe_code)]
fn current_stack_bottom() -> u64 {
    unsafe extern "C" {
        static __stack_bottom: u8;
    }
    unsafe { &__stack_bottom as *const u8 as u64 }
}

#[panic_handler]
fn panic(info: &PanicInfo<'_>) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PANIC {}\n", info);
    qemu::exit(0x11)
}
