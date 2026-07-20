//! FinnOS-owned EL1 translation tables for the initial `AArch64` identity map.
//!
//! The policy/model half of this module is host-testable. Raw table memory and
//! system-register access are compiled only for the freestanding target. The
//! resulting address space is immutable after activation.

#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(unsafe_code)]

#[cfg(target_os = "none")]
use crate::memory::EarlyPhysicalPageAllocator;

/// Architectural page and translation-table granule used by this module.
pub const PAGE_SIZE: u64 = 4096;
/// Entries in every four-kibibyte translation table.
pub const TABLE_ENTRIES: usize = 512;
/// Maximum number of caller-supplied mapping requests.
pub const MAX_MAPPING_REQUESTS: usize = 64;
/// Maximum number of physical pages mapped by the caller's plan.
pub const MAX_MAPPED_PAGES: u64 = 32_768;
/// Number of allocator-owned pages reserved for all translation tables.
pub const MAX_TABLE_PAGES: usize = 64;
/// PL011 physical base on the supported QEMU `virt` machine.
pub const PL011_BASE: u64 = 0x0900_0000;

const LOW_VA_LIMIT: u64 = 1 << 48;
const OUTPUT_ADDRESS_MASK: u64 = 0x0000_ffff_ffff_f000;
const DESCRIPTOR_VALID: u64 = 1;
const DESCRIPTOR_TABLE_OR_PAGE: u64 = 1 << 1;
const ATTR_INDEX_SHIFT: u64 = 2;
const AP_READ_ONLY: u64 = 1 << 7;
const SH_OUTER: u64 = 0b10 << 8;
const SH_INNER: u64 = 0b11 << 8;
const ACCESS_FLAG: u64 = 1 << 10;
const PXN: u64 = 1 << 53;
const UXN: u64 = 1 << 54;
const TABLE_DESCRIPTOR_ALLOWED: u64 = OUTPUT_ADDRESS_MASK | 0b11;
const PAGE_DESCRIPTOR_ALLOWED: u64 = OUTPUT_ADDRESS_MASK
    | 0b11
    | (0b111 << ATTR_INDEX_SHIFT)
    | AP_READ_ONLY
    | (0b11 << 8)
    | ACCESS_FLAG
    | PXN
    | UXN;
const NORMAL_ATTRIBUTE_INDEX: u8 = 0;
const DEVICE_ATTRIBUTE_INDEX: u8 = 1;
const NORMAL_NON_CACHEABLE_ATTRIBUTE_INDEX: u8 = 2;

/// MAIR value: Normal WBWA, Device-nGnRnE, then Normal non-cacheable.
pub const MAIR_VALUE: u64 = 0x0044_00ff;
/// `SCTLR_EL1` bits required by the initial hardened regime.
pub const SCTLR_REQUIRED_BITS: u64 = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 12) | (1 << 19);

/// Errors returned while planning, constructing, walking, or activating tables.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PagingError {
    /// A start address is not aligned to 4 KiB.
    AddressNotPageAligned,
    /// A page count is zero.
    ZeroPageCount,
    /// Address or page-count arithmetic overflowed.
    AddressOverflow,
    /// The requested virtual address is outside the low 48-bit TTBR0 range.
    VirtualAddressOutOfRange,
    /// The requested physical address is wider than this implementation supports.
    PhysicalAddressOutOfRange,
    /// Page zero would be mapped.
    NullPageMapped,
    /// Writable and executable permissions were requested together.
    WritableExecutableMapping,
    /// Device memory was requested with executable permissions.
    ExecutableDeviceMapping,
    /// The fixed PL011 page was not mapped as one identity Device RW/NX page.
    InvalidPl011Mapping,
    /// A virtual range conflicts with an existing request.
    VirtualMappingConflict,
    /// One physical range would acquire conflicting permissions or memory types.
    PhysicalAliasConflict,
    /// The fixed mapping-request capacity is exhausted.
    MappingPlanCapacityExceeded,
    /// The bounded total mapped-page count is exceeded.
    MappedPageCapacityExceeded,
    /// The physical allocator cannot reserve the fixed table pool.
    TableAllocationFailed,
    /// The fixed translation-table pool is exhausted.
    TableCapacityExceeded,
    /// A table page is not identity-accessible while tables are being built.
    TablePageNotIdentityAccessible,
    /// A table page overlaps a caller mapping with incompatible attributes.
    TablePageAliasConflict,
    /// A descriptor is malformed or has an unexpected level/type.
    InvalidDescriptor,
    /// No valid translation exists for the requested virtual address.
    NotMapped,
    /// A live range needed during activation is absent or not identity-mapped.
    LiveIdentityMappingMissing,
    /// Current execution is not identity-mapped and cannot safely disable translation.
    CurrentIdentityMappingMissing,
    /// The CPU does not implement the required 4 KiB granule.
    FourKiBGranuleUnsupported,
    /// The CPU physical-address range is unsupported by this bounded slice.
    PhysicalAddressRangeUnsupported,
    /// The address space was already activated.
    AlreadyActive,
    /// Architectural register readback did not match the requested regime.
    ActivationReadbackFailed,
    /// The inherited EL1 regime uses big-endian data accesses.
    BigEndianEl1Unsupported,
}

/// Memory type selected through `MAIR_EL1`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryType {
    /// Inner-shareable Normal write-back/write-allocate memory.
    NormalWriteBack,
    /// Outer-shareable Device-nGnRnE memory.
    Device,
    /// Inner-shareable Normal non-cacheable memory (for example, a framebuffer).
    NormalNonCacheable,
}

