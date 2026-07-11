//! FinnOS-owned four-level x86-64 paging.
//!
//! The safe half of this module describes addresses, entries, permissions, and
//! deterministic mapping plans. The small unsafe half is only used by the
//! kernel binary to touch identity-mapped table pages and the CPU registers.
#![allow(missing_docs)]
#![allow(dead_code)]

use crate::memory::{EarlyPhysicalPageAllocator, PAGE_SIZE, PageAllocationError, PhysicalPage};

pub const TABLE_ENTRIES: usize = 512;
pub const MAX_PAGE_TABLE_PAGES: usize = 64;
pub const MAX_MAPPING_REQUESTS: usize = 64;
pub const SCRATCH_VIRTUAL_ADDRESS: u64 = 0x0000_4000_0000_0000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PagingError {
    UnsupportedFiveLevelPaging,
    PagingNotEnabled,
    PaeNotEnabled,
    LongModeNotActive,
    NxUnsupported,
    InvalidPhysicalAddressWidth,
    InvalidCanonicalAddress,
    AddressNotPageAligned,
    AddressOverflow,
    PageCountOverflow,
    InvalidKernelLayout,
    KernelSectionOutsideImage,
    GuardPageOverlap,
    InvalidBootInfoRange,
    InvalidMemoryMapRange,
    InvalidFramebufferRange,
    InvalidAcpiRange,
    PageTablePoolExhausted,
    PageTablePageNotAccessible,
    PageTablePageAlreadyUsed,
    InvalidPageTableEntry,
    UnexpectedHugePage,
    MappingConflict,
    AlreadyMapped,
    NotMapped,
    WritableExecutableMapping,
    UserMappingForbidden,
    RequiredMappingMissing,
    RequiredPermissionMismatch,
    Cr3ActivationFailed,
    Cr0WriteProtectFailed,
    EferNxFailed,
    NullPageMapped,
    GuardPageMapped,
    MappingPlanCapacityExceeded,
    CorruptPageTableState,
    PhysicalAddressTooWide,
    ZeroPageRange,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PhysicalAddress(u64);
impl PhysicalAddress {
    pub fn new(value: u64, width: u8) -> Result<Self, PagingError> {
        if width < 36 || width > 52 {
            return Err(PagingError::InvalidPhysicalAddressWidth);
        }
        if value & !((1u64 << width) - 1) != 0 {
            return Err(PagingError::PhysicalAddressTooWide);
        }
        Ok(Self(value))
    }
    pub const fn value(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtualAddress(u64);
impl VirtualAddress {
    pub const fn new(value: u64) -> Result<Self, PagingError> {
        if !is_canonical(value) {
            return Err(PagingError::InvalidCanonicalAddress);
        }
        Ok(Self(value))
    }
    pub const fn value(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PhysicalFrame(PhysicalAddress);
impl PhysicalFrame {
    pub fn new(value: u64, width: u8) -> Result<Self, PagingError> {
        if !value.is_multiple_of(PAGE_SIZE) {
            return Err(PagingError::AddressNotPageAligned);
        }
        Ok(Self(PhysicalAddress::new(value, width)?))
    }
    pub const fn address(self) -> u64 {
        self.0.value()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct VirtualPage(VirtualAddress);
impl VirtualPage {
    pub fn new(value: u64) -> Result<Self, PagingError> {
        if !value.is_multiple_of(PAGE_SIZE) {
            return Err(PagingError::AddressNotPageAligned);
        }
        Ok(Self(VirtualAddress::new(value)?))
    }
    pub const fn address(self) -> u64 {
        self.0.value()
    }
}

pub const fn is_canonical(address: u64) -> bool {
    let upper = address >> 48;
    upper == 0 || upper == 0xffff
}
pub const fn align_down(address: u64) -> u64 {
    address & !(PAGE_SIZE - 1)
}
pub fn align_up(address: u64) -> Result<u64, PagingError> {
    let adjusted = address
        .checked_add(PAGE_SIZE - 1)
        .ok_or(PagingError::AddressOverflow)?;
    Ok(align_down(adjusted))
}
pub const fn pml4_index(address: u64) -> usize {
    ((address >> 39) & 0x1ff) as usize
}
pub const fn pdpt_index(address: u64) -> usize {
    ((address >> 30) & 0x1ff) as usize
}
pub const fn page_directory_index(address: u64) -> usize {
    ((address >> 21) & 0x1ff) as usize
}
pub const fn page_table_index(address: u64) -> usize {
    ((address >> 12) & 0x1ff) as usize
}
pub const fn page_offset(address: u64) -> u64 {
    address & (PAGE_SIZE - 1)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MappingPermissions {
    pub writable: bool,
    pub executable: bool,
    pub user: bool,
    pub write_through: bool,
    pub cache_disable: bool,
}
impl MappingPermissions {
    pub const fn kernel_rx() -> Self {
        Self {
            writable: false,
            executable: true,
            user: false,
            write_through: false,
            cache_disable: false,
        }
    }
    pub const fn kernel_r_nx() -> Self {
        Self {
            writable: false,
            executable: false,
            user: false,
            write_through: false,
            cache_disable: false,
        }
    }
    pub const fn kernel_rw_nx() -> Self {
        Self {
            writable: true,
            executable: false,
            user: false,
            write_through: false,
            cache_disable: false,
        }
    }
    pub const fn framebuffer() -> Self {
        Self {
            writable: true,
            executable: false,
            user: false,
            write_through: true,
            cache_disable: true,
        }
    }
    pub const fn validate(self) -> Result<(), PagingError> {
        if self.user {
            return Err(PagingError::UserMappingForbidden);
        }
        if self.writable && self.executable {
            return Err(PagingError::WritableExecutableMapping);
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MappingPurpose {
    KernelText,
    KernelReadOnly,
    KernelData,
    KernelBss,
    KernelStack,
    BootInfo,
    MemoryMapStorage,
    Framebuffer,
    PageTableStorage,
    AcpiRsdp,
    TestScratch,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MappingRequest {
    pub virtual_start: u64,
    pub physical_start: u64,
    pub page_count: u64,
    pub permissions: MappingPermissions,
    pub purpose: MappingPurpose,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MappingPlan {
    requests: [MappingRequest; MAX_MAPPING_REQUESTS],
    count: usize,
}
impl MappingPlan {
    pub const fn new() -> Self {
        Self {
            requests: [MappingRequest {
                virtual_start: 0,
                physical_start: 0,
                page_count: 0,
                permissions: MappingPermissions::kernel_r_nx(),
                purpose: MappingPurpose::KernelReadOnly,
            }; MAX_MAPPING_REQUESTS],
            count: 0,
        }
    }
    pub const fn len(&self) -> usize {
        self.count
    }
    pub fn as_slice(&self) -> &[MappingRequest] {
        &self.requests[..self.count]
    }
    pub fn push(&mut self, request: MappingRequest) -> Result<(), PagingError> {
        request.permissions.validate()?;
        if request.page_count == 0 {
            return Err(PagingError::ZeroPageRange);
        }
        if !request.virtual_start.is_multiple_of(PAGE_SIZE)
            || !request.physical_start.is_multiple_of(PAGE_SIZE)
        {
            return Err(PagingError::AddressNotPageAligned);
        }
        let v_end = request
            .virtual_start
            .checked_add(
                request
                    .page_count
                    .checked_mul(PAGE_SIZE)
                    .ok_or(PagingError::PageCountOverflow)?,
            )
            .ok_or(PagingError::AddressOverflow)?;
        if !is_canonical(request.virtual_start) || !is_canonical(v_end - 1) {
            return Err(PagingError::InvalidCanonicalAddress);
        }
        if request.virtual_start == 0 {
            return Err(PagingError::NullPageMapped);
        }
        for old in self.as_slice() {
            let old_end = old.virtual_start + old.page_count * PAGE_SIZE;
            if request.virtual_start < old_end && old.virtual_start < v_end {
                let compatible = request.permissions == old.permissions
                    && request.physical_start == old.physical_start;
                if compatible {
                    return Ok(());
                }
                return Err(PagingError::MappingConflict);
            }
        }
        if self.count == MAX_MAPPING_REQUESTS {
            return Err(PagingError::MappingPlanCapacityExceeded);
        }
        self.requests[self.count] = request;
        self.count += 1;
        for i in (1..self.count).rev() {
            if self.requests[i].virtual_start < self.requests[i - 1].virtual_start {
                self.requests.swap(i, i - 1);
            }
        }
        Ok(())
    }
}
impl Default for MappingPlan {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PageTableEntry(u64);
impl PageTableEntry {
    const PRESENT: u64 = 1;
    const WRITABLE: u64 = 1 << 1;
    const USER: u64 = 1 << 2;
    const WRITE_THROUGH: u64 = 1 << 3;
    const CACHE_DISABLE: u64 = 1 << 4;
    const ACCESSED: u64 = 1 << 5;
    const DIRTY: u64 = 1 << 6;
    const HUGE: u64 = 1 << 7;
    const GLOBAL: u64 = 1 << 8;
    const NX: u64 = 1 << 63;
    pub const fn empty() -> Self {
        Self(0)
    }
    pub const fn raw(self) -> u64 {
        self.0
    }
    pub const fn is_present(self) -> bool {
        self.0 & Self::PRESENT != 0
    }
    pub const fn is_huge(self) -> bool {
        self.0 & Self::HUGE != 0
    }
    pub const fn writable(self) -> bool {
        self.0 & Self::WRITABLE != 0
    }
    pub const fn user(self) -> bool {
        self.0 & Self::USER != 0
    }
    pub const fn executable(self) -> bool {
        self.0 & Self::NX == 0
    }
    pub const fn cache_disable(self) -> bool {
        self.0 & Self::CACHE_DISABLE != 0
    }
    pub const fn write_through(self) -> bool {
        self.0 & Self::WRITE_THROUGH != 0
    }
    pub const fn address(self, width: u8) -> Result<u64, PagingError> {
        if self.0 & !((1u64 << width) - 1) & 0x000f_ffff_ffff_f000 != 0 {
            return Err(PagingError::InvalidPageTableEntry);
        }
        Ok(self.0 & 0x000f_ffff_ffff_f000)
    }
    pub const fn leaf(
        frame: PhysicalFrame,
        permissions: MappingPermissions,
    ) -> Result<Self, PagingError> {
        if permissions.user {
            return Err(PagingError::UserMappingForbidden);
        }
        if permissions.writable && permissions.executable {
            return Err(PagingError::WritableExecutableMapping);
        }
        let mut raw = Self::PRESENT
            | if permissions.writable {
                Self::WRITABLE
            } else {
                0
            }
            | if permissions.write_through {
                Self::WRITE_THROUGH
            } else {
                0
            }
            | if permissions.cache_disable {
                Self::CACHE_DISABLE
            } else {
                0
            };
        if !permissions.executable {
            raw |= Self::NX;
        }
        Ok(Self(raw | frame.address()))
    }
    pub const fn table(frame: PhysicalFrame) -> Self {
        Self(Self::PRESENT | Self::WRITABLE | frame.address())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CpuPagingInfo {
    pub physical_address_width: u8,
    pub nx_supported: bool,
    pub old_cr3: u64,
}

#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
pub fn cpu_paging_info() -> Result<CpuPagingInfo, PagingError> {
    let max = core::arch::x86_64::__cpuid(0x8000_0000).eax;
    if max < 0x8000_0001 {
        return Err(PagingError::NxUnsupported);
    }
    let nx = core::arch::x86_64::__cpuid(0x8000_0001).edx & (1 << 20) != 0;
    if !nx {
        return Err(PagingError::NxUnsupported);
    }
    if max < 0x8000_0008 {
        return Err(PagingError::InvalidPhysicalAddressWidth);
    }
    let width = (core::arch::x86_64::__cpuid(0x8000_0008).eax & 0xff) as u8;
    let cr4 = read_cr4();
    if cr4 & (1 << 12) != 0 {
        return Err(PagingError::UnsupportedFiveLevelPaging);
    }
    let cr0 = read_cr0();
    if cr0 & (1 << 31) == 0 {
        return Err(PagingError::PagingNotEnabled);
    }
    if cr4 & (1 << 5) == 0 {
        return Err(PagingError::PaeNotEnabled);
    }
    if !(36..=52).contains(&width) {
        return Err(PagingError::InvalidPhysicalAddressWidth);
    }
    Ok(CpuPagingInfo {
        physical_address_width: width,
        nx_supported: nx,
        old_cr3: read_cr3(),
    })
}
#[cfg(not(target_arch = "x86_64"))]
pub fn cpu_paging_info() -> Result<CpuPagingInfo, PagingError> {
    Err(PagingError::PagingNotEnabled)
}

#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
fn read_cr0() -> u64 {
    let value;
    unsafe {
        core::arch::asm!("mov {}, cr0", out(reg) value, options(nostack, preserves_flags));
    }
    value
}
#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
fn read_cr3() -> u64 {
    let value;
    unsafe {
        core::arch::asm!("mov {}, cr3", out(reg) value, options(nostack, preserves_flags));
    }
    value
}
#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
fn read_cr4() -> u64 {
    let value;
    unsafe {
        core::arch::asm!("mov {}, cr4", out(reg) value, options(nostack, preserves_flags));
    }
    value
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PageTablePagePool {
    pages: [u64; MAX_PAGE_TABLE_PAGES],
    reserved_count: usize,
    used_count: usize,
}
impl PageTablePagePool {
    pub const fn empty() -> Self {
        Self {
            pages: [0; MAX_PAGE_TABLE_PAGES],
            reserved_count: 0,
            used_count: 0,
        }
    }
    pub fn reserve(
        allocator: &mut EarlyPhysicalPageAllocator,
    ) -> Result<Self, PageAllocationError> {
        let mut pool = Self::empty();
        while pool.reserved_count < MAX_PAGE_TABLE_PAGES {
            let page = allocator.allocate_page()?;
            pool.pages[pool.reserved_count] = page.start_address();
            pool.reserved_count += 1;
        }
        Ok(pool)
    }
    pub const fn reserved_count(&self) -> usize {
        self.reserved_count
    }
    pub const fn used_count(&self) -> usize {
        self.used_count
    }
    pub fn take(&mut self) -> Result<PhysicalPage, PagingError> {
        if self.used_count >= self.reserved_count {
            return Err(PagingError::PageTablePoolExhausted);
        }
        let page = PhysicalPage::new(self.pages[self.used_count])
            .map_err(|_| PagingError::AddressNotPageAligned)?;
        self.used_count += 1;
        Ok(page)
    }
    pub fn pages(&self) -> &[u64] {
        &self.pages[..self.reserved_count]
    }
}

/// An address space whose table storage is owned by the physical allocator.
pub struct ActiveAddressSpace {
    root: PhysicalFrame,
    pool: PageTablePagePool,
    mapped_pages: u64,
    width: u8,
}
impl ActiveAddressSpace {
    pub const fn root(&self) -> PhysicalFrame {
        self.root
    }
    pub const fn mapped_pages(&self) -> u64 {
        self.mapped_pages
    }
    pub const fn pool(&self) -> &PageTablePagePool {
        &self.pool
    }
    pub const fn width(&self) -> u8 {
        self.width
    }
    #[allow(unsafe_code)]
    pub fn translate(&self, virtual_address: u64) -> Result<Option<Translation>, PagingError> {
        // SAFETY: the root and every followed entry were created by this module.
        unsafe { walk(self.root, virtual_address, self.width) }
    }
    #[allow(unsafe_code)]
    pub fn unmap_page(&mut self, page: VirtualPage) -> Result<PhysicalFrame, PagingError> {
        // SAFETY: the root is owned by this address space and the page is validated.
        let result = unsafe { clear_leaf(self.root, page.address(), self.width) }?;
        // SAFETY: the address is canonical and belongs to the active address space.
        unsafe {
            invalidate(page.address());
        }
        Ok(result)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Translation {
    pub physical_address: u64,
    pub page_size: u64,
    pub effective_writable: bool,
    pub effective_user: bool,
    pub effective_executable: bool,
    pub cache_disable: bool,
    pub write_through: bool,
}

/// Build and validate a FinnOS address space while the inherited identity map is active.
#[allow(unsafe_code)]
pub fn build(
    plan: &MappingPlan,
    allocator: &mut EarlyPhysicalPageAllocator,
    width: u8,
) -> Result<ActiveAddressSpace, PagingError> {
    // Keep physical page zero unavailable as well as virtual page zero. The
    // page allocator intentionally remains architecture-neutral, so this
    // one-page architectural reservation is made at the paging boundary.
    let probe = allocator
        .allocate_page()
        .map_err(|_| PagingError::PageTablePoolExhausted)?;
    if probe.start_address() != 0 {
        allocator
            .deallocate(
                crate::memory::PageRange::new(probe.start_address(), 1)
                    .map_err(|_| PagingError::PageTablePoolExhausted)?,
            )
            .map_err(|_| PagingError::PageTablePoolExhausted)?;
    }
    let mut pool =
        PageTablePagePool::reserve(allocator).map_err(|_| PagingError::PageTablePoolExhausted)?;
    let root_page = pool.take()?;
    let root = PhysicalFrame::new(root_page.start_address(), width)?;
    // SAFETY: every pool page came from the allocator, is aligned, and is still
    // identity accessible under the transition page tables.
    unsafe {
        zero_page(root.address());
    }
    let mut space = ActiveAddressSpace {
        root,
        pool,
        mapped_pages: 0,
        width,
    };
    let pool_count = space.pool.reserved_count();
    for index in 0..pool_count {
        let address = space.pool.pages()[index];
        // SAFETY: the pool page is allocator-owned and identity mapped during transition.
        unsafe {
            map_one(
                &mut space,
                VirtualPage::new(address)?,
                PhysicalFrame::new(address, width)?,
                MappingPermissions::kernel_rw_nx(),
            )?
        }
        space.mapped_pages = space
            .mapped_pages
            .checked_add(1)
            .ok_or(PagingError::AddressOverflow)?;
    }
    for request in plan.as_slice() {
        for page in 0..request.page_count {
            let offset = page
                .checked_mul(PAGE_SIZE)
                .ok_or(PagingError::AddressOverflow)?;
            let virtual_address = request
                .virtual_start
                .checked_add(offset)
                .ok_or(PagingError::AddressOverflow)?;
            let physical_address = request
                .physical_start
                .checked_add(offset)
                .ok_or(PagingError::AddressOverflow)?;
            let frame = PhysicalFrame::new(physical_address, width)?;
            // SAFETY: table pages are allocator-owned and identity mapped during transition.
            unsafe {
                map_one(
                    &mut space,
                    VirtualPage::new(virtual_address)?,
                    frame,
                    request.permissions,
                )?
            }
            space.mapped_pages = space
                .mapped_pages
                .checked_add(1)
                .ok_or(PagingError::AddressOverflow)?;
        }
    }
    Ok(space)
}

#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn map_one(
    space: &mut ActiveAddressSpace,
    page: VirtualPage,
    frame: PhysicalFrame,
    permissions: MappingPermissions,
) -> Result<(), PagingError> {
    let mut table = space.root.address();
    for index in [
        pml4_index(page.address()),
        pdpt_index(page.address()),
        page_directory_index(page.address()),
    ] {
        let entry_address = table
            .checked_add((index as u64) * 8)
            .ok_or(PagingError::AddressOverflow)?;
        let entry = read_entry(entry_address);
        if entry.is_present() {
            if entry.is_huge() {
                return Err(PagingError::UnexpectedHugePage);
            }
            table = entry.address(space.width)?;
        } else {
            let child_page = space.pool.take()?;
            let child = PhysicalFrame::new(child_page.start_address(), space.width)?;
            // SAFETY: the child is an allocator-owned, aligned transition page.
            zero_page(child.address());
            write_entry(entry_address, PageTableEntry::table(child));
            table = child.address();
        }
    }
    let leaf_address = table
        .checked_add((page_table_index(page.address()) as u64) * 8)
        .ok_or(PagingError::AddressOverflow)?;
    let old = read_entry(leaf_address);
    let new = PageTableEntry::leaf(frame, permissions)?;
    if old.is_present() {
        if old.raw() == new.raw() {
            return Ok(());
        }
        return Err(PagingError::AlreadyMapped);
    }
    write_entry(leaf_address, new);
    Ok(())
}

#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn walk(
    root: PhysicalFrame,
    address: u64,
    width: u8,
) -> Result<Option<Translation>, PagingError> {
    let page = VirtualPage::new(align_down(address))?;
    let mut table = root.address();
    let mut writable = true;
    let mut user = true;
    let mut executable = true;
    let mut pwt = false;
    let mut pcd = false;
    for index in [
        pml4_index(page.address()),
        pdpt_index(page.address()),
        page_directory_index(page.address()),
        page_table_index(page.address()),
    ] {
        let entry = read_entry(
            table
                .checked_add((index as u64) * 8)
                .ok_or(PagingError::AddressOverflow)?,
        );
        if !entry.is_present() {
            return Ok(None);
        }
        if entry.is_huge() {
            return Err(PagingError::UnexpectedHugePage);
        }
        writable &= entry.writable();
        user &= entry.user();
        executable &= entry.executable();
        pwt |= entry.write_through();
        pcd |= entry.cache_disable();
        table = entry.address(width)?;
    }
    Ok(Some(Translation {
        physical_address: table
            .checked_add(page_offset(address))
            .ok_or(PagingError::AddressOverflow)?,
        page_size: PAGE_SIZE,
        effective_writable: writable,
        effective_user: user,
        effective_executable: executable,
        cache_disable: pcd,
        write_through: pwt,
    }))
}

#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn clear_leaf(
    root: PhysicalFrame,
    address: u64,
    width: u8,
) -> Result<PhysicalFrame, PagingError> {
    let mut table = root.address();
    for index in [
        pml4_index(address),
        pdpt_index(address),
        page_directory_index(address),
    ] {
        let entry_address = table
            .checked_add((index as u64) * 8)
            .ok_or(PagingError::AddressOverflow)?;
        let entry = read_entry(entry_address);
        if !entry.is_present() {
            return Err(PagingError::NotMapped);
        }
        if entry.is_huge() {
            return Err(PagingError::UnexpectedHugePage);
        }
        table = entry.address(width)?;
    }
    let leaf_address = table
        .checked_add((page_table_index(address) as u64) * 8)
        .ok_or(PagingError::AddressOverflow)?;
    let old = read_entry(leaf_address);
    if !old.is_present() {
        return Err(PagingError::NotMapped);
    }
    let frame = PhysicalFrame::new(old.address(width)?, width)?;
    write_entry(leaf_address, PageTableEntry::empty());
    Ok(frame)
}

#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn read_entry(address: u64) -> PageTableEntry {
    PageTableEntry(core::ptr::read_volatile(address as *const u64))
}
#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn write_entry(address: u64, entry: PageTableEntry) {
    core::ptr::write_volatile(address as *mut u64, entry.raw());
}
#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn zero_page(address: u64) {
    core::ptr::write_bytes(address as *mut u8, 0, PAGE_SIZE as usize);
}
#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn invalidate(address: u64) {
    core::arch::asm!("invlpg [{}]", in(reg) address, options(nostack, preserves_flags));
}

/// Activate a prepared address space and enable the protections required by W^X.
#[allow(unsafe_code)]
#[allow(unsafe_op_in_unsafe_fn)]
pub unsafe fn activate(space: &ActiveAddressSpace) -> Result<(), PagingError> {
    if !cpu_paging_info()?.nx_supported {
        return Err(PagingError::NxUnsupported);
    }
    let mut efer = rdmsr(0xc000_0080);
    efer |= 1 << 11;
    wrmsr(0xc000_0080, efer);
    if rdmsr(0xc000_0080) & (1 << 11) == 0 {
        return Err(PagingError::EferNxFailed);
    }
    let mut cr0 = read_cr0();
    cr0 |= 1 << 16;
    core::arch::asm!("mov cr0, {}", in(reg) cr0, options(nostack, preserves_flags));
    if read_cr0() & (1 << 16) == 0 {
        return Err(PagingError::Cr0WriteProtectFailed);
    }
    core::arch::asm!("mov cr3, {}", in(reg) space.root.address(), options(nostack, preserves_flags));
    if read_cr3() & !0xfff != space.root.address() {
        return Err(PagingError::Cr3ActivationFailed);
    }
    Ok(())
}
#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
fn rdmsr(msr: u32) -> u64 {
    let low: u32;
    let high: u32;
    unsafe {
        core::arch::asm!("rdmsr", in("ecx") msr, out("eax") low, out("edx") high, options(nostack));
    }
    ((high as u64) << 32) | low as u64
}
#[cfg(target_arch = "x86_64")]
#[allow(unsafe_code)]
fn wrmsr(msr: u32, value: u64) {
    unsafe {
        core::arch::asm!("wrmsr", in("ecx") msr, in("eax") value as u32, in("edx") (value >> 32) as u32, options(nostack));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn canonical_and_indices() {
        assert!(is_canonical(0x0000_7fff_ffff_f000));
        assert!(is_canonical(0xffff_8000_0000_0000));
        assert!(!is_canonical(0x0001_0000_0000_0000));
        assert_eq!(pml4_index(0x1234_5678_9abc), 0x24);
        assert_eq!(page_offset(0x1234), 0x234);
    }
    #[test]
    fn alignment_and_width() {
        assert_eq!(align_down(0x1234), 0x1000);
        assert_eq!(align_up(0x1001), Ok(0x2000));
        assert_eq!(align_up(u64::MAX), Err(PagingError::AddressOverflow));
        assert!(PhysicalAddress::new(1 << 36, 36).is_err());
        assert!(PhysicalAddress::new((1 << 36) - 1, 36).is_ok());
    }
    #[test]
    fn entries_and_wx() {
        let frame = PhysicalFrame::new(0x2000, 48).unwrap();
        let e = PageTableEntry::leaf(frame, MappingPermissions::kernel_rx()).unwrap();
        assert!(e.is_present() && e.executable() && !e.writable());
        let n = PageTableEntry::leaf(frame, MappingPermissions::kernel_rw_nx()).unwrap();
        assert!(n.writable() && !n.executable());
        assert_eq!(
            MappingPermissions {
                writable: true,
                executable: true,
                user: false,
                write_through: false,
                cache_disable: false
            }
            .validate(),
            Err(PagingError::WritableExecutableMapping)
        );
    }
    #[test]
    fn plan_rejects_conflicts_and_null() {
        let mut p = MappingPlan::new();
        let r = MappingRequest {
            virtual_start: 0x1000,
            physical_start: 0x1000,
            page_count: 1,
            permissions: MappingPermissions::kernel_r_nx(),
            purpose: MappingPurpose::BootInfo,
        };
        p.push(r).unwrap();
        assert!(p.push(r).is_ok());
        let mut c = r;
        c.permissions = MappingPermissions::kernel_rw_nx();
        assert_eq!(p.push(c), Err(PagingError::MappingConflict));
        let mut z = r;
        z.virtual_start = 0;
        assert_eq!(p.push(z), Err(PagingError::NullPageMapped));
    }
    #[test]
    fn pool_capacity_is_fixed() {
        assert_eq!(MAX_PAGE_TABLE_PAGES, 64);
        assert_eq!(MappingPlan::new().len(), 0);
    }
}
