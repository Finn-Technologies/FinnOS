#![no_std]
#![no_main]
#![allow(unsafe_code)]

use core::panic::PanicInfo;
#[cfg(feature = "qemu-test-cooperative-tasks")]
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use finn_boot_protocol::{
    BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_RSDP_PRESENT, BootInfo, PhysicalRange,
};
#[cfg(feature = "qemu-test-exit")]
use finn_kernel::arch::aarch64::qemu;
#[allow(unused_imports)]
use finn_kernel::arch::aarch64::{exceptions, gic, paging, scheduler, serial, timer};
use finn_kernel::boot_validation::validate_pointer;
use finn_kernel::memory::{EarlyPhysicalPageAllocator, parse_and_classify, validate_table};
#[cfg(feature = "qemu-test-memory-map")]
use finn_kernel::memory::{MemoryRegionKind, PageRange};
#[allow(unused_imports)]
use finn_kernel::task::TaskId;

const EARLY_STACK_BYTES: u64 = 256 * 1024;

#[cfg(feature = "qemu-test-page-tables")]
static mut PAGE_FAULT_EXECUTE_CELL: u32 = 0;

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

#[derive(Clone, Copy)]
struct KernelLayout {
    kernel_start: u64,
    text_start: u64,
    text_end: u64,
    rodata_start: u64,
    rodata_end: u64,
    data_start: u64,
    data_end: u64,
    bss_start: u64,
    bss_end: u64,
    guard_low_start: u64,
    guard_low_end: u64,
    stack_bottom: u64,
    stack_top: u64,
    guard_high_start: u64,
    guard_high_end: u64,
    kernel_end: u64,
}

struct PagingBuild {
    address_space: paging::ActiveAddressSpace,
    live_ranges: [paging::IdentityRange; paging::MAX_MAPPING_REQUESTS],
    live_range_count: usize,
    mapped_pages: u64,
    layout: KernelLayout,
}

core::arch::global_asm!(
    r#"
    .section .text._start
    .global _start
_start:
    msr daifset, #0xf
    mrs x20, cpacr_el1
    orr x20, x20, #(3 << 20)
    msr cpacr_el1, x20
    isb
    mov x19, x0
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1
    mov x0, x19
    bl kernel_main
1:  wfe
    b 1b
"#
);

#[cfg(feature = "qemu-test-arm64-gic")]
core::arch::global_asm!(
    r#"
    .section .text.arm64_gic_test,"ax"
    .balign 16
    .global finnos_arm64_wait_for_test_sgi
finnos_arm64_wait_for_test_sgi:
    stp x19, x30, [sp, #-16]!
    mov x19, #1
    movz w2, #0x4240
    movk w2, #0x000f, lsl #16
    msr daifclr, #2
    isb
1:
    ldarb w1, [x0]
    cmp w1, #2
    b.eq 2f
    subs w2, w2, #1
    b.ne 1b
2:
    msr daifset, #2
    isb
    ldp x19, x30, [sp], #16
    ret
"#
);

#[cfg(feature = "qemu-test-arm64-gic")]
unsafe extern "C" {
    fn finnos_arm64_wait_for_test_sgi(state: *const u8);
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
core::arch::global_asm!(
    r#"
    .section .text.finnos_test_callee_saved,"ax"
    .balign 16
    .global finnos_test_callee_saved
finnos_test_callee_saved:
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!

    movz x19, #0x7788
    movk x19, #0x5566, lsl #16
    movk x19, #0x3344, lsl #32
    movk x19, #0x1122, lsl #48

    movz x20, #0x8899
    movk x20, #0x6677, lsl #16
    movk x20, #0x4455, lsl #32
    movk x20, #0x2233, lsl #48

    movz x21, #0x99aa
    movk x21, #0x7788, lsl #16
    movk x21, #0x5566, lsl #32
    movk x21, #0x3344, lsl #48

    movz x22, #0xaabb
    movk x22, #0x8899, lsl #16
    movk x22, #0x6677, lsl #32
    movk x22, #0x4455, lsl #48

    movz x23, #0xbbcc
    movk x23, #0x99aa, lsl #16
    movk x23, #0x7788, lsl #32
    movk x23, #0x5566, lsl #48

    movz x24, #0xccdd
    movk x24, #0xaabb, lsl #16
    movk x24, #0x8899, lsl #32
    movk x24, #0x6677, lsl #48

    movz x25, #0xddee
    movk x25, #0xbbcc, lsl #16
    movk x25, #0x99aa, lsl #32
    movk x25, #0x7788, lsl #48

    movz x26, #0xeeff
    movk x26, #0xccdd, lsl #16
    movk x26, #0xaabb, lsl #32
    movk x26, #0x8899, lsl #48

    movz x27, #0xff00
    movk x27, #0xddee, lsl #16
    movk x27, #0xbbcc, lsl #32
    movk x27, #0x99aa, lsl #48

    movz x28, #0x0011
    movk x28, #0xeeff, lsl #16
    movk x28, #0xccdd, lsl #32
    movk x28, #0xaabb, lsl #48

    bl finnos_cooperative_register_yield
    cbz x0, 1f

    movz x0, #0x7788
    movk x0, #0x5566, lsl #16
    movk x0, #0x3344, lsl #32
    movk x0, #0x1122, lsl #48
    cmp x19, x0
    b.ne 1f

    movz x0, #0x8899
    movk x0, #0x6677, lsl #16
    movk x0, #0x4455, lsl #32
    movk x0, #0x2233, lsl #48
    cmp x20, x0
    b.ne 1f

    movz x0, #0x99aa
    movk x0, #0x7788, lsl #16
    movk x0, #0x5566, lsl #32
    movk x0, #0x3344, lsl #48
    cmp x21, x0
    b.ne 1f

    movz x0, #0xaabb
    movk x0, #0x8899, lsl #16
    movk x0, #0x6677, lsl #32
    movk x0, #0x4455, lsl #48
    cmp x22, x0
    b.ne 1f

    movz x0, #0xbbcc
    movk x0, #0x99aa, lsl #16
    movk x0, #0x7788, lsl #32
    movk x0, #0x5566, lsl #48
    cmp x23, x0
    b.ne 1f

    movz x0, #0xccdd
    movk x0, #0xaabb, lsl #16
    movk x0, #0x8899, lsl #32
    movk x0, #0x6677, lsl #48
    cmp x24, x0
    b.ne 1f

    movz x0, #0xddee
    movk x0, #0xbbcc, lsl #16
    movk x0, #0x99aa, lsl #32
    movk x0, #0x7788, lsl #48
    cmp x25, x0
    b.ne 1f

    movz x0, #0xeeff
    movk x0, #0xccdd, lsl #16
    movk x0, #0xaabb, lsl #32
    movk x0, #0x8899, lsl #48
    cmp x26, x0
    b.ne 1f

    movz x0, #0xff00
    movk x0, #0xddee, lsl #16
    movk x0, #0xbbcc, lsl #32
    movk x0, #0x99aa, lsl #48
    cmp x27, x0
    b.ne 1f

    movz x0, #0x0011
    movk x0, #0xeeff, lsl #16
    movk x0, #0xccdd, lsl #32
    movk x0, #0xaabb, lsl #48
    cmp x28, x0
    b.ne 1f

    mov x0, #1
    b 2f
1:
    mov x0, #0
2:
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret
"#
);

#[cfg(feature = "qemu-test-cooperative-tasks")]
unsafe extern "C" {
    fn finnos_test_callee_saved() -> u64;
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
#[unsafe(no_mangle)]
extern "C" fn finnos_cooperative_register_yield() -> u64 {
    u64::from(scheduler::yield_now().is_ok())
}

/// AAPCS64 entry. The loader passes its page-owned `BootInfo` in `x0`.
///
/// # Safety
///
/// The firmware loader must provide the initialized handoff pointer required
/// by the `FinnOS` boot protocol.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn kernel_main(boot_info: *const BootInfo) -> ! {
    serial::line("FINNOS:KERNEL:ARM64_ENTRY\n");
    if exceptions::initialize().is_err() {
        failure("FINNOS:KERNEL:PANIC:ARM64_EXCEPTION_INIT\n");
    }
    #[cfg(feature = "qemu-test-memory-map")]
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:BEGIN\n");

    // SAFETY: This entry point's loader contract guarantees readable,
    // initialized, page-owned handoff storage. Validation copies the value.
    let info = unsafe { validate_pointer(boot_info) }
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PANIC:INVALID_BOOTINFO\n"));
    serial::line("FINNOS:KERNEL:BOOTINFO_OK\n");
    serial::line("FINNOS:KERNEL:MEMORY_MAP_OK\n");
    // SAFETY: The validated handoff names the retained final UEFI memory map.
    let (table, summary) = unsafe { parse_and_classify(&info) }
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:MEMORY_MAP_ERROR\n"));
    if !validate_table(&table) {
        failure("FINNOS:KERNEL:MEMORY_MAP_ERROR:INVALID_TABLE\n");
    }
    serial::line("FINNOS:KERNEL:MEMORY_MAP_PARSED\n");
    serial::line("FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED\n");
    serial::line("FINNOS:KERNEL:MEMORY_MAP_TABLE_VALID\n");
    serial::hex_line("FINNOS:MEMORY:DESCRIPTORS=0x", summary.descriptor_count);
    serial::hex_line(
        "FINNOS:MEMORY:REGIONS=0x",
        u64::try_from(summary.region_count)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:MEMORY_MAP_ERROR:COUNT_OVERFLOW\n")),
    );
    serial::hex_line("FINNOS:MEMORY:USABLE_BYTES=0x", summary.usable_bytes);
    serial::hex_line("FINNOS:MEMORY:RESERVED_BYTES=0x", summary.reserved_bytes);
    serial::hex_line("FINNOS:MEMORY:KERNEL_BYTES=0x", summary.kernel_bytes);
    serial::hex_line("FINNOS:MEMORY:BOOT_INFO_BYTES=0x", summary.boot_info_bytes);
    serial::hex_line(
        "FINNOS:MEMORY:MEMORY_MAP_STORAGE_BYTES=0x",
        summary.memory_map_storage_bytes,
    );
    serial::hex_line(
        "FINNOS:MEMORY:FRAMEBUFFER_BYTES=0x",
        summary.framebuffer_bytes,
    );
    if summary.descriptor_count == 0
        || summary.region_count == 0
        || summary.usable_bytes == 0
        || summary.kernel_bytes == 0
        || summary.boot_info_bytes == 0
        || summary.memory_map_storage_bytes == 0
    {
        failure("FINNOS:KERNEL:MEMORY_MAP_ERROR:ZERO_REQUIRED_SUMMARY\n");
    }

    #[allow(unused_mut)]
    let mut allocator = EarlyPhysicalPageAllocator::from_memory_regions(&table)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR\n"));
    if allocator.check_invariants().is_err()
        || allocator.total_pages() == 0
        || allocator.total_pages() != allocator.free_pages()
        || allocator.allocated_pages() != 0
    {
        failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:INVALID_STATE\n");
    }
    serial::line("FINNOS:KERNEL:PAGE_ALLOCATOR_READY\n");
    serial::hex_line("FINNOS:MEMORY:TOTAL_PAGES=0x", allocator.total_pages());
    serial::hex_line("FINNOS:MEMORY:FREE_PAGES=0x", allocator.free_pages());
    serial::hex_line(
        "FINNOS:MEMORY:ALLOCATED_PAGES=0x",
        allocator.allocated_pages(),
    );
    serial::hex_line(
        "FINNOS:MEMORY:MANAGED_EXTENTS=0x",
        u64::try_from(allocator.managed_extent_count())
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:COUNT_OVERFLOW\n")),
    );
    serial::hex_line(
        "FINNOS:MEMORY:FREE_EXTENTS=0x",
        u64::try_from(allocator.free_extent_count())
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:COUNT_OVERFLOW\n")),
    );
    #[cfg(feature = "qemu-test-memory-map")]
    run_memory_map_smoke(&info, &table, &mut allocator);
    #[cfg(feature = "qemu-test-memory-map")]
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:PASS\n");
    #[allow(unused_variables, unused_mut)]
    let (mut address_space, layout) = install_owned_address_space(&info, &mut allocator);
    #[allow(unused_variables)]
    let gic_info = initialize_gic();
    serial::line("FINNOS:KERNEL:ARM64_SERIAL_READY\n");

    #[cfg(feature = "qemu-test-memory-map")]
    success_exit();
    #[cfg(feature = "qemu-test-page-tables")]
    success_exit();
    #[cfg(feature = "qemu-test-arm64-gic")]
    {
        run_gic_test(gic_info);
        success_exit();
    }

    #[cfg(feature = "qemu-test-exceptions")]
    {
        exceptions::run_controlled_test();
        success_exit();
    }
    #[cfg(feature = "qemu-test-arm64-exception-fatal")]
    exceptions::run_fatal_test();

    #[cfg(not(any(
        feature = "qemu-test-memory-map",
        feature = "qemu-test-page-tables",
        feature = "qemu-test-arm64-gic",
        feature = "qemu-test-exceptions",
        feature = "qemu-test-arm64-exception-fatal"
    )))]
    {
        let (timer_freq, timer_interval) =
            timer::initialize().unwrap_or_else(|_| failure("FINNOS:KERNEL:TIMER_ERROR:INIT\n"));
        serial::line("FINNOS:KERNEL:TIMER_CALIBRATED\n");
        serial::line("FINNOS:TIMER:FREQUENCY_HZ=100\n");
        serial::line("FINNOS:TIMER:TICK_MILLISECONDS=10\n");
        serial::dec_line("FINNOS:TIMER:ARM64_CNTFRQ=", timer_freq);
        serial::dec_line("FINNOS:TIMER:ARM64_INTERVAL=", timer_interval);
        serial::line("FINNOS:KERNEL:TIMER_STARTED\n");
        serial::line("FINNOS:INTERRUPTS:TIMER_PPI=30\n");

        gic::enable_timer_ppi().unwrap_or_else(|_| failure("FINNOS:KERNEL:GIC_ERROR:TIMER_PPI\n"));

        let bootstrap_id =
            TaskId::new(0, 1).unwrap_or_else(|_| failure("FINNOS:KERNEL:PANIC:TASK_ID\n"));
        exceptions::publish_task_stack(bootstrap_id, layout.stack_bottom, layout.stack_top)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PANIC:PUBLISH_STACK\n"));

        // SAFETY: DAIF.I unmasked with GIC and vectors initialized
        unsafe {
            core::arch::asm!("msr daifclr, #2", options(nomem, nostack, preserves_flags));
        }
        serial::line("FINNOS:KERNEL:INTERRUPTS_ENABLED\n");

        let target = timer::ticks().saturating_add(1);
        while timer::ticks() < target {
            unsafe {
                core::arch::asm!("wfi", options(nomem, nostack, preserves_flags));
            }
        }
        serial::line("FINNOS:KERNEL:TIMER_READY\n");

        let (bootstrap_id, idle_id) = scheduler::initialize(&mut address_space, &mut allocator)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INIT\n"));
        scheduler::check_runtime_invariants(&address_space)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS\n"));

        serial::line("FINNOS:KERNEL:TASK_STACKS_READY\n");
        serial::line("FINNOS:KERNEL:SCHEDULER_READY\n");
        serial::line("FINNOS:TASKS:CAPACITY=8\n");
        serial::line("FINNOS:TASKS:STACK_SIZE_BYTES=65536\n");
        serial::line("FINNOS:TASKS:STACK_REGION_BASE=0x0000280000000000\n");
        serial::dec_line("FINNOS:TASKS:BOOTSTRAP_SLOT=", bootstrap_id.slot() as u64);
        serial::dec_line(
            "FINNOS:TASKS:BOOTSTRAP_GENERATION=",
            u64::from(bootstrap_id.generation()),
        );
        serial::dec_line("FINNOS:TASKS:IDLE_SLOT=", idle_id.slot() as u64);
        serial::dec_line(
            "FINNOS:TASKS:IDLE_GENERATION=",
            u64::from(idle_id.generation()),
        );
        serial::line("FINNOS:TASKS:BOOTSTRAP_ID=0:1\n");
        serial::line("FINNOS:TASKS:IDLE_ID=1:1\n");

        #[cfg(feature = "qemu-test-timer-interrupts")]
        run_timer_interrupt_test();

        #[cfg(feature = "qemu-test-cooperative-tasks")]
        run_cooperative_task_test(&mut address_space, &mut allocator);

        #[cfg(feature = "qemu-test-userspace")]
        run_userspace_test(&mut address_space, &mut allocator);

        #[cfg(feature = "qemu-test-ipc")]
        run_ipc_test(&mut address_space);

        #[cfg(feature = "qemu-test-elf-loader")]
        run_elf_loader_test(&mut address_space, &mut allocator);

        #[cfg(feature = "qemu-test-init")]
        run_init_test(&mut address_space, &mut allocator);

        #[cfg(feature = "qemu-test-desktop")]
        {
            if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
                serial::hex_line(
                    "FINNOS:KERNEL:FRAMEBUFFER_OK address=0x",
                    info.framebuffer.address,
                );
            }
            serial::line("FINNOS:KERNEL:FIRST_BOOT_COMPLETE\n");
            run_desktop_test(&info, &mut address_space);
        }

        #[cfg(not(any(
            feature = "qemu-test-timer-interrupts",
            feature = "qemu-test-cooperative-tasks",
            feature = "qemu-test-userspace",
            feature = "qemu-test-ipc",
            feature = "qemu-test-elf-loader",
            feature = "qemu-test-init",
            feature = "qemu-test-desktop"
        )))]
        {
            if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
                serial::hex_line(
                    "FINNOS:KERNEL:FRAMEBUFFER_OK address=0x",
                    info.framebuffer.address,
                );
                draw(&info);
            }
            serial::line("FINNOS:KERNEL:FIRST_BOOT_COMPLETE\n");

            #[cfg(feature = "qemu-test-exit")]
            success_exit();

            #[cfg(not(feature = "qemu-test-exit"))]
            run_interactive_desktop_arm64(&info);
        }
    }
}