impl MemoryType {
    const fn attribute_index(self) -> u8 {
        match self {
            Self::NormalWriteBack => NORMAL_ATTRIBUTE_INDEX,
            Self::Device => DEVICE_ATTRIBUTE_INDEX,
            Self::NormalNonCacheable => NORMAL_NON_CACHEABLE_ATTRIBUTE_INDEX,
        }
    }

    const fn shareability_bits(self) -> u64 {
        match self {
            Self::Device => SH_OUTER,
            Self::NormalWriteBack | Self::NormalNonCacheable => SH_INNER,
        }
    }
}

/// Privileged leaf permissions. EL0 access is always forbidden.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Permissions {
    /// Read-only and executable at EL1, execute-never at EL0.
    ReadExecute,
    /// Read-only and execute-never at every EL.
    ReadOnlyNoExecute,
    /// Read-write and execute-never at every EL.
    ReadWriteNoExecute,
}

impl Permissions {
    const fn writable(self) -> bool {
        matches!(self, Self::ReadWriteNoExecute)
    }

    const fn executable(self) -> bool {
        matches!(self, Self::ReadExecute)
    }
}

/// One checked page-aligned virtual-to-physical mapping request.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MappingRequest {
    /// First virtual address.
    pub virtual_start: u64,
    /// First physical address.
    pub physical_start: u64,
    /// Number of 4 KiB pages.
    pub page_count: u64,
    /// Privileged access permissions.
    pub permissions: Permissions,
    /// Architectural memory type.
    pub memory_type: MemoryType,
}

impl MappingRequest {
    fn byte_len(self) -> Result<u64, PagingError> {
        self.page_count
            .checked_mul(PAGE_SIZE)
            .ok_or(PagingError::AddressOverflow)
    }

    fn virtual_end(self) -> Result<u64, PagingError> {
        self.virtual_start
            .checked_add(self.byte_len()?)
            .ok_or(PagingError::AddressOverflow)
    }

    fn physical_end(self) -> Result<u64, PagingError> {
        self.physical_start
            .checked_add(self.byte_len()?)
            .ok_or(PagingError::AddressOverflow)
    }
}

const EMPTY_REQUEST: MappingRequest = MappingRequest {
    virtual_start: PAGE_SIZE,
    physical_start: PAGE_SIZE,
    page_count: 1,
    permissions: Permissions::ReadOnlyNoExecute,
    memory_type: MemoryType::NormalWriteBack,
};

/// Fixed-capacity, prevalidated mapping plan.
#[derive(Clone)]
pub struct MappingPlan {
    requests: [MappingRequest; MAX_MAPPING_REQUESTS],
    count: usize,
    mapped_pages: u64,
}

impl MappingPlan {
    /// Create an empty mapping plan.
    pub const fn new() -> Self {
        Self {
            requests: [EMPTY_REQUEST; MAX_MAPPING_REQUESTS],
            count: 0,
            mapped_pages: 0,
        }
    }

    /// Return validated mapping requests.
    pub fn as_slice(&self) -> &[MappingRequest] {
        &self.requests[..self.count]
    }

    /// Return the number of requested virtual pages.
    pub const fn mapped_pages(&self) -> u64 {
        self.mapped_pages
    }

    /// Add a request after checking bounds, W^X, virtual overlap, and physical aliases.
    pub fn push(&mut self, request: MappingRequest) -> Result<(), PagingError> {
        validate_request(request)?;
        if self.as_slice().contains(&request) {
            return Ok(());
        }
        let virtual_end = request.virtual_end()?;
        let physical_end = request.physical_end()?;
        for old in self.as_slice() {
            let old_virtual_end = old.virtual_end()?;
            if overlaps(
                request.virtual_start,
                virtual_end,
                old.virtual_start,
                old_virtual_end,
            ) {
                return Err(PagingError::VirtualMappingConflict);
            }
            let old_physical_end = old.physical_end()?;
            if overlaps(
                request.physical_start,
                physical_end,
                old.physical_start,
                old_physical_end,
            ) && (request.permissions != old.permissions
                || request.memory_type != old.memory_type)
            {
                return Err(PagingError::PhysicalAliasConflict);
            }
        }
        if self.count == MAX_MAPPING_REQUESTS {
            return Err(PagingError::MappingPlanCapacityExceeded);
        }
        let new_total = self
            .mapped_pages
            .checked_add(request.page_count)
            .ok_or(PagingError::MappedPageCapacityExceeded)?;
        if new_total > MAX_MAPPED_PAGES {
            return Err(PagingError::MappedPageCapacityExceeded);
        }
        self.requests[self.count] = request;
        self.count += 1;
        self.mapped_pages = new_total;
        Ok(())
    }

    /// Prove that every page of a range has an identity mapping.
    pub fn contains_identity_range(&self, range: IdentityRange) -> Result<bool, PagingError> {
        range.validate()?;
        let end = range.end()?;
        let mut cursor = range.start;
        while cursor < end {
            let Some(request) = self.as_slice().iter().find(|request| {
                request.virtual_start <= cursor
                    && request
                        .virtual_end()
                        .is_ok_and(|request_end| cursor < request_end)
            }) else {
                return Ok(false);
            };
            let offset = cursor
                .checked_sub(request.virtual_start)
                .ok_or(PagingError::AddressOverflow)?;
            if request.physical_start.checked_add(offset) != Some(cursor) {
                return Ok(false);
            }
            cursor = cursor
                .checked_add(PAGE_SIZE)
                .ok_or(PagingError::AddressOverflow)?;
        }
        Ok(true)
    }
}

