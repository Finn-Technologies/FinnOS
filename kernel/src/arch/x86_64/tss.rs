//! `FinnOS`-owned x86-64 Task State Segment.

/// Size of the dedicated double-fault IST stack.
const DOUBLE_FAULT_STACK_SIZE: usize = 64 * 1024;

/// Alignment for the dedicated double-fault IST stack.
const DOUBLE_FAULT_STACK_ALIGN: usize = 4096;

/// x86-64 Task State Segment.
///
/// Only the fields used by the early kernel are represented. Reserved fields are explicitly
/// zero-initialized.
///
/// The layout matches the x86-64 architectural TSS. The original `u64` reserved fields at offsets
/// 28 and 92 are represented as pairs of `u32` so the struct remains naturally aligned without
/// needing `#[repr(C, packed)]`.
#[repr(C)]
pub struct TSS {
    _reserved0: u32,
    /// Privilege level 0 stack pointer (low half).
    pub rsp0_low: u32,
    /// Privilege level 0 stack pointer (high half).
    pub rsp0_high: u32,
    /// Privilege level 1 stack pointer (low half).
    pub rsp1_low: u32,
    /// Privilege level 1 stack pointer (high half).
    pub rsp1_high: u32,
    /// Privilege level 2 stack pointer (low half).
    pub rsp2_low: u32,
    /// Privilege level 2 stack pointer (high half).
    pub rsp2_high: u32,
    _reserved1_low: u32,
    _reserved1_high: u32,
    /// Interrupt stack table entries.
    pub ist: [ISTEntry; 7],
    _reserved2_low: u32,
    _reserved2_high: u32,
    _reserved3: u16,
    /// I/O map base address. Set to the size of the TSS so no bitmap is exposed.
    pub io_map_base: u16,
}

/// A single Interrupt Stack Table entry, split into low and high halves.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ISTEntry {
    /// Low 32 bits of the stack pointer.
    pub low: u32,
    /// High 32 bits of the stack pointer.
    pub high: u32,
}

impl Default for TSS {
    fn default() -> Self {
        Self::new()
    }
}

impl TSS {
    /// Create a zero-initialized TSS.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            _reserved0: 0,
            rsp0_low: 0,
            rsp0_high: 0,
            rsp1_low: 0,
            rsp1_high: 0,
            rsp2_low: 0,
            rsp2_high: 0,
            _reserved1_low: 0,
            _reserved1_high: 0,
            ist: [ISTEntry { low: 0, high: 0 }; 7],
            _reserved2_low: 0,
            _reserved2_high: 0,
            _reserved3: 0,
            io_map_base: 0,
        }
    }

    /// Set the RSP0 field to a 64-bit stack top.
    #[allow(clippy::cast_possible_truncation)]
    pub const fn set_rsp0(&mut self, value: u64) {
        self.rsp0_low = value as u32;
        self.rsp0_high = (value >> 32) as u32;
    }

    /// Set an IST entry to a 64-bit stack top.
    #[allow(clippy::cast_possible_truncation)]
    pub const fn set_ist(&mut self, index: usize, value: u64) {
        self.ist[index].low = value as u32;
        self.ist[index].high = (value >> 32) as u32;
    }

    /// Return the I/O map base value.
    #[must_use]
    pub const fn io_map_base(&self) -> u16 {
        self.io_map_base
    }
}

/// Dedicated double-fault IST stack storage.
#[repr(C, align(4096))]
struct DoubleFaultStack {
    data: [u8; DOUBLE_FAULT_STACK_SIZE],
}

// SAFETY: The linker places the dedicated IST storage above the guarded early stack.
#[allow(unsafe_code)]
#[unsafe(link_section = ".kernel_after_stack")]
static mut DOUBLE_FAULT_STACK: DoubleFaultStack = DoubleFaultStack {
    data: [0; DOUBLE_FAULT_STACK_SIZE],
};

