#![no_std]
#![no_main]
#![allow(unsafe_code)]
#![allow(unreachable_code)]

use core::panic::PanicInfo;
use finn_boot_protocol::{BOOT_FLAG_FRAMEBUFFER_PRESENT, BOOT_FLAG_MEMORY_MAP_PRESENT, BootInfo};
use finn_kernel::{
    arch::x86_64::{paging, qemu},
    boot_validation::validate_pointer,
    framebuffer::{encode_pixel, pixel_offset},
    memory::{EarlyPhysicalPageAllocator, parse_and_classify},
};

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
                if unsafe { paging::activate(&address_space) }.is_err() {
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

    #[cfg(feature = "qemu-test-exceptions")]
    {
        // SAFETY: The exception foundation is initialized and the IDT is loaded.
        unsafe {
            finn_kernel::arch::x86_64::exceptions::run_exception_tests();
        }
    }

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

#[panic_handler]
fn panic(info: &PanicInfo<'_>) -> ! {
    finn_kernel::serial_log!("FINNOS:KERNEL:PANIC {}\n", info);
    qemu::exit(0x11)
}