impl Default for MappingPlan {
    fn default() -> Self {
        Self::new()
    }
}

fn validate_request(request: MappingRequest) -> Result<(), PagingError> {
    if request.page_count == 0 {
        return Err(PagingError::ZeroPageCount);
    }
    if !request.virtual_start.is_multiple_of(PAGE_SIZE)
        || !request.physical_start.is_multiple_of(PAGE_SIZE)
    {
        return Err(PagingError::AddressNotPageAligned);
    }
    if request.virtual_start == 0 {
        return Err(PagingError::NullPageMapped);
    }
    if request.permissions.writable() && request.permissions.executable() {
        return Err(PagingError::WritableExecutableMapping);
    }
    if request.memory_type == MemoryType::Device && request.permissions.executable() {
        return Err(PagingError::ExecutableDeviceMapping);
    }
    let virtual_end = request.virtual_end()?;
    let physical_end = request.physical_end()?;
    if virtual_end > LOW_VA_LIMIT {
        return Err(PagingError::VirtualAddressOutOfRange);
    }
    if physical_end > LOW_VA_LIMIT {
        return Err(PagingError::PhysicalAddressOutOfRange);
    }
    let overlaps_pl011_physical = overlaps(
        request.physical_start,
        physical_end,
        PL011_BASE,
        PL011_BASE + PAGE_SIZE,
    );
    let overlaps_pl011_virtual = overlaps(
        request.virtual_start,
        virtual_end,
        PL011_BASE,
        PL011_BASE + PAGE_SIZE,
    );
    if (overlaps_pl011_physical || overlaps_pl011_virtual)
        && (request.virtual_start != PL011_BASE
            || request.physical_start != PL011_BASE
            || request.page_count != 1
            || request.permissions != Permissions::ReadWriteNoExecute
            || request.memory_type != MemoryType::Device)
    {
        return Err(PagingError::InvalidPl011Mapping);
    }
    Ok(())
}

const fn overlaps(a_start: u64, a_end: u64, b_start: u64, b_end: u64) -> bool {
    a_start < b_end && b_start < a_end
}

/// A page-aligned identity range that must remain live across activation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct IdentityRange {
    /// Inclusive page-aligned start.
    pub start: u64,
    /// Number of pages in the range.
    pub page_count: u64,
}

impl IdentityRange {
    fn validate(self) -> Result<(), PagingError> {
        if self.page_count == 0 {
            return Err(PagingError::ZeroPageCount);
        }
        if !self.start.is_multiple_of(PAGE_SIZE) {
            return Err(PagingError::AddressNotPageAligned);
        }
        let _ = self.end()?;
        Ok(())
    }

    fn end(self) -> Result<u64, PagingError> {
        self.start
            .checked_add(
                self.page_count
                    .checked_mul(PAGE_SIZE)
                    .ok_or(PagingError::AddressOverflow)?,
            )
            .ok_or(PagingError::AddressOverflow)
    }
}

/// Decoded result of a page-table walk.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Translation {
    /// Physical address including the page offset.
    pub physical_address: u64,
    /// Leaf permissions.
    pub permissions: Permissions,
    /// Leaf memory type.
    pub memory_type: MemoryType,
}

/// Build a level-three page descriptor.
pub fn page_descriptor(
    physical_address: u64,
    permissions: Permissions,
    memory_type: MemoryType,
) -> Result<u64, PagingError> {
    if !physical_address.is_multiple_of(PAGE_SIZE) {
        return Err(PagingError::AddressNotPageAligned);
    }
    if physical_address >= LOW_VA_LIMIT {
        return Err(PagingError::PhysicalAddressOutOfRange);
    }
    if permissions.writable() && permissions.executable() {
        return Err(PagingError::WritableExecutableMapping);
    }
    if memory_type == MemoryType::Device && permissions.executable() {
        return Err(PagingError::ExecutableDeviceMapping);
    }
    let mut descriptor = DESCRIPTOR_VALID
        | DESCRIPTOR_TABLE_OR_PAGE
        | physical_address
        | (u64::from(memory_type.attribute_index()) << ATTR_INDEX_SHIFT)
        | memory_type.shareability_bits()
        | ACCESS_FLAG
        | UXN;
    if !permissions.writable() {
        descriptor |= AP_READ_ONLY;
    }
    if !permissions.executable() {
        descriptor |= PXN;
    }
    Ok(descriptor)
}

#[cfg(any(target_os = "none", test))]
const fn table_descriptor(physical_address: u64) -> Result<u64, PagingError> {
    if !physical_address.is_multiple_of(PAGE_SIZE) {
        return Err(PagingError::AddressNotPageAligned);
    }
    if physical_address >= LOW_VA_LIMIT {
        return Err(PagingError::PhysicalAddressOutOfRange);
    }
    Ok(DESCRIPTOR_VALID | DESCRIPTOR_TABLE_OR_PAGE | physical_address)
}