/// Return the physical/identity address of the dedicated double-fault stack.
#[must_use]
#[allow(unsafe_code)]
pub fn double_fault_stack_start() -> u64 {
    core::ptr::addr_of!(DOUBLE_FAULT_STACK) as u64
}

/// Initialize the TSS with a valid RSP0 and a dedicated double-fault IST stack.
///
/// # Safety
///
/// Must be called once on the BSP before loading the task register.
#[allow(unsafe_code, clippy::cast_possible_truncation)]
pub unsafe fn init(tss: &mut TSS, rsp0: u64) {
    tss.set_rsp0(rsp0);
    // SAFETY: `DOUBLE_FAULT_STACK` is a static array accessed only during init.
    let stack_top = unsafe {
        (&raw const DOUBLE_FAULT_STACK)
            .cast::<u8>()
            .add(DOUBLE_FAULT_STACK_SIZE)
    } as u64;
    tss.set_ist(0, stack_top);
    // I/O map base set to the TSS size so no bitmap is exposed.
    tss.io_map_base = core::mem::size_of::<TSS>() as u16;
}

/// Return the top of the dedicated double-fault IST stack.
#[must_use]
#[allow(unsafe_code)]
pub fn double_fault_stack_top() -> u64 {
    // SAFETY: `DOUBLE_FAULT_STACK` is a static array; the top address is stable.
    unsafe {
        (&raw const DOUBLE_FAULT_STACK)
            .cast::<u8>()
            .add(DOUBLE_FAULT_STACK_SIZE) as u64
    }
}

/// Return the size of the dedicated double-fault IST stack.
#[must_use]
pub const fn double_fault_stack_size() -> usize {
    DOUBLE_FAULT_STACK_SIZE
}

/// Return the alignment of the dedicated double-fault IST stack.
#[must_use]
pub const fn double_fault_stack_align() -> usize {
    DOUBLE_FAULT_STACK_ALIGN
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tss_size_is_at_least_104_bytes() {
        assert!(core::mem::size_of::<TSS>() >= 104);
    }

    #[test]
    fn ist_entry_round_trips() {
        let mut tss = TSS::new();
        tss.set_ist(0, 0x1234_5678_9abc_def0);
        assert_eq!(tss.ist[0].low, 0x9abc_def0);
        assert_eq!(tss.ist[0].high, 0x1234_5678);
    }

    #[test]
    fn rsp0_round_trips() {
        let mut tss = TSS::new();
        tss.set_rsp0(0xfedc_ba98_7654_3210);
        assert_eq!(tss.rsp0_low, 0x7654_3210);
        assert_eq!(tss.rsp0_high, 0xfedc_ba98);
    }

    #[test]
    fn double_fault_stack_is_64k() {
        assert_eq!(double_fault_stack_size(), 64 * 1024);
    }

    #[test]
    fn double_fault_stack_alignment_is_page() {
        assert_eq!(double_fault_stack_align(), 4096);
    }

    #[test]
    fn tss_layout_matches_architecture() {
        use core::mem::{offset_of, size_of};
        assert_eq!(size_of::<TSS>(), 104);
        assert_eq!(offset_of!(TSS, rsp0_low), 4);
        assert_eq!(offset_of!(TSS, ist), 36);
        assert_eq!(offset_of!(TSS, io_map_base), 102);
    }

    #[test]
    #[allow(unsafe_code)]
    fn tss_ist1_matches_double_fault_stack_top() {
        let mut tss = TSS::new();
        unsafe {
            init(&mut tss, 0);
        }
        let ist1 = (u64::from(tss.ist[0].high) << 32) | u64::from(tss.ist[0].low);
        assert_eq!(ist1, double_fault_stack_top());
    }

    #[test]
    fn tss_limit_matches_size_minus_one() {
        assert_eq!(core::mem::size_of::<TSS>() - 1, 103);
    }
}
