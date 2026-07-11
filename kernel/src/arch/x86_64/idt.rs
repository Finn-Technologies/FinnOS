//! FinnOS-owned x86-64 Interrupt Descriptor Table.

use super::gdt::KERNEL_CODE_SELECTOR;

/// Number of IDT entries.
const IDT_ENTRIES: usize = 256;

/// IDT storage. Must remain valid permanently after load.
static mut IDT: [IdtEntry; IDT_ENTRIES] = [IdtEntry::empty(); IDT_ENTRIES];

/// x86-64 IDT gate descriptor.
#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct IdtEntry {
    /// Low 16 bits of the handler offset.
    offset_low: u16,
    /// Code segment selector.
    selector: u16,
    /// IST index and reserved bits.
    ist: u8,
    /// Type and attributes.
    type_attr: u8,
    /// Middle 16 bits of the handler offset.
    offset_mid: u16,
    /// High 32 bits of the handler offset.
    offset_high: u32,
    /// Reserved.
    _reserved: u32,
}

impl IdtEntry {
    /// Create an empty, non-present IDT entry.
    #[must_use]
    pub const fn empty() -> IdtEntry {
        IdtEntry {
            offset_low: 0,
            selector: 0,
            ist: 0,
            type_attr: 0,
            offset_mid: 0,
            offset_high: 0,
            _reserved: 0,
        }
    }

    /// Encode an IDT entry from a handler address, IST index, and type attributes.
    #[allow(clippy::cast_possible_truncation)]
    pub fn set(&mut self, handler: u64, ist: u8, type_attr: u8) {
        self.offset_low = (handler & 0xffff) as u16;
        self.offset_mid = ((handler >> 16) & 0xffff) as u16;
        self.offset_high = ((handler >> 32) & 0xffff_ffff) as u32;
        self.selector = KERNEL_CODE_SELECTOR;
        self.ist = ist & 0x7;
        self.type_attr = type_attr;
    }

    /// Return the reconstructed 64-bit handler offset.
    #[must_use]
    pub fn offset(&self) -> u64 {
        let low = self.offset_low as u64;
        let mid = (self.offset_mid as u64) << 16;
        let high = (self.offset_high as u64) << 32;
        low | mid | high
    }

    /// Return the IST index.
    #[must_use]
    pub fn ist(&self) -> u8 {
        self.ist & 0x7
    }

    /// Return the type and attribute byte.
    #[must_use]
    pub fn type_attr(&self) -> u8 {
        self.type_attr
    }
}

/// IDT pseudo-descriptor passed to `lidt`.
#[repr(C, packed)]
struct IdtPointer {
    limit: u16,
    base: u64,
}

/// Attribute byte for a present ring-0 interrupt gate.
pub const IDT_INTERRUPT_GATE: u8 = 0x8e;

/// Attribute byte for a present ring-0 trap gate.
pub const IDT_TRAP_GATE: u8 = 0x8f;

/// Addresses of the first 32 exception handler entry points.
///
/// This structure is intentionally host-testable: it carries raw handler offsets so the IDT
/// construction logic can be validated without linking the assembly stubs.
pub struct HandlerAddresses {
    /// Handler offset for each vector 0–31.
    pub handlers: [u64; 32],
}

impl HandlerAddresses {
    /// Create a new handler-address table initialized to zero.
    #[must_use]
    pub const fn new() -> HandlerAddresses {
        HandlerAddresses { handlers: [0; 32] }
    }
}

/// Build the early exception IDT from a table of handler addresses.
///
/// The returned table has entries for vectors 0–31 configured as present ring-0 gates. Vector 3
/// uses a trap gate so it can be used as a resumable breakpoint; vector 8 (double fault) uses
/// IST index 1; all other early vectors use IST index 0.
#[must_use]
pub fn build_exception_idt(addresses: &HandlerAddresses) -> [IdtEntry; IDT_ENTRIES] {
    let mut idt = [IdtEntry::empty(); IDT_ENTRIES];
    for vector in 0..32 {
        let ist = if vector == 8 { 1 } else { 0 };
        let gate_type = if vector == 3 {
            IDT_TRAP_GATE
        } else {
            IDT_INTERRUPT_GATE
        };
        idt[vector].set(addresses.handlers[vector], ist, gate_type | 0x80);
    }
    idt
}

/// Copy a built IDT into the runtime static IDT storage.
///
/// # Safety
///
/// Must be called once on the BSP before `load`. The source IDT must be valid.
#[allow(unsafe_code)]
pub unsafe fn install(idt: [IdtEntry; IDT_ENTRIES]) {
    // SAFETY: `IDT` is a static array accessed only during single-core init.
    unsafe {
        IDT = idt;
    }
}