fn decode_page_descriptor(raw: u64, page_offset: u64) -> Result<Translation, PagingError> {
    if raw & !PAGE_DESCRIPTOR_ALLOWED != 0 {
        return Err(PagingError::InvalidDescriptor);
    }
    if raw & 0b11 != 0b11 {
        return Err(if raw & DESCRIPTOR_VALID == 0 {
            PagingError::NotMapped
        } else {
            PagingError::InvalidDescriptor
        });
    }
    let attribute_index = ((raw >> ATTR_INDEX_SHIFT) & 0b111) as u8;
    let memory_type = match attribute_index {
        NORMAL_ATTRIBUTE_INDEX => MemoryType::NormalWriteBack,
        DEVICE_ATTRIBUTE_INDEX => MemoryType::Device,
        NORMAL_NON_CACHEABLE_ATTRIBUTE_INDEX => MemoryType::NormalNonCacheable,
        _ => return Err(PagingError::InvalidDescriptor),
    };
    let expected_shareability = memory_type.shareability_bits();
    if raw & (0b11 << 8) != expected_shareability {
        return Err(PagingError::InvalidDescriptor);
    }
    let read_only = raw & AP_READ_ONLY != 0;
    let pxn = raw & PXN != 0;
    let uxn = raw & UXN != 0;
    if !uxn || raw & ACCESS_FLAG == 0 {
        return Err(PagingError::InvalidDescriptor);
    }
    let permissions = match (read_only, pxn) {
        (true, false) => Permissions::ReadExecute,
        (true, true) => Permissions::ReadOnlyNoExecute,
        (false, true) => Permissions::ReadWriteNoExecute,
        (false, false) => return Err(PagingError::WritableExecutableMapping),
    };
    Ok(Translation {
        physical_address: (raw & OUTPUT_ADDRESS_MASK)
            .checked_add(page_offset)
            .ok_or(PagingError::AddressOverflow)?,
        permissions,
        memory_type,
    })
}

/// Walk a four-level 4 KiB table through a caller-supplied descriptor reader.
pub fn walk_with<F>(
    root: u64,
    virtual_address: u64,
    mut read: F,
) -> Result<Translation, PagingError>
where
    F: FnMut(u64, usize) -> Result<u64, PagingError>,
{
    if root >= LOW_VA_LIMIT || !root.is_multiple_of(PAGE_SIZE) {
        return Err(PagingError::PhysicalAddressOutOfRange);
    }
    if virtual_address >= LOW_VA_LIMIT {
        return Err(PagingError::VirtualAddressOutOfRange);
    }
    let shifts = [39u32, 30, 21, 12];
    let mut table = root;
    for (level, shift) in shifts.into_iter().enumerate() {
        let index = ((virtual_address >> shift) & 0x1ff) as usize;
        let raw = read(table, index)?;
        if level == 3 {
            return decode_page_descriptor(raw, virtual_address & (PAGE_SIZE - 1));
        }
        if raw & 0b11 != 0b11 {
            return Err(if raw & DESCRIPTOR_VALID == 0 {
                PagingError::NotMapped
            } else {
                PagingError::InvalidDescriptor
            });
        }
        if raw & !TABLE_DESCRIPTOR_ALLOWED != 0 {
            return Err(PagingError::InvalidDescriptor);
        }
        table = raw & OUTPUT_ADDRESS_MASK;
    }
    Err(PagingError::InvalidDescriptor)
}

/// CPU translation capabilities required by this bounded implementation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CpuFeatures {
    /// Physical-address width in bits.
    pub physical_address_bits: u8,
}

impl CpuFeatures {
    /// Decode `ID_AA64MMFR0_EL1` and reject unsupported granules/PARange values.
    pub const fn from_mmfr0(value: u64) -> Result<Self, PagingError> {
        let tgran4 = ((value >> 28) & 0xf) as u8;
        if tgran4 != 0 {
            return Err(PagingError::FourKiBGranuleUnsupported);
        }
        let physical_address_bits = match value & 0xf {
            0 => 32,
            1 => 36,
            2 => 40,
            3 => 42,
            4 => 44,
            5 => 48,
            _ => return Err(PagingError::PhysicalAddressRangeUnsupported),
        };
        Ok(Self {
            physical_address_bits,
        })
    }
}

const fn ips_encoding(bits: u8) -> Result<u64, PagingError> {
    match bits {
        32 => Ok(0),
        36 => Ok(1),
        40 => Ok(2),
        42 => Ok(3),
        44 => Ok(4),
        48 => Ok(5),
        _ => Err(PagingError::PhysicalAddressRangeUnsupported),
    }
}

/// Construct `TCR_EL1` for a 48-bit low `TTBR0`, 4 KiB, WBWA regime.
pub const fn tcr_value(features: CpuFeatures) -> Result<u64, PagingError> {
    let t0sz = 16u64;
    let irgn0_wbwa = 0b01u64 << 8;
    let orgn0_wbwa = 0b01u64 << 10;
    let sh0_inner = 0b11u64 << 12;
    let t1sz = 16u64 << 16;
    let epd1 = 1u64 << 23;
    let tg1_4k = 0b10u64 << 30;
    let ips = match ips_encoding(features.physical_address_bits) {
        Ok(value) => value << 32,
        Err(error) => return Err(error),
    };
    Ok(t0sz | irgn0_wbwa | orgn0_wbwa | sh0_inner | t1sz | epd1 | tg1_4k | ips)
}

#[cfg(target_os = "none")]
#[derive(Clone)]
struct TablePool {
    pages: [u64; MAX_TABLE_PAGES],
    used: usize,
}

#[cfg(target_os = "none")]
impl TablePool {
    fn reserve(allocator: &mut EarlyPhysicalPageAllocator) -> Result<Self, PagingError> {
        let mut pages = [0u64; MAX_TABLE_PAGES];
        for page in &mut pages {
            *page = allocator
                .allocate_page()
                .map_err(|_| PagingError::TableAllocationFailed)?
                .start_address();
        }
        Ok(Self { pages, used: 1 })
    }

    fn contains(&self, address: u64) -> bool {
        self.pages.contains(&address)
    }

    const fn take(&mut self) -> Result<u64, PagingError> {
        if self.used == MAX_TABLE_PAGES {
            return Err(PagingError::TableCapacityExceeded);
        }
        let page = self.pages[self.used];
        self.used += 1;
        Ok(page)
    }
}