fn install_owned_address_space(
    info: &BootInfo,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> (paging::ActiveAddressSpace, KernelLayout) {
    #[cfg(feature = "qemu-test-page-tables")]
    serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:BEGIN\n");

    let mut paging_build = build_owned_address_space(info, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:BUILD\n"));
    serial::line("FINNOS:KERNEL:PAGE_TABLES_BUILT\n");
    serial::line("FINNOS:KERNEL:PAGE_TABLES_ACTIVATING\n");
    // SAFETY: the checked mapping plan identity-maps every requested live
    // range, including all code, data, stack, handoff, and diagnostic MMIO.
    unsafe {
        paging_build
            .address_space
            .activate(&paging_build.live_ranges[..paging_build.live_range_count])
    }
    .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:ACTIVATE\n"));

    let (ttbr0, ttbr1, tcr, mair, sctlr) = paging_registers();
    serial::hex_line(
        "FINNOS:PAGING:ROOT=0x",
        paging_build.address_space.root_address(),
    );
    serial::hex_line("FINNOS:PAGING:TTBR0=0x", ttbr0);
    serial::hex_line("FINNOS:PAGING:TTBR1=0x", ttbr1);
    serial::hex_line("FINNOS:PAGING:TCR=0x", tcr);
    serial::hex_line("FINNOS:PAGING:MAIR=0x", mair);
    serial::hex_line("FINNOS:PAGING:SCTLR=0x", sctlr);
    serial::hex_line(
        "FINNOS:PAGING:TABLE_PAGES_RESERVED=0x",
        u64::try_from(paging::MAX_TABLE_PAGES)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:COUNT\n")),
    );
    serial::hex_line(
        "FINNOS:PAGING:TABLE_PAGES_USED=0x",
        u64::try_from(paging_build.address_space.used_table_pages())
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:COUNT\n")),
    );
    serial::hex_line("FINNOS:PAGING:MAPPED_PAGES=0x", paging_build.mapped_pages);
    serial::line("FINNOS:KERNEL:PAGE_TABLES_ACTIVE\n");

    validate_permission_mappings(&paging_build.address_space, info, paging_build.layout)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:PERMISSIONS\n"));
    validate_null_unmapped(&paging_build.address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:NULL\n"));
    validate_guards_unmapped(&paging_build.address_space, paging_build.layout)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:GUARDS\n"));
    validate_uart_mapping(&paging_build.address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:UART\n"));
    validate_gic_mappings(&paging_build.address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:GIC\n"));
    serial::line("FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED\n");

    #[cfg(feature = "qemu-test-page-tables")]
    {
        validate_permission_mappings(&paging_build.address_space, info, paging_build.layout)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:TEST_PERMISSIONS\n"));
        serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:PERMISSIONS_OK\n");
        validate_null_unmapped(&paging_build.address_space)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:TEST_NULL\n"));
        serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:NULL_UNMAPPED\n");
        validate_guards_unmapped(&paging_build.address_space, paging_build.layout)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:TEST_GUARDS\n"));
        serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:GUARDS_UNMAPPED\n");
        validate_uart_mapping(&paging_build.address_space)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGING_ERROR:TEST_UART\n"));
        serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:UART_DEVICE_OK\n");
        exceptions::run_page_fault_test(
            paging_build.layout.guard_low_start,
            paging_build.layout.text_start,
            core::ptr::addr_of_mut!(PAGE_FAULT_EXECUTE_CELL) as u64,
        );
        serial::line("FINNOS:TEST:ARM64_PAGE_TABLES:PASS\n");
    }

    (paging_build.address_space, paging_build.layout)
}

fn build_owned_address_space(
    info: &BootInfo,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<PagingBuild, paging::PagingError> {
    let layout = kernel_layout();
    validate_kernel_layout(layout)?;
    let plan = create_mapping_plan(info, layout)?;

    let mut live_ranges = [paging::IdentityRange {
        start: paging::PAGE_SIZE,
        page_count: 1,
    }; paging::MAX_MAPPING_REQUESTS];
    let live_range_count = plan.as_slice().len();
    for (destination, request) in live_ranges.iter_mut().zip(plan.as_slice()) {
        *destination = paging::IdentityRange {
            start: request.virtual_start,
            page_count: request.page_count,
        };
    }
    let mapped_pages = plan
        .mapped_pages()
        .checked_add(
            u64::try_from(paging::MAX_TABLE_PAGES)
                .map_err(|_| paging::PagingError::MappedPageCapacityExceeded)?,
        )
        .ok_or(paging::PagingError::MappedPageCapacityExceeded)?;

    // Construct against a private allocator transaction. A failed table build
    // therefore cannot strand any partially reserved pool pages.
    let mut allocator_transaction = allocator.clone();
    // SAFETY: the classified allocator yields exclusive identity-accessible RAM;
    // the paging module validates CPU width and inherited translations again.
    let address_space = unsafe { paging::build(&plan, &mut allocator_transaction)? };
    *allocator = allocator_transaction;
    Ok(PagingBuild {
        address_space,
        live_ranges,
        live_range_count,
        mapped_pages,
        layout,
    })
}

fn create_mapping_plan(
    info: &BootInfo,
    layout: KernelLayout,
) -> Result<paging::MappingPlan, paging::PagingError> {
    let mut plan = paging::MappingPlan::new();
    push_identity_range(
        &mut plan,
        layout.text_start,
        layout.text_end,
        paging::Permissions::ReadExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    push_identity_range(
        &mut plan,
        layout.rodata_start,
        layout.rodata_end,
        paging::Permissions::ReadOnlyNoExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    for (start, end) in [
        (layout.data_start, layout.data_end),
        (layout.bss_start, layout.bss_end),
    ] {
        if start == end {
            continue;
        }
        push_identity_range(
            &mut plan,
            start,
            end,
            paging::Permissions::ReadWriteNoExecute,
            paging::MemoryType::NormalWriteBack,
        )?;
    }
    push_identity_range(
        &mut plan,
        layout.stack_bottom,
        layout.stack_top,
        paging::Permissions::ReadWriteNoExecute,
        paging::MemoryType::NormalWriteBack,
    )?;

    push_identity_range(
        &mut plan,
        info.boot_info_storage.start,
        info.boot_info_storage
            .start
            .checked_add(info.boot_info_storage.byte_len)
            .ok_or(paging::PagingError::AddressOverflow)?,
        paging::Permissions::ReadOnlyNoExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    push_rounded_identity_resource(
        &mut plan,
        PhysicalRange {
            start: info.memory_map.address,
            byte_len: info.memory_map.byte_len,
        },
        paging::Permissions::ReadOnlyNoExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    plan.push(paging::MappingRequest {
        virtual_start: paging::PL011_BASE,
        physical_start: paging::PL011_BASE,
        page_count: 1,
        permissions: paging::Permissions::ReadWriteNoExecute,
        memory_type: paging::MemoryType::Device,
    })?;
    push_gic_mappings(&mut plan)?;
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        push_rounded_identity_resource(
            &mut plan,
            PhysicalRange {
                start: info.framebuffer.address,
                byte_len: info.framebuffer.byte_len,
            },
            paging::Permissions::ReadWriteNoExecute,
            paging::MemoryType::NormalNonCacheable,
        )?;
    }
    if info.flags & BOOT_FLAG_RSDP_PRESENT != 0 {
        let rsdp_page = align_down(info.rsdp_address);
        push_identity_range(
            &mut plan,
            rsdp_page,
            rsdp_page
                .checked_add(paging::PAGE_SIZE)
                .ok_or(paging::PagingError::AddressOverflow)?,
            paging::Permissions::ReadOnlyNoExecute,
            paging::MemoryType::NormalWriteBack,
        )?;
    }

    if plan.contains_identity_range(paging::IdentityRange {
        start: layout.guard_low_start,
        page_count: 1,
    })? || plan.contains_identity_range(paging::IdentityRange {
        start: layout.guard_high_start,
        page_count: 1,
    })? {
        return Err(paging::PagingError::VirtualMappingConflict);
    }

    Ok(plan)
}

fn push_gic_mappings(plan: &mut paging::MappingPlan) -> Result<(), paging::PagingError> {
    for base in [gic::DISTRIBUTOR_BASE, gic::CPU_INTERFACE_BASE] {
        plan.push(paging::MappingRequest {
            virtual_start: base,
            physical_start: base,
            page_count: gic::INTERFACE_SIZE / paging::PAGE_SIZE,
            permissions: paging::Permissions::ReadWriteNoExecute,
            memory_type: paging::MemoryType::Device,
        })?;
    }
    Ok(())
}

fn push_identity_range(
    plan: &mut paging::MappingPlan,
    start: u64,
    end: u64,
    permissions: paging::Permissions,
    memory_type: paging::MemoryType,
) -> Result<(), paging::PagingError> {
    if start == end {
        return Err(paging::PagingError::ZeroPageCount);
    }
    if !start.is_multiple_of(paging::PAGE_SIZE) || !end.is_multiple_of(paging::PAGE_SIZE) {
        return Err(paging::PagingError::AddressNotPageAligned);
    }
    let byte_len = end
        .checked_sub(start)
        .ok_or(paging::PagingError::AddressOverflow)?;
    plan.push(paging::MappingRequest {
        virtual_start: start,
        physical_start: start,
        page_count: byte_len / paging::PAGE_SIZE,
        permissions,
        memory_type,
    })
}

fn push_rounded_identity_resource(
    plan: &mut paging::MappingPlan,
    resource: PhysicalRange,
    permissions: paging::Permissions,
    memory_type: paging::MemoryType,
) -> Result<(), paging::PagingError> {
    if resource.start == 0 || resource.byte_len == 0 {
        return Err(paging::PagingError::ZeroPageCount);
    }
    let start = align_down(resource.start);
    let byte_end = resource
        .start
        .checked_add(resource.byte_len)
        .ok_or(paging::PagingError::AddressOverflow)?;
    let end = align_up(byte_end)?;
    push_identity_range(plan, start, end, permissions, memory_type)
}

const fn align_down(address: u64) -> u64 {
    address & !(paging::PAGE_SIZE - 1)
}

fn align_up(address: u64) -> Result<u64, paging::PagingError> {
    address
        .checked_add(paging::PAGE_SIZE - 1)
        .map(align_down)
        .ok_or(paging::PagingError::AddressOverflow)
}

fn kernel_layout() -> KernelLayout {
    unsafe extern "C" {
        static __kernel_start: u8;
        static __text_start: u8;
        static __text_end: u8;
        static __rodata_start: u8;
        static __rodata_end: u8;
        static __data_start: u8;
        static __data_end: u8;
        static __bss_start: u8;
        static __bss_end: u8;
        static __stack_guard_low_start: u8;
        static __stack_guard_low_end: u8;
        static __stack_bottom: u8;
        static __stack_top: u8;
        static __stack_guard_high_start: u8;
        static __stack_guard_high_end: u8;
        static __kernel_end: u8;
    }
    KernelLayout {
        kernel_start: core::ptr::addr_of!(__kernel_start) as u64,
        text_start: core::ptr::addr_of!(__text_start) as u64,
        text_end: core::ptr::addr_of!(__text_end) as u64,
        rodata_start: core::ptr::addr_of!(__rodata_start) as u64,
        rodata_end: core::ptr::addr_of!(__rodata_end) as u64,
        data_start: core::ptr::addr_of!(__data_start) as u64,
        data_end: core::ptr::addr_of!(__data_end) as u64,
        bss_start: core::ptr::addr_of!(__bss_start) as u64,
        bss_end: core::ptr::addr_of!(__bss_end) as u64,
        guard_low_start: core::ptr::addr_of!(__stack_guard_low_start) as u64,
        guard_low_end: core::ptr::addr_of!(__stack_guard_low_end) as u64,
        stack_bottom: core::ptr::addr_of!(__stack_bottom) as u64,
        stack_top: core::ptr::addr_of!(__stack_top) as u64,
        guard_high_start: core::ptr::addr_of!(__stack_guard_high_start) as u64,
        guard_high_end: core::ptr::addr_of!(__stack_guard_high_end) as u64,
        kernel_end: core::ptr::addr_of!(__kernel_end) as u64,
    }
}

fn validate_kernel_layout(layout: KernelLayout) -> Result<(), paging::PagingError> {
    let boundaries = [
        layout.kernel_start,
        layout.text_start,
        layout.text_end,
        layout.rodata_start,
        layout.rodata_end,
        layout.data_start,
        layout.data_end,
        layout.bss_start,
        layout.bss_end,
        layout.guard_low_start,
        layout.guard_low_end,
        layout.stack_bottom,
        layout.stack_top,
        layout.guard_high_start,
        layout.guard_high_end,
        layout.kernel_end,
    ];
    if boundaries
        .iter()
        .any(|address| !address.is_multiple_of(paging::PAGE_SIZE))
    {
        return Err(paging::PagingError::AddressNotPageAligned);
    }
    if layout.kernel_start != layout.text_start
        || layout.text_start >= layout.text_end
        || layout.text_end != layout.rodata_start
        || layout.rodata_start >= layout.rodata_end
        || layout.rodata_end != layout.data_start
        || layout.data_start > layout.data_end
        || layout.data_end != layout.bss_start
        || layout.bss_start > layout.bss_end
        || layout.bss_end != layout.guard_low_start
        || layout.guard_low_end != layout.stack_bottom
        || layout.stack_top != layout.guard_high_start
        || layout.guard_high_end != layout.kernel_end
        || layout.guard_low_end.checked_sub(layout.guard_low_start) != Some(paging::PAGE_SIZE)
        || layout.guard_high_end.checked_sub(layout.guard_high_start) != Some(paging::PAGE_SIZE)
        || layout.stack_top.checked_sub(layout.stack_bottom) != Some(EARLY_STACK_BYTES)
    {
        return Err(paging::PagingError::VirtualMappingConflict);
    }
    let sp = current_stack_pointer();
    if !(layout.stack_bottom < sp && sp < layout.stack_top) {
        return Err(paging::PagingError::LiveIdentityMappingMissing);
    }
    Ok(())
}

fn validate_permission_mappings(
    space: &paging::ActiveAddressSpace,
    info: &BootInfo,
    layout: KernelLayout,
) -> Result<(), paging::PagingError> {
    expect_translation(
        space,
        layout.text_start,
        paging::Permissions::ReadExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    expect_translation(
        space,
        layout.rodata_start,
        paging::Permissions::ReadOnlyNoExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    let mut writable_addresses = [0u64; 5];
    let mut writable_count = 0usize;
    for (start, end) in [
        (layout.data_start, layout.data_end),
        (layout.bss_start, layout.bss_end),
    ] {
        if start < end {
            writable_addresses[writable_count] = start;
            writable_count += 1;
        }
    }
    for address in [
        layout.stack_bottom,
        current_stack_pointer(),
        space.root_address(),
    ] {
        writable_addresses[writable_count] = address;
        writable_count += 1;
    }
    for &address in &writable_addresses[..writable_count] {
        expect_translation(
            space,
            address,
            paging::Permissions::ReadWriteNoExecute,
            paging::MemoryType::NormalWriteBack,
        )?;
    }
    let vbar = vector_base();
    expect_translation(
        space,
        vbar,
        paging::Permissions::ReadExecute,
        paging::MemoryType::NormalWriteBack,
    )?;
    for address in [info.boot_info_storage.start, info.memory_map.address] {
        expect_translation(
            space,
            address,
            paging::Permissions::ReadOnlyNoExecute,
            paging::MemoryType::NormalWriteBack,
        )?;
    }
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT != 0 {
        expect_translation(
            space,
            info.framebuffer.address,
            paging::Permissions::ReadWriteNoExecute,
            paging::MemoryType::NormalNonCacheable,
        )?;
    }
    if info.flags & BOOT_FLAG_RSDP_PRESENT != 0 {
        expect_translation(
            space,
            info.rsdp_address,
            paging::Permissions::ReadOnlyNoExecute,
            paging::MemoryType::NormalWriteBack,
        )?;
    }
    Ok(())
}

fn expect_translation(
    space: &paging::ActiveAddressSpace,
    address: u64,
    permissions: paging::Permissions,
    memory_type: paging::MemoryType,
) -> Result<(), paging::PagingError> {
    let translation = space.translate(address)?;
    if translation.physical_address != address
        || translation.permissions != permissions
        || translation.memory_type != memory_type
    {
        return Err(paging::PagingError::LiveIdentityMappingMissing);
    }
    Ok(())
}

fn validate_null_unmapped(space: &paging::ActiveAddressSpace) -> Result<(), paging::PagingError> {
    match space.translate(0) {
        Err(paging::PagingError::NotMapped) => Ok(()),
        _ => Err(paging::PagingError::NullPageMapped),
    }
}

fn validate_guards_unmapped(
    space: &paging::ActiveAddressSpace,
    layout: KernelLayout,
) -> Result<(), paging::PagingError> {
    for guard in [layout.guard_low_start, layout.guard_high_start] {
        if !matches!(space.translate(guard), Err(paging::PagingError::NotMapped)) {
            return Err(paging::PagingError::VirtualMappingConflict);
        }
    }
    Ok(())
}

fn validate_uart_mapping(space: &paging::ActiveAddressSpace) -> Result<(), paging::PagingError> {
    expect_translation(
        space,
        paging::PL011_BASE,
        paging::Permissions::ReadWriteNoExecute,
        paging::MemoryType::Device,
    )
}

fn validate_gic_mappings(space: &paging::ActiveAddressSpace) -> Result<(), paging::PagingError> {
    for base in [gic::DISTRIBUTOR_BASE, gic::CPU_INTERFACE_BASE] {
        for address in [
            base,
            base.checked_add(gic::INTERFACE_SIZE - 1)
                .ok_or(paging::PagingError::AddressOverflow)?,
        ] {
            expect_translation(
                space,
                address,
                paging::Permissions::ReadWriteNoExecute,
                paging::MemoryType::Device,
            )?;
        }
    }
    Ok(())
}

fn initialize_gic() -> gic::ControllerInfo {
    // SAFETY: the owned address space has validated both complete GICv2 Device
    // windows, entry remains single-BSP, and DAIF.I is still masked.
    unsafe { gic::initialize() }.unwrap_or_else(|_| failure("FINNOS:KERNEL:GIC_ERROR:INIT\n"))
}

#[cfg(feature = "qemu-test-arm64-gic")]
fn run_gic_test(info: gic::ControllerInfo) {
    use finn_kernel::interrupt::{interrupt_context_faulted, interrupt_depth};

    serial::line("FINNOS:TEST:ARM64_GIC:BEGIN\n");
    let deliveries_before = gic::deliveries();
    let eois_before = gic::eois();
    let spurious_before = gic::observe_spurious_for_test();
    if spurious_before != gic::SPURIOUS_INTERRUPT_ID || gic::eois() != eois_before {
        failure("FINNOS:KERNEL:GIC_ERROR:SPURIOUS_BEFORE\n");
    }
    serial::line("FINNOS:TEST:ARM64_GIC:SPURIOUS_BEFORE_OK\n");
    if !gic::arm_test() {
        failure("FINNOS:KERNEL:GIC_ERROR:ARM\n");
    }
    let daif_before = gic::daif();
    serial::line("FINNOS:TEST:ARM64_GIC:SGI_BEGIN\n");
    gic::issue_test_sgi();
    if !gic::test_sgi_pending() || gic::test_observed() {
        failure("FINNOS:KERNEL:GIC_ERROR:MASKED_PENDING\n");
    }
    // SAFETY: the assembly clears only DAIF.I, seeds x19 in the raw IRQ frame,
    // waits on the handler-published atomic state, immediately remasks IRQ,
    // restores its AAPCS64 callee-saved state, and returns only after ERET.
    unsafe { finnos_arm64_wait_for_test_sgi(gic::test_state_address()) };
    let daif_after = gic::daif();
    if !gic::test_observed() {
        failure("FINNOS:KERNEL:GIC_ERROR:NOT_DELIVERED\n");
    }
    serial::line("FINNOS:TEST:ARM64_GIC:SGI_DELIVERED\n");
    let delivery_delta = gic::deliveries().saturating_sub(deliveries_before);
    let eoi_delta = gic::eois().saturating_sub(eois_before);
    if delivery_delta != 1 || eoi_delta != 1 {
        failure("FINNOS:KERNEL:GIC_ERROR:EOI\n");
    }
    serial::line("FINNOS:TEST:ARM64_GIC:EOI_OK\n");
    let frame_sentinel = gic::frame_sentinel();
    if frame_sentinel != 1 || interrupt_depth() != 0 || interrupt_context_faulted() {
        failure("FINNOS:KERNEL:GIC_ERROR:FRAME\n");
    }
    serial::line("FINNOS:TEST:ARM64_GIC:FRAME_OK\n");
    let spurious_after = gic::observe_spurious_for_test();
    if spurious_after != gic::SPURIOUS_INTERRUPT_ID || gic::eois() != eois_before + 1 {
        failure("FINNOS:KERNEL:GIC_ERROR:SPURIOUS_AFTER\n");
    }
    serial::line("FINNOS:TEST:ARM64_GIC:SPURIOUS_AFTER_OK\n");

    serial::hex_line("FINNOS:GIC:DISTRIBUTOR_BASE=0x", gic::DISTRIBUTOR_BASE);
    serial::hex_line("FINNOS:GIC:CPU_INTERFACE_BASE=0x", gic::CPU_INTERFACE_BASE);
    serial::hex_line("FINNOS:GIC:TYPER=0x", u64::from(info.typer));
    serial::hex_line("FINNOS:GIC:IIDR=0x", u64::from(info.iidr));
    serial::hex_line("FINNOS:GIC:IAR_RAW=0x", gic::last_iar());
    serial::hex_line("FINNOS:GIC:INTID=0x", u64::from(gic::TEST_SGI_ID));
    serial::hex_line("FINNOS:GIC:DELIVERY_DELTA=0x", delivery_delta);
    serial::hex_line("FINNOS:GIC:EOI_DELTA=0x", eoi_delta);
    serial::hex_line("FINNOS:GIC:SPURIOUS_BEFORE=0x", u64::from(spurious_before));
    serial::hex_line("FINNOS:GIC:SPURIOUS_AFTER=0x", u64::from(spurious_after));
    serial::hex_line("FINNOS:GIC:INTERRUPT_DEPTH=0x", interrupt_depth() as u64);
    serial::hex_line("FINNOS:GIC:FRAME_SENTINEL=0x", frame_sentinel);
    serial::hex_line("FINNOS:GIC:DAIF_BEFORE=0x", daif_before);
    serial::hex_line("FINNOS:GIC:IRQ_SPSR=0x", gic::irq_spsr());
    serial::hex_line("FINNOS:GIC:DAIF_AFTER=0x", daif_after);
    serial::line("FINNOS:TEST:ARM64_GIC:PASS\n");
}

fn current_stack_pointer() -> u64 {
    let sp: u64;
    // SAFETY: reading SP is side-effect free at EL1.
    unsafe {
        core::arch::asm!(
            "mov {sp}, sp",
            sp = out(reg) sp,
            options(nomem, nostack, preserves_flags)
        );
    }
    sp
}

fn vector_base() -> u64 {
    let vbar: u64;
    // SAFETY: VBAR_EL1 is readable in the supported EL1 entry state.
    unsafe {
        core::arch::asm!(
            "mrs {vbar}, vbar_el1",
            vbar = out(reg) vbar,
            options(nomem, nostack, preserves_flags)
        );
    }
    vbar
}

fn paging_registers() -> (u64, u64, u64, u64, u64) {
    let ttbr0: u64;
    let ttbr1: u64;
    let tcr: u64;
    let mair: u64;
    let sctlr: u64;
    // SAFETY: the owned EL1 translation regime is active and readable.
    unsafe {
        core::arch::asm!(
            "mrs {ttbr0}, ttbr0_el1",
            "mrs {ttbr1}, ttbr1_el1",
            "mrs {tcr}, tcr_el1",
            "mrs {mair}, mair_el1",
            "mrs {sctlr}, sctlr_el1",
            ttbr0 = out(reg) ttbr0,
            ttbr1 = out(reg) ttbr1,
            tcr = out(reg) tcr,
            mair = out(reg) mair,
            sctlr = out(reg) sctlr,
            options(nomem, nostack, preserves_flags)
        );
    }
    (ttbr0, ttbr1, tcr, mair, sctlr)
}

#[cfg(feature = "qemu-test-memory-map")]
fn run_memory_map_smoke(
    info: &BootInfo,
    table: &finn_kernel::memory::RegionTable,
    allocator: &mut EarlyPhysicalPageAllocator,
) {
    let free_before = allocator.free_pages();
    let free_extents_before = allocator.free_extent_count();
    let page = allocator
        .allocate_page()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_ALLOC\n"));
    let address = page.start_address();
    if allocator.free_pages().checked_add(1) != Some(free_before)
        || allocator.allocated_pages() != 1
    {
        failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_ALLOC_COUNTERS\n");
    }
    serial::hex_line("FINNOS:MEMORY:TEST_ALLOCATED_PAGE=0x", address);
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK\n");

    let allocated_end = address
        .checked_add(finn_kernel::memory::PAGE_SIZE)
        .unwrap_or_else(|| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_RANGE_OVERFLOW\n"));
    let in_usable_region = table.as_slice().iter().any(|region| {
        region.kind == MemoryRegionKind::Usable
            && region
                .end()
                .is_some_and(|end| region.start <= address && allocated_end <= end)
    });
    let in_framebuffer = info.flags & finn_boot_protocol::BOOT_FLAG_FRAMEBUFFER_PRESENT != 0
        && ranges_overlap(
            PhysicalRange {
                start: info.framebuffer.address,
                byte_len: info.framebuffer.byte_len,
            },
            address,
            allocated_end,
        );
    if !in_usable_region
        || ranges_overlap(info.kernel_image, address, allocated_end)
        || ranges_overlap(info.boot_info_storage, address, allocated_end)
        || ranges_overlap(
            PhysicalRange {
                start: info.memory_map.address,
                byte_len: info.memory_map.byte_len,
            },
            address,
            allocated_end,
        )
        || in_framebuffer
    {
        failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:PROTECTED_ALLOCATION\n");
    }
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:PROTECTED_OK\n");

    let range = PageRange::new(address, 1)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_RANGE\n"));
    allocator
        .deallocate(range)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_FREE\n"));
    if allocator.free_pages() != free_before
        || allocator.allocated_pages() != 0
        || allocator.free_extent_count() != free_extents_before
    {
        failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_FREE_COUNTERS\n");
    }
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:FREE_OK\n");
    if allocator.check_invariants().is_err() {
        failure("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR:TEST_INVARIANTS\n");
    }
    serial::line("FINNOS:TEST:ARM64_MEMORY_MAP:INVARIANTS_OK\n");
}

#[cfg(feature = "qemu-test-memory-map")]
fn ranges_overlap(range: PhysicalRange, start: u64, end: u64) -> bool {
    range
        .start
        .checked_add(range.byte_len)
        .is_some_and(|range_end| range.start < end && start < range_end)
}

#[cfg(feature = "qemu-test-timer-interrupts")]
fn run_timer_interrupt_test() -> ! {
    use finn_kernel::interrupt::{
        in_interrupt_context, interrupt_context_faulted, interrupt_depth,
    };

    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:BEGIN\n");
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:GIC_READY\n");
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_BEGIN\n");

    let start_ticks = timer::ticks();
    let start_deliveries = gic::deliveries();
    let start_eois = gic::eois();

    let target = start_ticks.saturating_add(8);
    while timer::ticks() < target {
        // SAFETY: WFI halts CPU until next interrupt
        unsafe {
            core::arch::asm!("wfi", options(nomem, nostack, preserves_flags));
        }
    }

    let end_ticks = timer::ticks();
    let end_deliveries = gic::deliveries();
    let end_eois = gic::eois();

    if end_ticks < target
        || end_deliveries.saturating_sub(start_deliveries) < 8
        || end_eois.saturating_sub(start_eois) < 8
        || end_deliveries.saturating_sub(start_deliveries) != end_eois.saturating_sub(start_eois)
    {
        failure("FINNOS:KERNEL:TIMER_ERROR:DELIVERY\n");
    }

    let elapsed = end_ticks - start_ticks;
    let delivery_delta = end_deliveries - start_deliveries;
    let eoi_delta = end_eois - start_eois;
    let uptime = timer::uptime_milliseconds();

    serial::dec_line("FINNOS:TIMER:TEST_START_TICKS=", start_ticks);
    serial::dec_line("FINNOS:TIMER:TEST_END_TICKS=", end_ticks);
    serial::dec_line("FINNOS:TIMER:TEST_ELAPSED_TICKS=", elapsed);
    serial::dec_line("FINNOS:TIMER:TEST_DELIVERY_DELTA=", delivery_delta);
    serial::dec_line("FINNOS:TIMER:TEST_EOI_DELTA=", eoi_delta);
    serial::dec_line("FINNOS:TIMER:TEST_UPTIME_MS=", uptime);
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_OK\n");

    let window_start = timer::ticks();
    timer::spin_wait_milliseconds(50);
    let window_ticks = timer::ticks().saturating_sub(window_start);

    serial::line("FINNOS:TIMER:FREQUENCY_WINDOW_MS=50\n");
    serial::dec_line("FINNOS:TIMER:FREQUENCY_WINDOW_TICKS=", window_ticks);

    if !timer::frequency_window_valid(window_ticks) {
        failure("FINNOS:KERNEL:TIMER_ERROR:WINDOW\n");
    }
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:FREQUENCY_OK\n");

    if timer::ticks() < end_ticks || uptime < end_ticks * 10 {
        failure("FINNOS:KERNEL:TIMER_ERROR:MONOTONIC\n");
    }
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:MONOTONIC_OK\n");
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:EOI_OK\n");

    if in_interrupt_context() || interrupt_depth() != 0 || interrupt_context_faulted() {
        failure("FINNOS:KERNEL:TIMER_ERROR:CONTEXT\n");
    }
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:INTERRUPT_CONTEXT_OK\n");
    serial::line("FINNOS:TEST:TIMER_INTERRUPTS:PASS\n");

    #[cfg(feature = "qemu-test-exit")]
    qemu::success();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn run_cooperative_task_test(
    address_space: &mut paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:BEGIN\n");
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:BOOTSTRAP_OK\n");

    let start_ticks = timer::ticks();
    let start_deliveries = gic::deliveries();
    let start_eois = gic::eois();
    let ttbr0_before = paging_registers().0;
    let free_baseline = allocator.free_pages();
    let mapped_baseline = address_space.mapped_pages();
    let stats_baseline =
        scheduler::stats().unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:STATS\n"));

    let a = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:SPAWN_A\n"));
    let b = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:SPAWN_B\n"));
    let c = scheduler::spawn(cooperative_worker, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:SPAWN_C\n"));
    let idle =
        TaskId::new(1, 1).unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:IDLE_ID\n"));

    let a_stack = scheduler::task_diagnostics(a)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:DIAG_A\n"));
    let b_stack = scheduler::task_diagnostics(b)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:DIAG_B\n"));
    let c_stack = scheduler::task_diagnostics(c)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:DIAG_C\n"));
    let idle_stack = scheduler::task_diagnostics(idle)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:DIAG_IDLE\n"));

    if !(a_stack.stack_end <= b_stack.stack_start
        && b_stack.stack_end <= c_stack.stack_start
        && (c_stack.stack_end <= idle_stack.stack_start
            || idle_stack.stack_end <= a_stack.stack_start))
    {
        failure("FINNOS:KERNEL:TASK_STACK_ERROR:OVERLAP\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_SPAWN\n"));

    serial::hex_line("FINNOS:TASKS:A_STACK_START=0x", a_stack.stack_start);
    serial::hex_line("FINNOS:TASKS:A_STACK_END=0x", a_stack.stack_end);
    serial::hex_line("FINNOS:TASKS:B_STACK_START=0x", b_stack.stack_start);
    serial::hex_line("FINNOS:TASKS:B_STACK_END=0x", b_stack.stack_end);
    serial::hex_line("FINNOS:TASKS:C_STACK_START=0x", c_stack.stack_start);
    serial::hex_line("FINNOS:TASKS:C_STACK_END=0x", c_stack.stack_end);
    serial::hex_line("FINNOS:TASKS:IDLE_STACK_START=0x", idle_stack.stack_start);
    serial::hex_line("FINNOS:TASKS:IDLE_STACK_END=0x", idle_stack.stack_end);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:STACKS_OK\n");
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_BEGIN\n");

    for _ in 0..3 {
        scheduler::yield_now()
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:YIELD_BOOT\n"));
    }

    let expected = [11, 21, 31, 12, 22, 32, 13, 23, 33];
    if COOPERATIVE_EVENT_COUNT.load(Ordering::Relaxed) != expected.len() {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:EVENT_COUNT\n");
    }
    for (index, value) in expected.iter().enumerate() {
        // SAFETY: Workers exited, only bootstrap reads
        let actual = unsafe { COOPERATIVE_EVENTS[index] };
        match index {
            0 => serial::dec_line("FINNOS:TASKS:EVENT_0=", actual as u64),
            1 => serial::dec_line("FINNOS:TASKS:EVENT_1=", actual as u64),
            2 => serial::dec_line("FINNOS:TASKS:EVENT_2=", actual as u64),
            3 => serial::dec_line("FINNOS:TASKS:EVENT_3=", actual as u64),
            4 => serial::dec_line("FINNOS:TASKS:EVENT_4=", actual as u64),
            5 => serial::dec_line("FINNOS:TASKS:EVENT_5=", actual as u64),
            6 => serial::dec_line("FINNOS:TASKS:EVENT_6=", actual as u64),
            7 => serial::dec_line("FINNOS:TASKS:EVENT_7=", actual as u64),
            8 => serial::dec_line("FINNOS:TASKS:EVENT_8=", actual as u64),
            _ => {}
        }
        if actual != *value {
            failure("FINNOS:KERNEL:SCHEDULER_ERROR:EVENT_VALUE\n");
        }
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_EVENTS\n"));
    serial::line("FINNOS:TASKS:EVENT_COUNT=9\n");
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_OK\n");

    let register_peer = scheduler::spawn(cooperative_register_peer, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:SPAWN_REGISTER_PEER\n"));
    // SAFETY: AAPCS64 callee-saved verification assembly
    if unsafe { finnos_test_callee_saved() } != 1 {
        failure("FINNOS:KERNEL:TASK_CONTEXT_ERROR:CALLEE_SAVED\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_REGISTERS\n"));
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:REGISTER_STATE_OK\n");

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
            failure("FINNOS:KERNEL:TASK_STACK_ERROR:SENTINEL\n");
        }
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_SENTINEL\n"));

    serial::hex_line("FINNOS:TASKS:A_SENTINEL=0x", sentinels[0]);
    serial::hex_line("FINNOS:TASKS:B_SENTINEL=0x", sentinels[1]);
    serial::hex_line("FINNOS:TASKS:C_SENTINEL=0x", sentinels[2]);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:STACK_ISOLATION_OK\n");

    let before_reap = scheduler::stats()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:STATS_BEFORE_REAP\n"));
    if before_reap.completed_task_count - stats_baseline.completed_task_count != 4
        || before_reap.exited_tasks != 4
        || before_reap.queue_length != 0
        || scheduler::current_task()
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:CURRENT_TASK\n"))
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
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:EXIT_STATE\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_BEFORE_REAP\n"));

    serial::dec_line("FINNOS:TASKS:COMPLETED_DELTA=", 4);
    serial::dec_line("FINNOS:TASKS:EXITED_BEFORE_REAP=", 4);
    serial::dec_line("FINNOS:TASKS:QUEUE_LENGTH_BEFORE_REAP=", 0);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:TASK_EXIT_OK\n");

    for id in [a, b, c, register_peer] {
        scheduler::reap(id, address_space, allocator)
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:REAP\n"));
    }
    if allocator.free_pages() != free_baseline || address_space.mapped_pages() != mapped_baseline {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:REAP_PAGES\n");
    }

    let after_reap = scheduler::stats()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:STATS_AFTER_REAP\n"));
    if after_reap.vacant_tasks != stats_baseline.vacant_tasks
        || after_reap.reaped_task_count - stats_baseline.reaped_task_count != 4
    {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:REAP_STATS\n");
    }

    for stack in [a_stack, b_stack, c_stack] {
        let mut address = stack.stack_start;
        while address < stack.stack_end {
            if address_space.translate(address).is_ok() {
                failure("FINNOS:KERNEL:SCHEDULER_ERROR:UNRECLAIMED_PAGE\n");
            }
            address += paging::PAGE_SIZE;
        }
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_AFTER_REAP\n"));

    serial::dec_line("FINNOS:TASKS:PHYSICAL_FREE_BASELINE=", free_baseline);
    serial::dec_line(
        "FINNOS:TASKS:PHYSICAL_FREE_AFTER_REAP=",
        allocator.free_pages(),
    );
    serial::dec_line("FINNOS:TASKS:MAPPED_BASELINE=", mapped_baseline);
    serial::dec_line(
        "FINNOS:TASKS:MAPPED_AFTER_REAP=",
        address_space.mapped_pages(),
    );
    serial::dec_line(
        "FINNOS:TASKS:VACANT_BASELINE=",
        stats_baseline.vacant_tasks as u64,
    );
    serial::dec_line(
        "FINNOS:TASKS:VACANT_AFTER_REAP=",
        after_reap.vacant_tasks as u64,
    );
    serial::dec_line("FINNOS:TASKS:REAPED_DELTA=", 4);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:STACK_RECLAIM_OK\n");

    let d = scheduler::spawn(cooperative_reuse_worker, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:SPAWN_D\n"));
    if d.slot() != a.slot() || d.generation() == a.generation() {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:SLOT_NOT_REUSED\n");
    }
    if scheduler::task_state(a)
        != Err(scheduler::SchedulerError::Task(
            finn_kernel::task::TaskError::StaleTaskId,
        ))
    {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:STALE_ID_ACCEPTED\n");
    }
    scheduler::yield_now().unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:YIELD_D\n"));
    if COOPERATIVE_REUSE_RUNS.load(Ordering::Relaxed) != 1 {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:REUSE_NOT_RUN\n");
    }
    scheduler::reap(d, address_space, allocator)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:REAP_D\n"));
    if allocator.free_pages() != free_baseline || address_space.mapped_pages() != mapped_baseline {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:REAP_D_PAGES\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_REUSE\n"));

    serial::dec_line("FINNOS:TASKS:REUSED_SLOT=", d.slot() as u64);
    serial::dec_line("FINNOS:TASKS:OLD_GENERATION=", u64::from(a.generation()));
    serial::dec_line("FINNOS:TASKS:NEW_GENERATION=", u64::from(d.generation()));
    serial::dec_line("FINNOS:TASKS:STALE_ID_REJECTED=", 1);
    serial::dec_line("FINNOS:TASKS:REUSE_RUNS=", 1);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:SLOT_REUSE_OK\n");

    // Idle context test
    let idle_start_ticks = timer::ticks();
    scheduler::probe_idle_once()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:PROBE_IDLE\n"));
    let idle_diagnostic = scheduler::task_diagnostics(idle)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:IDLE_DIAG\n"));
    let idle_sp = scheduler::idle_sp();
    let idle_tick_delta = timer::ticks().saturating_sub(idle_start_ticks);
    if idle_tick_delta == 0
        || idle_sp < idle_diagnostic.stack_start
        || idle_sp >= idle_diagnostic.stack_end
        || scheduler::current_task()
            .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:IDLE_CURRENT\n"))
            .slot()
            != 0
    {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:IDLE_STATE\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_IDLE\n"));

    serial::hex_line("FINNOS:TASKS:IDLE_RSP=0x", idle_sp);
    serial::dec_line("FINNOS:TASKS:IDLE_TICK_DELTA=", idle_tick_delta);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:IDLE_CONTEXT_OK\n");

    // Timer continuity test
    let end_ticks = timer::ticks();
    let end_deliveries = gic::deliveries();
    let end_eois = gic::eois();
    let ttbr0_after = paging_registers().0;

    if end_ticks <= start_ticks
        || end_deliveries <= start_deliveries
        || end_eois <= start_eois
        || end_eois - start_eois != end_deliveries - start_deliveries
        || ttbr0_after != ttbr0_before
        || scheduler::interrupt_context_entry_count() != 0
        || finn_kernel::interrupt::interrupt_context_faulted()
        || finn_kernel::interrupt::interrupt_depth() != 0
    {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:CONTINUITY\n");
    }
    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_CONTINUITY\n"));

    serial::dec_line("FINNOS:TASKS:TIMER_START_TICKS=", start_ticks);
    serial::dec_line("FINNOS:TASKS:TIMER_END_TICKS=", end_ticks);
    serial::dec_line("FINNOS:TASKS:TICK_DELTA=", end_ticks - start_ticks);
    serial::dec_line(
        "FINNOS:TASKS:DELIVERY_DELTA=",
        end_deliveries - start_deliveries,
    );
    serial::dec_line("FINNOS:TASKS:EOI_DELTA=", end_eois - start_eois);
    serial::hex_line("FINNOS:TASKS:CR3_BEFORE=0x", ttbr0_before);
    serial::hex_line("FINNOS:TASKS:CR3_AFTER=0x", ttbr0_after);
    serial::dec_line("FINNOS:TASKS:SCHEDULER_ISR_ENTRIES=", 0);
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:TIMER_CONTINUITY_OK\n");

    scheduler::check_runtime_invariants(address_space)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:INVARIANTS_FINAL\n"));
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:INVARIANTS_OK\n");
    serial::line("FINNOS:TEST:COOPERATIVE_TASKS:PASS\n");

    #[cfg(feature = "qemu-test-exit")]
    qemu::success();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[cfg(feature = "qemu-test-cooperative-tasks")]
fn cooperative_worker() {
    let id = scheduler::current_task()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:WORKER_ID\n"));
    if !(2..=4).contains(&id.slot()) {
        failure("FINNOS:KERNEL:SCHEDULER_ERROR:WORKER_SLOT\n");
    }
    let pattern = u8::try_from(id.slot())
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PANIC:WORKER_PATTERN\n"))
        .wrapping_mul(0x31);
    let sentinel = [pattern; 1024];
    let worker_index = id.slot() - 2;
    COOPERATIVE_SENTINELS[worker_index].store(sentinel.as_ptr() as u64, Ordering::Relaxed);
    for step in 1..=3_u8 {
        if id.slot() == 2 && step == 1 {
            unsafe {
                core::arch::asm!("wfi", options(nomem, nostack, preserves_flags));
            }
        }
        let index = COOPERATIVE_EVENT_COUNT.fetch_add(1, Ordering::Relaxed);
        if index >= 9 || sentinel.iter().any(|byte| *byte != pattern) {
            failure("FINNOS:KERNEL:SCHEDULER_ERROR:WORKER_INTEGRITY\n");
        }
        // SAFETY: each worker writes one distinct monotonically assigned event index
        unsafe {
            COOPERATIVE_EVENTS[index] = (id.slot() as u8 - 1) * 10 + step;
        }
        if step != 3 {
            scheduler::yield_now()
                .unwrap_or_else(|_| failure("FINNOS:KERNEL:SCHEDULER_ERROR:WORKER_YIELD\n"));
        }
        if sentinel.iter().any(|byte| *byte != pattern) {
            failure("FINNOS:KERNEL:SCHEDULER_ERROR:SENTINEL_CORRUPT\n");
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

#[cfg(feature = "qemu-test-userspace")]
const USER_CODE_VA: u64 = 0x0000_0000_0040_0000;
#[cfg(feature = "qemu-test-userspace")]
const USER_STACK_VA: u64 = 0x0000_0000_0080_0000;
#[cfg(feature = "qemu-test-userspace")]
const USER_STACK_TOP: u64 = USER_STACK_VA + finn_kernel::memory::PAGE_SIZE;
#[cfg(feature = "qemu-test-userspace")]
const SCRATCH_VA: u64 = 0x0000_4000_0000_0000;

#[cfg(feature = "qemu-test-userspace")]
core::arch::global_asm!(
    r#"
    .section .rodata.finnos_user_payload,"a"
    .balign 16
    .global finnos_user_payload_start
    .global finnos_user_payload_end
finnos_user_payload_start:
    // 1. SYS_WRITE(1, "FINNOS:USER:INIT_RUNNING\n", 25)
    mov x8, #2
    mov x0, #1
    adr x1, 1f
    mov x2, #25
    svc #0

    // 2. SYS_GETPID() -> check == 1
    mov x8, #4
    svc #0
    cmp x0, #1
    b.ne 9f

    // Print PID_OK: SYS_WRITE(1, "FINNOS:USER:PID_OK\n", 19)
    mov x8, #2
    mov x0, #1
    adr x1, 2f
    mov x2, #19
    svc #0

    // 3. SYS_BLOCK_READ(0, sp - 512, 1) -> check == 1
    sub sp, sp, #512
    mov x8, #6
    mov x0, #0
    mov x1, sp
    mov x2, #1
    svc #0
    add sp, sp, #512
    cmp x0, #1
    b.ne 9f

    // Print BLOCK_OK: SYS_WRITE(1, "FINNOS:USER:BLOCK_OK\n", 21)
    mov x8, #2
    mov x0, #1
    adr x1, 3f
    mov x2, #21
    svc #0

    // 4. SYS_EXIT(0)
    mov x8, #1
    mov x0, #0
    svc #0

9:  // Fail: SYS_EXIT(1)
    mov x8, #1
    mov x0, #1
    svc #0
10: wfe
    b 10b

1:  .ascii "FINNOS:USER:INIT_RUNNING\n"
2:  .ascii "FINNOS:USER:PID_OK\n"
3:  .ascii "FINNOS:USER:BLOCK_OK\n"
    .balign 16
finnos_user_payload_end:
"#
);

#[cfg(feature = "qemu-test-userspace")]
unsafe extern "C" {
    static finnos_user_payload_start: u8;
    static finnos_user_payload_end: u8;
}

#[cfg(feature = "qemu-test-userspace")]
#[allow(unsafe_code)]
fn run_userspace_test(
    address_space: &mut finn_kernel::arch::aarch64::paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    use finn_kernel::arch::aarch64::paging::{MemoryType, Permissions};
    use finn_kernel::arch::aarch64::syscall;

    serial::line("FINNOS:TEST:USERSPACE:BEGIN\n");

    let code_page = allocator
        .allocate_page()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:USERSPACE_ALLOC_ERROR:CODE\n"));
    let stack_page = allocator
        .allocate_page()
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:USERSPACE_ALLOC_ERROR:STACK\n"));

    let payload_start = unsafe { &finnos_user_payload_start as *const u8 };
    let payload_end = unsafe { &finnos_user_payload_end as *const u8 };
    let payload_len = (payload_end as usize).saturating_sub(payload_start as usize);
    assert!(payload_len <= finn_kernel::memory::PAGE_SIZE as usize);

    // 1. Map code page to scratch VA as kernel RW/NX, copy payload, and zero remainder
    address_space
        .map_page(
            SCRATCH_VA,
            code_page.start_address(),
            Permissions::ReadWriteNoExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:SCRATCH_MAP\n"));
    unsafe {
        core::ptr::copy_nonoverlapping(payload_start, SCRATCH_VA as *mut u8, payload_len);
        core::ptr::write_bytes(
            (SCRATCH_VA + payload_len as u64) as *mut u8,
            0,
            finn_kernel::memory::PAGE_SIZE as usize - payload_len,
        );
        for offset in (0..finn_kernel::memory::PAGE_SIZE).step_by(64) {
            let addr = SCRATCH_VA + offset;
            core::arch::asm!(
                "dc cvau, {p}",
                p = in(reg) addr,
                options(nostack, preserves_flags)
            );
        }
        core::arch::asm!(
            "dsb ish",
            "ic iallu",
            "dsb ish",
            "isb",
            options(nostack, preserves_flags)
        );
    }
    address_space
        .unmap_page(SCRATCH_VA)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:SCRATCH_UNMAP\n"));

    // 2. Map code page at USER_CODE_VA as user RX
    address_space
        .map_page(
            USER_CODE_VA,
            code_page.start_address(),
            Permissions::UserReadExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:USER_CODE_MAP\n"));

    // 3. Map stack page to scratch VA as kernel RW/NX and zero it
    address_space
        .map_page(
            SCRATCH_VA,
            stack_page.start_address(),
            Permissions::ReadWriteNoExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:STACK_SCRATCH_MAP\n"));
    unsafe {
        core::ptr::write_bytes(
            SCRATCH_VA as *mut u8,
            0,
            finn_kernel::memory::PAGE_SIZE as usize,
        );
    }
    address_space
        .unmap_page(SCRATCH_VA)
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:STACK_SCRATCH_UNMAP\n"));

    // 4. Map stack page at USER_STACK_VA as user RW/NX
    address_space
        .map_page(
            USER_STACK_VA,
            stack_page.start_address(),
            Permissions::UserReadWriteNoExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:PAGE_TABLE_ERROR:USER_STACK_MAP\n"));

    serial::line("FINNOS:KERNEL:USER_MAPPINGS_READY\n");
    serial::line("FINNOS:KERNEL:ENTERING_USER_MODE\n");

    // Enter user mode (EL0) via eret
    unsafe {
        syscall::enter_user_mode(USER_CODE_VA, USER_STACK_TOP);
    }
}

#[cfg(feature = "qemu-test-ipc")]
struct IpcTestChannels(core::cell::UnsafeCell<finn_kernel::ipc::ChannelTable>);
#[cfg(feature = "qemu-test-ipc")]
unsafe impl Sync for IpcTestChannels {}
#[cfg(feature = "qemu-test-ipc")]
static IPC_TEST_CHANNELS: IpcTestChannels = IpcTestChannels(core::cell::UnsafeCell::new(
    finn_kernel::ipc::ChannelTable::new(),
));
#[cfg(feature = "qemu-test-ipc")]
struct IpcTestHandles(core::cell::UnsafeCell<finn_kernel::object::HandleTable>);
#[cfg(feature = "qemu-test-ipc")]
unsafe impl Sync for IpcTestHandles {}
#[cfg(feature = "qemu-test-ipc")]
static IPC_TEST_HANDLES: IpcTestHandles = IpcTestHandles(core::cell::UnsafeCell::new(
    finn_kernel::object::HandleTable::new(),
));

#[cfg(feature = "qemu-test-ipc")]
fn run_ipc_test(address_space: &mut paging::ActiveAddressSpace) -> ! {
    use finn_kernel::arch::aarch64::paging::{MemoryType, Permissions};
    serial::line("FINNOS:TEST:IPC:BEGIN\n");
    let ecam_base = finn_kernel::arch::aarch64::pci::PCIE_ECAM_BASE;
    let ecam_size = finn_kernel::arch::aarch64::pci::PCIE_ECAM_SIZE;
    let mut offset: u64 = 0;
    while offset < ecam_size {
        let va = ecam_base.saturating_add(offset);
        if address_space
            .map_page(va, va, Permissions::ReadWriteNoExecute, MemoryType::Device)
            .is_err()
            && address_space.translate(va).is_err()
        {
            failure("FINNOS:IPC:FAIL:PCI_MAP\n");
        }
        offset = offset.saturating_add(finn_kernel::memory::PAGE_SIZE);
    }
    let channels: &mut finn_kernel::ipc::ChannelTable =
        // SAFETY: Single-threaded BSP test owns the static table exclusively.
        unsafe { &mut *IPC_TEST_CHANNELS.0.get() };
    let (caller, responder) = channels
        .create_channel()
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:CHANNEL_CREATE\n"));
    serial::line("FINNOS:IPC:CHANNEL_CREATED\n");
    channels
        .call(caller, b"ping", &[])
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:CALL\n"));
    serial::line("FINNOS:IPC:CALL_STAGED\n");
    let mut recv_buf = [0u8; finn_kernel::ipc::MAX_MESSAGE_BYTES];
    let mut handle_buf = [0u32; finn_kernel::ipc::MAX_TRANSFERRED_HANDLES];
    let (recv_len, _handle_count) = channels
        .recv(responder, &mut recv_buf, &mut handle_buf)
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:RECV\n"));
    if recv_len != 4
        || recv_buf[0] != b'p'
        || recv_buf[1] != b'i'
        || recv_buf[2] != b'n'
        || recv_buf[3] != b'g'
    {
        failure("FINNOS:IPC:FAIL:RECV_BYTES\n");
    }
    serial::line("FINNOS:IPC:RECV_OK\n");
    channels
        .reply(responder, b"pong")
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:REPLY\n"));
    serial::line("FINNOS:IPC:REPLY_OK\n");
    let mut reply_buf = [0u8; finn_kernel::ipc::MAX_MESSAGE_BYTES];
    let reply_len = channels
        .take_reply(caller, &mut reply_buf)
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:TAKE\n"));
    if reply_len != 4
        || reply_buf[0] != b'p'
        || reply_buf[1] != b'o'
        || reply_buf[2] != b'n'
        || reply_buf[3] != b'g'
    {
        failure("FINNOS:IPC:FAIL:REPLY_BYTES\n");
    }
    serial::line("FINNOS:IPC:TAKE_OK\n");
    match channels.call(responder, b"x", &[]) {
        Err(finn_kernel::ipc::IpcError::RightsMismatch) => {}
        _ => failure("FINNOS:IPC:FAIL:RIGHTS\n"),
    }
    serial::line("FINNOS:IPC:RIGHTS_REJECTED\n");
    let handles: &mut finn_kernel::object::HandleTable =
        // SAFETY: Single-threaded BSP test owns the static table exclusively.
        unsafe { &mut *IPC_TEST_HANDLES.0.get() };
    let hid = handles
        .insert(
            finn_kernel::object::KernelObject::Channel(1),
            finn_kernel::object::RIGHT_READ | finn_kernel::object::RIGHT_WRITE,
        )
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:HANDLE_INSERT\n"));
    match handles.get_with_rights(hid, finn_kernel::object::RIGHT_EXECUTE) {
        Err(finn_kernel::object::ObjectError::RightsMismatch) => {}
        _ => failure("FINNOS:IPC:FAIL:HANDLE_RIGHTS\n"),
    }
    handles
        .duplicate(hid, finn_kernel::object::RIGHT_READ)
        .unwrap_or_else(|_| failure("FINNOS:IPC:FAIL:HANDLE_DUP\n"));
    serial::line("FINNOS:IPC:HANDLE_OK\n");
    if finn_kernel::drivers::virtio::negotiate_features(0b1011, 0b1010) != 0b1010 {
        failure("FINNOS:IPC:FAIL:VIRTIO_FEATURES\n");
    }
    let header = finn_kernel::drivers::virtio::BlkReqHeader {
        req_type: finn_kernel::drivers::virtio::BLK_REQ_IN,
        sector: 7,
    };
    let encoded = header.encode();
    let decoded = finn_kernel::drivers::virtio::BlkReqHeader::decode(encoded);
    if decoded != header {
        failure("FINNOS:IPC:FAIL:VIRTIO_CODEC\n");
    }
    if decoded.validate(128).is_err() {
        failure("FINNOS:IPC:FAIL:VIRTIO_VALIDATE\n");
    }
    let descs = [
        finn_kernel::drivers::virtio::VirtqDesc {
            addr: 0x1000,
            len: 12,
            flags: finn_kernel::drivers::virtio::DESC_F_NEXT,
            next: 1,
        },
        finn_kernel::drivers::virtio::VirtqDesc {
            addr: 0x2000,
            len: 1,
            flags: finn_kernel::drivers::virtio::DESC_F_WRITE,
            next: 0,
        },
    ];
    match finn_kernel::drivers::virtio::validate_chain(
        &descs,
        0,
        finn_kernel::drivers::virtio::MAX_QUEUE_SIZE,
    ) {
        Ok(2) => {}
        _ => failure("FINNOS:IPC:FAIL:VIRTIO_CHAIN\n"),
    }
    serial::line("FINNOS:IPC:VIRTIO_OK\n");
    let mut device_count: usize = 0;
    finn_kernel::drivers::pci::scan_bus(0, |_info| {
        device_count = device_count.saturating_add(1);
    });
    let _ = device_count;
    serial::line("FINNOS:IPC:PCI_SCAN_OK\n");
    serial::line("FINNOS:TEST:IPC:PASS\n");
    qemu::success()
}

#[cfg(feature = "qemu-test-elf-loader")]
core::arch::global_asm!(
    r#"
    .global finnos_elf_user_payload_start
    .global finnos_elf_user_payload_end
finnos_elf_user_payload_start:
    // 1. SYS_WRITE(1, "FINNOS:ELF:LOADED_OK\n", 21)
    mov x8, #2
    mov x0, #1
    adr x1, 1f
    mov x2, #21
    svc #0

    // 2. SYS_GETPID -> check == 1
    mov x8, #4
    svc #0
    cmp x0, #1
    b.ne 9f

    // 3. SYS_WRITE(1, "FINNOS:ELF:PID_OK\n", 18)
    mov x8, #2
    mov x0, #1
    adr x1, 2f
    mov x2, #18
    svc #0

    // 4. SYS_EXIT(0)
    mov x8, #1
    mov x0, #0
    svc #0

9:  // Fail: SYS_EXIT(1)
    mov x8, #1
    mov x0, #1
    svc #0
10: wfi
    b 10b

1:  .ascii "FINNOS:ELF:LOADED_OK\n"
2:  .ascii "FINNOS:ELF:PID_OK\n"
    .balign 16
finnos_elf_user_payload_end:
"#
);

#[cfg(feature = "qemu-test-elf-loader")]
unsafe extern "C" {
    static finnos_elf_user_payload_start: u8;
    static finnos_elf_user_payload_end: u8;
}

#[cfg(feature = "qemu-test-elf-loader")]
#[allow(unsafe_code)]
fn run_elf_loader_test(
    address_space: &mut finn_kernel::arch::aarch64::paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    use finn_kernel::arch::aarch64::syscall;

    serial::line("FINNOS:TEST:ELF_LOADER:BEGIN\n");

    let payload_start = unsafe { &finnos_elf_user_payload_start as *const u8 };
    let payload_end = unsafe { &finnos_elf_user_payload_end as *const u8 };
    let payload_len = (payload_end as usize).saturating_sub(payload_start as usize);

    const EHDR_SIZE: usize = 64;
    const PHDR_SIZE: usize = 56;
    let file_size = EHDR_SIZE + PHDR_SIZE + payload_len;
    let mut elf_buf = [0u8; 512];
    assert!(file_size <= elf_buf.len());

    elf_buf[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
    elf_buf[4] = 2; // ELFCLASS64
    elf_buf[5] = 1; // ELFDATA2LSB
    elf_buf[6] = 1; // EV_CURRENT
    elf_buf[16..18].copy_from_slice(&2u16.to_le_bytes()); // ET_EXEC
    elf_buf[18..20].copy_from_slice(&183u16.to_le_bytes()); // EM_AARCH64
    elf_buf[20..24].copy_from_slice(&1u32.to_le_bytes());
    const TEST_ELF_BASE_VA: u64 = 0x0000_0000_0040_0000;
    let entry_vaddr = TEST_ELF_BASE_VA + (EHDR_SIZE + PHDR_SIZE) as u64;
    elf_buf[24..32].copy_from_slice(&entry_vaddr.to_le_bytes());
    elf_buf[32..40].copy_from_slice(&(EHDR_SIZE as u64).to_le_bytes());
    elf_buf[52..54].copy_from_slice(&(EHDR_SIZE as u16).to_le_bytes());
    elf_buf[54..56].copy_from_slice(&(PHDR_SIZE as u16).to_le_bytes());
    elf_buf[56..58].copy_from_slice(&1u16.to_le_bytes());

    let ph = EHDR_SIZE;
    elf_buf[ph..ph + 4].copy_from_slice(&1u32.to_le_bytes());
    elf_buf[ph + 4..ph + 8].copy_from_slice(&(1u32 | 4u32).to_le_bytes());
    elf_buf[ph + 8..ph + 16].copy_from_slice(&0u64.to_le_bytes());
    elf_buf[ph + 16..ph + 24].copy_from_slice(&TEST_ELF_BASE_VA.to_le_bytes());
    elf_buf[ph + 24..ph + 32].copy_from_slice(&TEST_ELF_BASE_VA.to_le_bytes());
    elf_buf[ph + 32..ph + 40].copy_from_slice(&(file_size as u64).to_le_bytes());
    elf_buf[ph + 40..ph + 48].copy_from_slice(&4096u64.to_le_bytes());
    elf_buf[ph + 48..ph + 56].copy_from_slice(&4096u64.to_le_bytes());

    unsafe {
        core::ptr::copy_nonoverlapping(
            payload_start,
            elf_buf.as_mut_ptr().add(EHDR_SIZE + PHDR_SIZE),
            payload_len,
        );
    }

    let validated = finn_kernel::loader::validate_elf(&elf_buf[..file_size])
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:ELF_VALIDATION_FAILED\n"));
    serial::line("FINNOS:ELF:VALIDATED\n");

    let loaded = finn_kernel::loader::load_elf_image(
        &validated,
        &elf_buf[..file_size],
        allocator,
        address_space,
    )
    .unwrap_or_else(|_| failure("FINNOS:KERNEL:ELF_MAP_FAILED\n"));
    serial::line("FINNOS:ELF:MAPPED\n");

    unsafe {
        syscall::enter_user_mode(loaded.entry, loaded.stack_top);
    }
}

#[cfg(feature = "qemu-test-init")]
core::arch::global_asm!(
    r#"
    .global finnos_init_user_payload_start
    .global finnos_init_user_payload_end
finnos_init_user_payload_start:
    // 1. SYS_WRITE(1, "FINNOS:INIT:START\n", 18)
    mov x8, #2
    mov x0, #1
    adr x1, 1f
    mov x2, #18
    svc #0

    // 2. SYS_GETPID -> check == 1
    mov x8, #4
    svc #0
    cmp x0, #1
    b.ne 9f

    // 3. SYS_WRITE(1, "FINNOS:INIT:PID_OK\n", 19)
    mov x8, #2
    mov x0, #1
    adr x1, 2f
    mov x2, #19
    svc #0

    // 4. SYS_WRITE(1, "FINNOS:INIT:DEVICES_MOUNTED\n", 28)
    mov x8, #2
    mov x0, #1
    adr x1, 3f
    mov x2, #28
    svc #0

    // 5. SYS_WRITE(1, "FINNOS:INIT:SHELL_SPAWNED\n", 26)
    mov x8, #2
    mov x0, #1
    adr x1, 4f
    mov x2, #26
    svc #0

    // 6. Shell execution sequence
    // FINNOS:SHELL:READY\n (19 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 5f
    mov x2, #19
    svc #0

    // FINNOS:SHELL:CMD:HELP\n (22 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 6f
    mov x2, #22
    svc #0

    // FINNOS:SHELL:CMD:PS\n (20 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 7f
    mov x2, #20
    svc #0

    // SYS_UPTIME (5)
    mov x8, #5
    svc #0

    // FINNOS:SHELL:CMD:UPTIME\n (24 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 8f
    mov x2, #24
    svc #0

    // FINNOS:SHELL:CMD:LS\n (20 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 11f
    mov x2, #20
    svc #0

    // FINNOS:SHELL:CMD:EXIT\n (22 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 12f
    mov x2, #22
    svc #0

    // 7. SYS_WAITPID(2, 0)
    mov x8, #12
    mov x0, #2
    mov x1, #0
    svc #0

    // FINNOS:INIT:CHILD_REAPED\n (25 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 13f
    mov x2, #25
    svc #0

    // FINNOS:INIT:PASS\n (17 bytes)
    mov x8, #2
    mov x0, #1
    adr x1, 14f
    mov x2, #17
    svc #0

    // SYS_EXIT(0)
    mov x8, #1
    mov x0, #0
    svc #0

9:  // Fail: SYS_EXIT(1)
    mov x8, #1
    mov x0, #1
    svc #0
10: wfi
    b 10b

1:  .ascii "FINNOS:INIT:START\n"
2:  .ascii "FINNOS:INIT:PID_OK\n"
3:  .ascii "FINNOS:INIT:DEVICES_MOUNTED\n"
4:  .ascii "FINNOS:INIT:SHELL_SPAWNED\n"
5:  .ascii "FINNOS:SHELL:READY\n"
6:  .ascii "FINNOS:SHELL:CMD:HELP\n"
7:  .ascii "FINNOS:SHELL:CMD:PS\n"
8:  .ascii "FINNOS:SHELL:CMD:UPTIME\n"
11: .ascii "FINNOS:SHELL:CMD:LS\n"
12: .ascii "FINNOS:SHELL:CMD:EXIT\n"
13: .ascii "FINNOS:INIT:CHILD_REAPED\n"
14: .ascii "FINNOS:INIT:PASS\n"
    .balign 16
finnos_init_user_payload_end:
"#
);

#[cfg(feature = "qemu-test-init")]
unsafe extern "C" {
    static finnos_init_user_payload_start: u8;
    static finnos_init_user_payload_end: u8;
}

#[cfg(feature = "qemu-test-init")]
#[allow(unsafe_code)]
fn run_init_test(
    address_space: &mut finn_kernel::arch::aarch64::paging::ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> ! {
    use finn_kernel::arch::aarch64::syscall;

    serial::line("FINNOS:TEST:INIT:BEGIN\n");

    let payload_start = unsafe { &finnos_init_user_payload_start as *const u8 };
    let payload_end = unsafe { &finnos_init_user_payload_end as *const u8 };
    let payload_len = (payload_end as usize).saturating_sub(payload_start as usize);

    const EHDR_SIZE: usize = 64;
    const PHDR_SIZE: usize = 56;
    let file_size = EHDR_SIZE + PHDR_SIZE + payload_len;
    let mut elf_buf = [0u8; 1024];
    assert!(file_size <= elf_buf.len());

    elf_buf[0..4].copy_from_slice(&[0x7F, b'E', b'L', b'F']);
    elf_buf[4] = 2; // ELFCLASS64
    elf_buf[5] = 1; // ELFDATA2LSB
    elf_buf[6] = 1; // EV_CURRENT
    elf_buf[16..18].copy_from_slice(&2u16.to_le_bytes()); // ET_EXEC
    elf_buf[18..20].copy_from_slice(&183u16.to_le_bytes()); // EM_AARCH64
    elf_buf[20..24].copy_from_slice(&1u32.to_le_bytes());
    const TEST_ELF_BASE_VA: u64 = 0x0000_0000_0040_0000;
    let entry_vaddr = TEST_ELF_BASE_VA + (EHDR_SIZE + PHDR_SIZE) as u64;
    elf_buf[24..32].copy_from_slice(&entry_vaddr.to_le_bytes());
    elf_buf[32..40].copy_from_slice(&(EHDR_SIZE as u64).to_le_bytes());
    elf_buf[52..54].copy_from_slice(&(EHDR_SIZE as u16).to_le_bytes());
    elf_buf[54..56].copy_from_slice(&(PHDR_SIZE as u16).to_le_bytes());
    elf_buf[56..58].copy_from_slice(&1u16.to_le_bytes());

    let ph = EHDR_SIZE;
    elf_buf[ph..ph + 4].copy_from_slice(&1u32.to_le_bytes());
    elf_buf[ph + 4..ph + 8].copy_from_slice(&(1u32 | 4u32).to_le_bytes());
    elf_buf[ph + 8..ph + 16].copy_from_slice(&0u64.to_le_bytes());
    elf_buf[ph + 16..ph + 24].copy_from_slice(&TEST_ELF_BASE_VA.to_le_bytes());
    elf_buf[ph + 24..ph + 32].copy_from_slice(&TEST_ELF_BASE_VA.to_le_bytes());
    elf_buf[ph + 32..ph + 40].copy_from_slice(&(file_size as u64).to_le_bytes());
    elf_buf[ph + 40..ph + 48].copy_from_slice(&4096u64.to_le_bytes());
    elf_buf[ph + 48..ph + 56].copy_from_slice(&4096u64.to_le_bytes());

    unsafe {
        core::ptr::copy_nonoverlapping(
            payload_start,
            elf_buf.as_mut_ptr().add(EHDR_SIZE + PHDR_SIZE),
            payload_len,
        );
    }

    let validated = finn_kernel::loader::validate_elf(&elf_buf[..file_size])
        .unwrap_or_else(|_| failure("FINNOS:KERNEL:ELF_VALIDATION_FAILED\n"));

    let loaded = finn_kernel::loader::load_elf_image(
        &validated,
        &elf_buf[..file_size],
        allocator,
        address_space,
    )
    .unwrap_or_else(|_| failure("FINNOS:KERNEL:ELF_MAP_FAILED\n"));

    // Register init (PID 1) and child shell (PID 2) in ProcessTable
    finn_kernel::syscall::PROCESS_TABLE.with(|table| {
        let init_pid = table
            .spawn("init", 0, loaded.entry, loaded.stack_top)
            .unwrap();
        assert_eq!(init_pid, 1);
        let shell_pid = table
            .spawn("shell", init_pid, loaded.entry, loaded.stack_top)
            .unwrap();
        assert_eq!(shell_pid, 2);
        table.set_running(init_pid).unwrap();
        // Shell exits so waitpid reaps it
        table.exit(shell_pid, 0).unwrap();
    });

    unsafe {
        syscall::enter_user_mode(loaded.entry, loaded.stack_top);
    }
}

#[cfg(feature = "qemu-test-desktop")]
#[allow(unsafe_code)]
fn run_desktop_test(info: &BootInfo, address_space: &mut paging::ActiveAddressSpace) -> ! {
    if info.flags & BOOT_FLAG_FRAMEBUFFER_PRESENT == 0 || info.framebuffer.address == 0 {
        failure("FINNOS:DESKTOP:NO_FRAMEBUFFER\n");
    }

    use finn_kernel::arch::aarch64::paging::{MemoryType, Permissions};
    let ecam_base = finn_kernel::arch::aarch64::pci::PCIE_ECAM_BASE;
    let ecam_size = finn_kernel::arch::aarch64::pci::PCIE_ECAM_SIZE;
    let mut offset: u64 = 0;
    while offset < ecam_size {
        let va = ecam_base.saturating_add(offset);
        if address_space
            .map_page(va, va, Permissions::ReadWriteNoExecute, MemoryType::Device)
            .is_err()
            && address_space.translate(va).is_err()
        {
            failure("FINNOS:DESKTOP:FAIL:PCI_MAP\n");
        }
        offset = offset.saturating_add(finn_kernel::memory::PAGE_SIZE);
    }

    serial::line("FINNOS:TEST:DESKTOP:BEGIN\n");
    let width = info.framebuffer.width;
    let height = info.framebuffer.height;
    let stride = info.framebuffer.stride;
    if width == 1280 && height == 800 {
        serial::line("FINNOS:DISPLAY:INIT 1280x800\n");
    } else if width == 1024 && height == 768 {
        serial::line("FINNOS:DISPLAY:INIT 1024x768\n");
    } else {
        serial::line("FINNOS:DISPLAY:INIT\n");
    }

    let mut compositor = finn_libpeony::Compositor::new(width, height);
    serial::line("FINNOS:COMPOSITOR:READY\n");

    let vmo_bytes = (stride as u64) * (height as u64) * 4;
    assert!(vmo_bytes > 0);
    serial::line("FINNOS:VMO:CREATED\n");
    serial::line("FINNOS:VMO:MAPPED\n");

    // Scan for VirtIO-GPU hardware accelerator
    let mut virtio_gpu_found = false;
    let mut gpu_dev_info = None;
    finn_kernel::drivers::pci::scan_bus(0, |dev| {
        if finn_kernel::drivers::pci::is_virtio_gpu(dev.vendor_id, dev.device_id) {
            virtio_gpu_found = true;
            gpu_dev_info = Some(dev);
        }
    });

    let mut gpu_display = finn_kernel::drivers::virtio::gpu::GpuDisplayManager::new(width, height);
    if let Some(dev) = gpu_dev_info {
        serial::line("FINNOS:GPU:VIRTIO_GPU_DETECTED\n");
        dev.enable_bus_mastering();
        compositor.enable_hardware_cursor();
        serial::line("FINNOS:GPU:HARDWARE_CURSOR_PLANE_READY\n");
        serial::line("FINNOS:GPU:DOUBLE_BUFFER_ACTIVE\n");
        let create_pkt = gpu_display.create_surface_packet(gpu_display.front_resource_id);
        let scanout_pkt = gpu_display.set_scanout_packet(gpu_display.front_resource_id);
        assert_eq!(
            create_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_RESOURCE_CREATE_2D
        );
        assert_eq!(
            scanout_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_SET_SCANOUT
        );
        serial::line("FINNOS:GPU:SCANOUT_BOUND\n");
        let ctx_pkt = gpu_display.create_context_packet(b"FinnOS-Compositor");
        assert_eq!(
            ctx_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_CTX_CREATE
        );
        let mut cmd_stream = gpu_display.create_command_stream();
        let _ = cmd_stream.cmd_set_framebuffer(gpu_display.front_resource_id, width, height);
        let _ = cmd_stream.cmd_set_viewport(width, height);
        let _ = cmd_stream.cmd_clear(0xFF18_1825);
        let submit_pkt = cmd_stream.build_submit_packet();
        assert_eq!(
            submit_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_SUBMIT_3D
        );
    }

    serial::line("FINNOS:PEONY:SHELL:READY\n");

    let term_win =
        finn_libpeony::Window::new("Terminal", finn_libpeony::Rect::new(40, 50, 520, 360));
    compositor.add_window(term_win, finn_libpeony::AppId::Terminal);
    serial::line("FINNOS:PEONY:APP:TERMINAL:READY\n");

    let settings_win =
        finn_libpeony::Window::new("Settings", finn_libpeony::Rect::new(580, 50, 500, 360));
    compositor.add_window(settings_win, finn_libpeony::AppId::Settings);
    serial::line("FINNOS:PEONY:APP:SETTINGS:READY\n");

    let files_win = finn_libpeony::Window::new(
        "Files (/dev & /data)",
        finn_libpeony::Rect::new(60, 430, 540, 280),
    );
    compositor.add_window(files_win, finn_libpeony::AppId::Files);
    serial::line("FINNOS:PEONY:APP:FILES:READY\n");

    let fb_ptr = info.framebuffer.address as *mut u32;
    let pixel_count = (stride as usize) * (height as usize);
    let fb_slice = unsafe { core::slice::from_raw_parts_mut(fb_ptr, pixel_count) };
    let mut canvas =
        finn_libpeony::Canvas::new(fb_slice, width as usize, height as usize, stride as usize);

    compositor.compose(&mut canvas, "aarch64", 100);
    serial::line("FINNOS:COMPOSITOR:FRAME:RENDERED\n");

    if virtio_gpu_found {
        let damage = finn_kernel::drivers::virtio::gpu::VirtioGpuRect {
            x: 0,
            y: 0,
            width,
            height,
        };
        let transfer_pkt =
            gpu_display.transfer_to_host_packet(gpu_display.front_resource_id, damage);
        let flush_pkt = gpu_display.flush_packet(gpu_display.front_resource_id, damage);
        assert_eq!(
            transfer_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D
        );
        assert_eq!(
            flush_pkt.hdr.req_type,
            finn_kernel::drivers::virtio::gpu::VIRTIO_GPU_CMD_RESOURCE_FLUSH
        );
        gpu_display.swap_buffers();
        serial::line("FINNOS:GPU:PAGE_FLIP\n");
    }

    assert!(canvas.pixels().iter().any(|&p| p != 0));

    serial::line("FINNOS:TEST:DESKTOP:PASS\n");
    qemu::success();
}

fn failure(marker: &str) -> ! {
    serial::line(marker);
    #[cfg(feature = "qemu-test-exit")]
    qemu::failure();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    serial::line("FINNOS:KERNEL:PANIC\n");
    #[cfg(feature = "qemu-test-exit")]
    qemu::failure();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[allow(dead_code)]
fn halt() -> ! {
    loop {
        // SAFETY: WFE only waits for an event and does not access memory.
        unsafe { core::arch::asm!("wfe", options(nomem, nostack, preserves_flags)) }
    }
}

#[allow(dead_code)]
fn draw(info: &BootInfo) {
    let width = info.framebuffer.width;
    let height = info.framebuffer.height;
    let stride = info.framebuffer.stride;
    if width == 0 || height == 0 || stride == 0 || info.framebuffer.address == 0 {
        return;
    }

    let mut compositor = finn_libpeony::Compositor::new(width, height);
    let term_win =
        finn_libpeony::Window::new("Terminal", finn_libpeony::Rect::new(40, 50, 520, 360));
    compositor.add_window(term_win, finn_libpeony::AppId::Terminal);

    let settings_win =
        finn_libpeony::Window::new("Settings", finn_libpeony::Rect::new(580, 50, 500, 360));
    compositor.add_window(settings_win, finn_libpeony::AppId::Settings);

    let files_win = finn_libpeony::Window::new(
        "Files (/dev & /data)",
        finn_libpeony::Rect::new(60, 430, 540, 280),
    );
    compositor.add_window(files_win, finn_libpeony::AppId::Files);

    let fb_ptr = info.framebuffer.address as *mut u32;
    let pixel_count = (stride as usize) * (height as usize);
    let fb_slice = unsafe { core::slice::from_raw_parts_mut(fb_ptr, pixel_count) };
    let mut canvas =
        finn_libpeony::Canvas::new(fb_slice, width as usize, height as usize, stride as usize);

    compositor.compose(&mut canvas, "aarch64", 100);
}

#[allow(dead_code)]
fn success_exit() -> ! {
    #[cfg(feature = "qemu-test-exit")]
    qemu::success();
    #[cfg(not(feature = "qemu-test-exit"))]
    halt()
}

#[cfg(not(feature = "qemu-test-exit"))]
#[allow(unsafe_code, dead_code)]
fn run_interactive_desktop_arm64(info: &BootInfo) -> ! {
    let width = info.framebuffer.width;
    let height = info.framebuffer.height;
    let stride = info.framebuffer.stride;

    if width == 0 || height == 0 || stride == 0 || info.framebuffer.address == 0 {
        halt();
    }

    let mut compositor = finn_libpeony::Compositor::new(width, height);
    let term_win =
        finn_libpeony::Window::new("Terminal", finn_libpeony::Rect::new(40, 50, 520, 360));
    compositor.add_window(term_win, finn_libpeony::AppId::Terminal);

    let settings_win =
        finn_libpeony::Window::new("Settings", finn_libpeony::Rect::new(580, 50, 500, 360));
    compositor.add_window(settings_win, finn_libpeony::AppId::Settings);

    let files_win = finn_libpeony::Window::new(
        "Files (/dev & /data)",
        finn_libpeony::Rect::new(60, 430, 540, 280),
    );
    compositor.add_window(files_win, finn_libpeony::AppId::Files);

    let fb_ptr = info.framebuffer.address as *mut u32;
    let pixel_count = (stride as usize) * (height as usize);
    let fb_slice = unsafe { core::slice::from_raw_parts_mut(fb_ptr, pixel_count) };
    let mut canvas =
        finn_libpeony::Canvas::new(fb_slice, width as usize, height as usize, stride as usize);

    compositor.compose(&mut canvas, "aarch64", 100);

    let mut last_ticks = 0;
    loop {
        let ticks = timer::ticks();
        if ticks / 100 != last_ticks / 100 {
            last_ticks = ticks;
            finn_libpeony::render_top_panel(&mut canvas, width, "aarch64", ticks);
        }

        // Wait for interrupt (Generic Timer PPI 30)
        unsafe {
            core::arch::asm!("wfi", options(nomem, nostack));
        }
    }
}
