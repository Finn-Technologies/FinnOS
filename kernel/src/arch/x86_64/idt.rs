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
            options(nomem, nostack, preserves_flags)
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
}
