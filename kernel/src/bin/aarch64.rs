#![no_std]
#![no_main]
#![allow(unsafe_code)]

use core::panic::PanicInfo;
use finn_boot_protocol::{
    BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_RSDP_PRESENT, BootInfo, PhysicalRange,
};
#[cfg(feature = "qemu-test-exit")]
use finn_kernel::arch::aarch64::qemu;
use finn_kernel::arch::aarch64::{exceptions, gic, paging, serial};
use finn_kernel::boot_validation::validate_pointer;
use finn_kernel::memory::{EarlyPhysicalPageAllocator, parse_and_classify, validate_table};
#[cfg(feature = "qemu-test-memory-map")]
use finn_kernel::memory::{MemoryRegionKind, PageRange};

const EARLY_STACK_BYTES: u64 = 256 * 1024;

#[cfg(feature = "qemu-test-page-tables")]
static mut PAGE_FAULT_EXECUTE_CELL: u32 = 0;

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
    install_owned_address_space(&info, &mut allocator);
    let gic_info = initialize_gic();
    #[cfg(not(feature = "qemu-test-arm64-gic"))]
    let _ = gic_info;
    serial::line("FINNOS:KERNEL:ARM64_SERIAL_READY\n");
    #[cfg(feature = "qemu-test-arm64-gic")]
    run_gic_test(gic_info);
    #[cfg(feature = "qemu-test-exceptions")]
    exceptions::run_controlled_test();
    #[cfg(feature = "qemu-test-arm64-exception-fatal")]
    exceptions::run_fatal_test();
    #[cfg(all(
        feature = "qemu-test-exit",
        not(feature = "qemu-test-arm64-exception-fatal")
    ))]
    qemu::success();
    #[cfg(all(
        not(feature = "qemu-test-exit"),
        not(feature = "qemu-test-arm64-exception-fatal")
    ))]
    halt()
}

fn install_owned_address_space(info: &BootInfo, allocator: &mut EarlyPhysicalPageAllocator) {
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

    // Dropping the immutable handle does not release its pool; the physical
    // allocator retains all reserved table pages for the active regime.
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

#[cfg(not(feature = "qemu-test-exit"))]
fn halt() -> ! {
    loop {
        // SAFETY: WFE only waits for an event and does not access memory.
        unsafe { core::arch::asm!("wfe", options(nomem, nostack, preserves_flags)) }
    }
}