/// Install a handler into the IDT.
///
/// # Safety
///
/// `vector` must be within the IDT range. The handler must be a valid x86-64 entry point.
#[allow(unsafe_code)]
pub unsafe fn set_handler(vector: usize, handler: u64, ist: u8, gate_type: u8) {
    if vector >= IDT_ENTRIES {
        return;
    }
    // SAFETY: `IDT` is a static array accessed only during single-core init.
    unsafe {
        IDT[vector].set(handler, ist, gate_type | 0x80);
    }
}

/// Return the handler offset for a vector.
#[must_use]
#[allow(unsafe_code)]
pub fn handler_offset(vector: usize) -> Option<u64> {
    if vector >= IDT_ENTRIES {
        return None;
    }
    // SAFETY: `IDT` is a static array; reads are safe after init.
    Some(unsafe { IDT[vector].offset() })
}

/// Return the IST index for a vector.
#[must_use]
#[allow(unsafe_code)]
pub fn handler_ist(vector: usize) -> Option<u8> {
    if vector >= IDT_ENTRIES {
        return None;
    }
    // SAFETY: `IDT` is a static array; reads are safe after init.
    Some(unsafe { IDT[vector].ist() })
}

/// Load the IDT into the processor.
///
/// # Safety
///
/// Must be called after all handlers have been installed. The IDT must remain valid.
#[allow(unsafe_code)]
pub unsafe fn load() {
    // `IDT` is a permanently resident static array.
    let idt = &raw const IDT;
    let pointer = IdtPointer {
        limit: (core::mem::size_of::<[IdtEntry; IDT_ENTRIES]>() - 1) as u16,
        base: idt as u64,
    };

    // SAFETY: `pointer` describes the permanently resident `IDT` array.
    unsafe {
        core::arch::asm! {
            "lidt [rdi]",
            in("rdi") &pointer,
            options(nostack, preserves_flags)
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_entry_is_not_present() {
        let entry = IdtEntry::empty();
        assert_eq!(entry.type_attr(), 0);
        assert_eq!(entry.offset(), 0);
    }

    #[test]
    fn offset_round_trips() {
        let mut entry = IdtEntry::empty();
        entry.set(0x1234_5678_9abc_def0, 1, IDT_INTERRUPT_GATE);
        assert_eq!(entry.offset(), 0x1234_5678_9abc_def0);
        assert_eq!(entry.ist(), 1);
        assert_eq!(entry.type_attr(), IDT_INTERRUPT_GATE | 0x80);
    }

    #[test]
    fn ist_is_masked() {
        let mut entry = IdtEntry::empty();
        entry.set(0, 0xff, IDT_TRAP_GATE);
        assert_eq!(entry.ist(), 7);
    }

    #[test]
    fn ist_index_round_trips() {
        let mut entry = IdtEntry::empty();
        entry.set(0x1234, 1, IDT_INTERRUPT_GATE);
        assert_eq!(entry.ist(), 1);
        entry.set(0x1234, 0, IDT_INTERRUPT_GATE);
        assert_eq!(entry.ist(), 0);
    }

    #[test]
    fn build_exception_idt_sets_double_fault_ist_one() {
        let mut addresses = HandlerAddresses::new();
        addresses.handlers[8] = 0x1234_5678_9abc_def0;
        let idt = build_exception_idt(&addresses);
        assert_eq!(idt[8].offset(), 0x1234_5678_9abc_def0);
        assert_eq!(idt[8].ist(), 1);
        assert_eq!(idt[8].type_attr(), IDT_INTERRUPT_GATE | 0x80);
    }

    #[test]
    fn build_exception_idt_uses_kernel_code_selector() {
        let addresses = HandlerAddresses::new();
        let idt = build_exception_idt(&addresses);
        for vector in 0..32 {
            // The selector field is stored as a u16; reconstruct it from the packed entry.
            let selector = (idt[vector].selector as u16) & 0xfff8;
            assert_eq!(selector, KERNEL_CODE_SELECTOR);
        }
    }

    #[test]
    fn build_exception_idt_ordinary_vectors_use_ist_zero() {
        let addresses = HandlerAddresses::new();
        let idt = build_exception_idt(&addresses);
        for vector in 0..32 {
            if vector == 8 {
                continue;
            }
            assert_eq!(idt[vector].ist(), 0, "vector {} should not use IST", vector);
        }
    }

    #[test]
    fn build_exception_idt_breakpoint_is_trap_gate() {
        let mut addresses = HandlerAddresses::new();
        addresses.handlers[3] = 0xdead_beef;
        let idt = build_exception_idt(&addresses);
        assert_eq!(idt[3].type_attr(), IDT_TRAP_GATE | 0x80);
    }
}