/// Allocator-owned immutable address space ready for EL1 activation.
#[cfg(target_os = "none")]
pub struct ActiveAddressSpace {
    root: u64,
    pool: TablePool,
    plan: MappingPlan,
    features: CpuFeatures,
    active: bool,
}

#[cfg(target_os = "none")]
impl ActiveAddressSpace {
    /// Root table physical address.
    pub const fn root_address(&self) -> u64 {
        self.root
    }

    /// Number of translation-table pages actually linked.
    pub const fn used_table_pages(&self) -> usize {
        self.pool.used
    }

    /// Walk the constructed hardware tables without changing them.
    pub fn translate(&self, virtual_address: u64) -> Result<Translation, PagingError> {
        walk_with(self.root, virtual_address, |table, index| {
            if !self.pool.contains(table) {
                return Err(PagingError::InvalidDescriptor);
            }
            // SAFETY: the immutable pool remains reserved and identity mapped.
            Ok(unsafe { core::ptr::read((table as *const u64).add(index)) })
        })
    }

    /// Activate this immutable address space after proving every live identity range.
    ///
    /// # Safety
    ///
    /// `live_ranges` must cover every caller-owned code/data/MMIO range touched
    /// before the caller can stop or transfer control. This function additionally
    /// proves its current PC, SP, and VBAR mappings itself.
    #[allow(clippy::too_many_lines)]
    pub unsafe fn activate(&mut self, live_ranges: &[IdentityRange]) -> Result<(), PagingError> {
        if self.active {
            return Err(PagingError::AlreadyActive);
        }
        let required_pl011 = MappingRequest {
            virtual_start: PL011_BASE,
            physical_start: PL011_BASE,
            page_count: 1,
            permissions: Permissions::ReadWriteNoExecute,
            memory_type: MemoryType::Device,
        };
        if !self.plan.as_slice().contains(&required_pl011) {
            return Err(PagingError::InvalidPl011Mapping);
        }
        let mut pc: u64;
        let mut sp: u64;
        let mut vbar: u64;
        let old_sctlr: u64;
        // SAFETY: these EL1 registers are readable in the supported entry state.
        unsafe {
            core::arch::asm!(
                "adr {pc}, 2f",
                "mov {sp}, sp",
                "mrs {vbar}, vbar_el1",
                "mrs {sctlr}, sctlr_el1",
                "2:",
                pc = out(reg) pc,
                sp = out(reg) sp,
                vbar = out(reg) vbar,
                sctlr = out(reg) old_sctlr,
                options(nomem, nostack, preserves_flags)
            );
        }
        if old_sctlr & (1 << 25) != 0 {
            return Err(PagingError::BigEndianEl1Unsupported);
        }
        pc &= !(PAGE_SIZE - 1);
        sp &= !(PAGE_SIZE - 1);
        vbar &= !(PAGE_SIZE - 1);
        for address in [pc, sp, vbar] {
            let range = IdentityRange {
                start: address,
                page_count: 1,
            };
            if !self.plan.contains_identity_range(range)?
                || self.translate(address)?.physical_address != address
            {
                return Err(PagingError::LiveIdentityMappingMissing);
            }
            if old_sctlr & 1 != 0 && !current_translation_is_identity(address) {
                return Err(PagingError::CurrentIdentityMappingMissing);
            }
        }
        for range in live_ranges {
            if !self.plan.contains_identity_range(*range)? {
                return Err(PagingError::LiveIdentityMappingMissing);
            }
            let end = range.end()?;
            let mut address = range.start;
            while address < end {
                if self.translate(address)?.physical_address != address {
                    return Err(PagingError::LiveIdentityMappingMissing);
                }
                if old_sctlr & 1 != 0 && !current_translation_is_identity(address) {
                    return Err(PagingError::CurrentIdentityMappingMissing);
                }
                address = address
                    .checked_add(PAGE_SIZE)
                    .ok_or(PagingError::AddressOverflow)?;
            }
        }
        // Clean every descriptor page to the point of coherency before changing
        // translation controls. This is required even on coherent systems:
        // table walks need not observe dirty cache lines created by the CPU.
        // SAFETY: all used pages remain allocator-owned and identity-accessible.
        unsafe { self.clean_tables_to_poc() };

        let tcr = tcr_value(self.features)?;
        let new_sctlr = old_sctlr | SCTLR_REQUIRED_BITS;
        let disabled_sctlr = old_sctlr & !1;
        // SAFETY: current PC/SP/VBAR and all caller-declared live ranges were
        // proven identity mapped under both the inherited and new regimes.
        unsafe {
            core::arch::asm!(
                "dsb sy",
                "msr sctlr_el1, {disabled}",
                "isb",
                "msr mair_el1, {mair}",
                "msr tcr_el1, {tcr}",
                "msr ttbr0_el1, {root}",
                "msr ttbr1_el1, xzr",
                "dsb sy",
                "tlbi vmalle1",
                "dsb sy",
                "isb",
                "msr sctlr_el1, {enabled}",
                "isb",
                disabled = in(reg) disabled_sctlr,
                mair = in(reg) MAIR_VALUE,
                tcr = in(reg) tcr,
                root = in(reg) self.root,
                enabled = in(reg) new_sctlr,
                options(nostack, preserves_flags)
            );
        }
        self.active = true;
        let read_sctlr: u64;
        let read_mair: u64;
        let read_tcr: u64;
        let read_ttbr0: u64;
        let read_ttbr1: u64;
        // SAFETY: activation completed and these EL1 registers are readable.
        unsafe {
            core::arch::asm!(
                "mrs {sctlr}, sctlr_el1",
                "mrs {mair}, mair_el1",
                "mrs {tcr}, tcr_el1",
                "mrs {ttbr0}, ttbr0_el1",
                "mrs {ttbr1}, ttbr1_el1",
                sctlr = out(reg) read_sctlr,
                mair = out(reg) read_mair,
                tcr = out(reg) read_tcr,
                ttbr0 = out(reg) read_ttbr0,
                ttbr1 = out(reg) read_ttbr1,
                options(nomem, nostack, preserves_flags)
            );
        }
        if read_sctlr != new_sctlr
            || read_mair != MAIR_VALUE
            || read_tcr != tcr
            || read_ttbr0 != self.root
            || read_ttbr1 != 0
        {
            return Err(PagingError::ActivationReadbackFailed);
        }
        Ok(())
    }

    /// Write back all descriptor cache lines to the point of coherency.
    unsafe fn clean_tables_to_poc(&self) {
        let ctr: u64;
        // SAFETY: CTR_EL0 is readable at EL1.
        unsafe {
            core::arch::asm!(
                "mrs {ctr}, ctr_el0",
                ctr = out(reg) ctr,
                options(nomem, nostack, preserves_flags)
            );
        }
        let line_size = 4u64 << ((ctr >> 16) & 0xf);
        for page in &self.pool.pages[..self.pool.used] {
            let mut address = *page;
            let end = address + PAGE_SIZE;
            while address < end {
                // SAFETY: the address names an allocator-owned table cache line.
                unsafe {
                    core::arch::asm!(
                        "dc cvac, {address}",
                        address = in(reg) address,
                        options(nostack, preserves_flags)
                    );
                }
                address += line_size;
            }
        }
        // SAFETY: complete all cache maintenance before translation-table use.
        unsafe {
            core::arch::asm!("dsb sy", options(nostack, preserves_flags));
        }
    }
}

/// Build immutable allocator-owned tables without changing architectural state.
///
/// # Safety
///
/// Allocator-returned physical pages must refer to initialized, writable RAM.
/// When inherited translation is enabled, those pages must currently be
/// identity-mapped; this function checks that architectural fact before access.
#[cfg(target_os = "none")]
pub unsafe fn build(
    plan: &MappingPlan,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<ActiveAddressSpace, PagingError> {
    let mmfr0: u64;
    let sctlr: u64;
    // SAFETY: the supported boot path executes at EL1.
    unsafe {
        core::arch::asm!(
            "mrs {mmfr0}, id_aa64mmfr0_el1",
            "mrs {sctlr}, sctlr_el1",
            mmfr0 = out(reg) mmfr0,
            sctlr = out(reg) sctlr,
            options(nomem, nostack, preserves_flags)
        );
    }
    let features = CpuFeatures::from_mmfr0(mmfr0)?;
    if sctlr & (1 << 25) != 0 {
        return Err(PagingError::BigEndianEl1Unsupported);
    }
    // Do not publish any table-page allocations until every validation and
    // descriptor write has succeeded. On error, only bytes in still-free RAM
    // were changed; the allocator state remains exactly as supplied.
    let mut allocator_transaction = allocator.clone();
    let pool = TablePool::reserve(&mut allocator_transaction)?;
    let physical_limit = 1u64 << features.physical_address_bits;
    for request in plan.as_slice() {
        if request.physical_end()? > physical_limit {
            return Err(PagingError::PhysicalAddressOutOfRange);
        }
    }
    for page in pool.pages {
        if page >= physical_limit {
            return Err(PagingError::PhysicalAddressOutOfRange);
        }
        if sctlr & 1 != 0 && !current_translation_is_identity(page) {
            return Err(PagingError::TablePageNotIdentityAccessible);
        }
        for request in plan.as_slice() {
            if request.physical_start <= page && page < request.physical_end()? {
                let offset = page - request.physical_start;
                let virtual_alias = request
                    .virtual_start
                    .checked_add(offset)
                    .ok_or(PagingError::AddressOverflow)?;
                if virtual_alias != page
                    || request.permissions != Permissions::ReadWriteNoExecute
                    || request.memory_type != MemoryType::NormalWriteBack
                {
                    return Err(PagingError::TablePageAliasConflict);
                }
            }
        }
        // SAFETY: the page is allocator-owned, checked identity-accessible, and
        // no references to it exist. Normal RAM uses ordinary stores.
        unsafe { core::ptr::write_bytes(page as *mut u8, 0, TABLE_ENTRIES * 8) };
    }
    let root = pool.pages[0];
    let mut space = ActiveAddressSpace {
        root,
        pool,
        plan: plan.clone(),
        features,
        active: false,
    };
    for request in plan.as_slice() {
        for page_index in 0..request.page_count {
            let offset = page_index
                .checked_mul(PAGE_SIZE)
                .ok_or(PagingError::AddressOverflow)?;
            // SAFETY: all pool pages are exclusively owned and identity accessible.
            unsafe {
                map_one(
                    &mut space,
                    request
                        .virtual_start
                        .checked_add(offset)
                        .ok_or(PagingError::AddressOverflow)?,
                    request
                        .physical_start
                        .checked_add(offset)
                        .ok_or(PagingError::AddressOverflow)?,
                    request.permissions,
                    request.memory_type,
                )?;
            }
        }
    }
    for page in space.pool.pages {
        // SAFETY: table pages are part of the immutable pool and must remain live.
        unsafe {
            map_one(
                &mut space,
                page,
                page,
                Permissions::ReadWriteNoExecute,
                MemoryType::NormalWriteBack,
            )?;
        }
    }
    allocator.copy_state_from(&allocator_transaction);
    Ok(space)
}

#[cfg(target_os = "none")]
unsafe fn map_one(
    space: &mut ActiveAddressSpace,
    virtual_address: u64,
    physical_address: u64,
    permissions: Permissions,
    memory_type: MemoryType,
) -> Result<(), PagingError> {
    let shifts = [39u32, 30, 21];
    let mut table = space.root;
    for shift in shifts {
        let index = ((virtual_address >> shift) & 0x1ff) as usize;
        // SAFETY: table is always an exclusively owned pool page.
        let pointer = unsafe { (table as *mut u64).add(index) };
        // SAFETY: pointer is aligned and inside the table page.
        let raw = unsafe { core::ptr::read(pointer) };
        if raw & DESCRIPTOR_VALID == 0 {
            let child = space.pool.take()?;
            // SAFETY: this entry is not visible to hardware before activation.
            unsafe { core::ptr::write(pointer, table_descriptor(child)?) };
            table = child;
        } else if raw & 0b11 == 0b11 {
            let child = raw & OUTPUT_ADDRESS_MASK;
            if !space.pool.contains(child) {
                return Err(PagingError::InvalidDescriptor);
            }
            table = child;
        } else {
            return Err(PagingError::InvalidDescriptor);
        }
    }
    let index = ((virtual_address >> 12) & 0x1ff) as usize;
    // SAFETY: table is an exclusively owned pool page.
    let pointer = unsafe { (table as *mut u64).add(index) };
    // SAFETY: pointer is aligned and inside the table page.
    let old = unsafe { core::ptr::read(pointer) };
    let new = page_descriptor(physical_address, permissions, memory_type)?;
    if old != 0 && old != new {
        return Err(PagingError::VirtualMappingConflict);
    }
    // SAFETY: tables are not active and this entry is exclusively owned.
    unsafe { core::ptr::write(pointer, new) };
    Ok(())
}

#[cfg(target_os = "none")]
fn current_translation_is_identity(address: u64) -> bool {
    let par: u64;
    // SAFETY: AT S1E1R and PAR_EL1 are available at EL1. ISB orders the result.
    unsafe {
        core::arch::asm!(
            "at s1e1r, {address}",
            "isb",
            "mrs {par}, par_el1",
            address = in(reg) address,
            par = out(reg) par,
            options(nomem, nostack, preserves_flags)
        );
    }
    if par & 1 != 0 {
        return false;
    }
    let translated = (par & OUTPUT_ADDRESS_MASK) | (address & (PAGE_SIZE - 1));
    translated == address
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn descriptor_encodes_permissions_and_memory_type() {
        let text = page_descriptor(
            0x4020_0000,
            Permissions::ReadExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap();
        assert_eq!(text & AP_READ_ONLY, AP_READ_ONLY);
        assert_eq!(text & PXN, 0);
        assert_eq!(text & UXN, UXN);
        assert_eq!((text >> ATTR_INDEX_SHIFT) & 0b111, 0);

        let data = page_descriptor(
            0x4030_0000,
            Permissions::ReadWriteNoExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap();
        assert_eq!(data & AP_READ_ONLY, 0);
        assert_eq!(data & (PXN | UXN), PXN | UXN);
        assert_eq!(data & SH_INNER, SH_INNER);

        let uart = page_descriptor(
            PL011_BASE,
            Permissions::ReadWriteNoExecute,
            MemoryType::Device,
        )
        .unwrap();
        assert_eq!((uart >> ATTR_INDEX_SHIFT) & 0b111, 1);
        assert_eq!(uart & SH_INNER, SH_OUTER);
        assert_eq!(uart & (PXN | UXN), PXN | UXN);

        let framebuffer = page_descriptor(
            0x5000_0000,
            Permissions::ReadWriteNoExecute,
            MemoryType::NormalNonCacheable,
        )
        .unwrap();
        assert_eq!((framebuffer >> ATTR_INDEX_SHIFT) & 0b111, 2);
        assert_eq!(framebuffer & SH_INNER, SH_INNER);
        assert_eq!(framebuffer & (PXN | UXN), PXN | UXN);
    }

    #[test]
    fn plan_rejects_null_bounds_wx_and_conflicts() {
        let mut plan = MappingPlan::new();
        let mut request = MappingRequest {
            virtual_start: 0,
            physical_start: 0x1000,
            page_count: 1,
            permissions: Permissions::ReadOnlyNoExecute,
            memory_type: MemoryType::NormalWriteBack,
        };
        assert_eq!(plan.push(request), Err(PagingError::NullPageMapped));
        request.virtual_start = 0x1001;
        assert_eq!(plan.push(request), Err(PagingError::AddressNotPageAligned));
        request.virtual_start = 0x1000;
        request.page_count = 0;
        assert_eq!(plan.push(request), Err(PagingError::ZeroPageCount));
        request.page_count = MAX_MAPPED_PAGES + 1;
        assert_eq!(
            plan.push(request),
            Err(PagingError::MappedPageCapacityExceeded)
        );

        request.page_count = 1;
        plan.push(request).unwrap();
        let mut overlap = request;
        overlap.physical_start = 0x2000;
        assert_eq!(plan.push(overlap), Err(PagingError::VirtualMappingConflict));
        let alias = MappingRequest {
            virtual_start: 0x3000,
            permissions: Permissions::ReadWriteNoExecute,
            ..request
        };
        assert_eq!(plan.push(alias), Err(PagingError::PhysicalAliasConflict));
        let device_alias = MappingRequest {
            virtual_start: 0x4000,
            memory_type: MemoryType::Device,
            ..request
        };
        assert_eq!(
            plan.push(device_alias),
            Err(PagingError::PhysicalAliasConflict)
        );

        assert_eq!(
            MappingPlan::new().push(MappingRequest {
                virtual_start: PL011_BASE,
                physical_start: PL011_BASE,
                page_count: 1,
                permissions: Permissions::ReadOnlyNoExecute,
                memory_type: MemoryType::Device,
            }),
            Err(PagingError::InvalidPl011Mapping)
        );
        assert_eq!(
            MappingPlan::new().push(MappingRequest {
                virtual_start: PL011_BASE,
                physical_start: 0x8000_0000,
                page_count: 1,
                permissions: Permissions::ReadWriteNoExecute,
                memory_type: MemoryType::NormalWriteBack,
            }),
            Err(PagingError::InvalidPl011Mapping)
        );
    }

    #[test]
    fn plan_capacity_and_identity_proof_are_bounded() {
        let mut plan = MappingPlan::new();
        for index in 0..MAX_MAPPING_REQUESTS {
            plan.push(MappingRequest {
                virtual_start: 0x1000 + index as u64 * 0x2000,
                physical_start: 0x1000 + index as u64 * 0x2000,
                page_count: 1,
                permissions: Permissions::ReadOnlyNoExecute,
                memory_type: MemoryType::NormalWriteBack,
            })
            .unwrap();
        }
        assert_eq!(
            plan.push(MappingRequest {
                virtual_start: 0x20_0000,
                physical_start: 0x20_0000,
                page_count: 1,
                permissions: Permissions::ReadOnlyNoExecute,
                memory_type: MemoryType::NormalWriteBack,
            }),
            Err(PagingError::MappingPlanCapacityExceeded)
        );
        assert!(
            plan.contains_identity_range(IdentityRange {
                start: 0x1000,
                page_count: 1
            })
            .unwrap()
        );
        assert!(
            !plan
                .contains_identity_range(IdentityRange {
                    start: 0x2000,
                    page_count: 1
                })
                .unwrap()
        );
    }

    #[test]
    fn walker_decodes_four_levels_and_rejects_absence() {
        let root = 0x1000;
        let tables = [0x1000, 0x2000, 0x3000, 0x4000];
        let mut entries = [[0u64; TABLE_ENTRIES]; 4];
        let virtual_address = 0x0000_0000_4020_0123;
        let shifts = [39u32, 30, 21];
        for (level, shift) in shifts.into_iter().enumerate() {
            let index = ((virtual_address >> shift) & 0x1ff) as usize;
            entries[level][index] = table_descriptor(tables[level + 1]).unwrap();
        }
        entries[3][((virtual_address >> 12) & 0x1ff) as usize] = page_descriptor(
            0x5000,
            Permissions::ReadOnlyNoExecute,
            MemoryType::NormalWriteBack,
        )
        .unwrap();
        let translation = walk_with(root, virtual_address, |table, index| {
            let table_index = tables
                .iter()
                .position(|candidate| *candidate == table)
                .ok_or(PagingError::InvalidDescriptor)?;
            Ok(entries[table_index][index])
        })
        .unwrap();
        assert_eq!(translation.physical_address, 0x5123);
        assert_eq!(translation.permissions, Permissions::ReadOnlyNoExecute);
        assert_eq!(
            walk_with(root, 0, |table, index| {
                let table_index = tables
                    .iter()
                    .position(|candidate| *candidate == table)
                    .ok_or(PagingError::InvalidDescriptor)?;
                Ok(entries[table_index][index])
            }),
            Err(PagingError::NotMapped)
        );

        entries[3][((virtual_address >> 12) & 0x1ff) as usize] |= 1 << 55;
        assert_eq!(
            walk_with(root, virtual_address, |table, index| {
                let table_index = tables
                    .iter()
                    .position(|candidate| *candidate == table)
                    .ok_or(PagingError::InvalidDescriptor)?;
                Ok(entries[table_index][index])
            }),
            Err(PagingError::InvalidDescriptor)
        );
    }

    #[test]
    fn control_register_encodings_reject_unsupported_features() {
        let features = CpuFeatures::from_mmfr0(2).unwrap();
        assert_eq!(features.physical_address_bits, 40);
        let tcr = tcr_value(features).unwrap();
        assert_eq!(tcr & 0x3f, 16);
        assert_eq!((tcr >> 32) & 0b111, 2);
        assert_ne!(tcr & (1 << 23), 0);
        assert_eq!((tcr >> 16) & 0x3f, 16);
        assert_eq!((tcr >> 30) & 0b11, 0b10);
        assert_eq!(MAIR_VALUE & 0xff, 0xff);
        assert_eq!((MAIR_VALUE >> 8) & 0xff, 0);
        assert_eq!((MAIR_VALUE >> 16) & 0xff, 0x44);
        assert_eq!(
            SCTLR_REQUIRED_BITS & ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 12) | (1 << 19)),
            SCTLR_REQUIRED_BITS
        );
        assert_eq!(
            CpuFeatures::from_mmfr0(0xf << 28),
            Err(PagingError::FourKiBGranuleUnsupported)
        );
        assert_eq!(
            CpuFeatures::from_mmfr0(1 << 28),
            Err(PagingError::FourKiBGranuleUnsupported)
        );
        assert_eq!(
            CpuFeatures::from_mmfr0(6),
            Err(PagingError::PhysicalAddressRangeUnsupported)
        );
    }
}
