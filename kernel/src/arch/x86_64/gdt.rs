//! `FinnOS`-owned x86-64 Global Descriptor Table.

use super::tss::TSS;

/// Number of 8-byte GDT entries.
///
/// Layout: null, kernel code, kernel data, TSS descriptor low, TSS descriptor high, plus two
/// spare entries for future expansion.
const GDT_ENTRIES: usize = 8;

/// GDT storage. Must remain valid permanently after load.
static mut GDT: [u64; GDT_ENTRIES] = [0; GDT_ENTRIES];

/// Segment selector for the kernel code descriptor.
pub const KERNEL_CODE_SELECTOR: u16 = 0x08;

/// Segment selector for the kernel data descriptor.
pub const KERNEL_DATA_SELECTOR: u16 = 0x10;

/// Segment selector for the TSS descriptor.
pub const TSS_SELECTOR: u16 = 0x18;

/// GDT pseudo-descriptor passed to `lgdt`.
#[repr(C, packed)]
struct GdtPointer {
    limit: u16,
    base: u64,
}

/// Encode a 64-bit code or data segment descriptor.
#[must_use]
#[allow(clippy::cast_possible_truncation)]
const fn encode_descriptor(base: u32, limit: u32, access: u8, flags: u8) -> u64 {
    let mut descriptor: u64 = 0;
    descriptor |= (limit & 0xffff) as u64;
    descriptor |= ((base & 0x00ff_ffff) as u64) << 16;
    descriptor |= (access as u64) << 40;
    descriptor |= ((limit >> 16) as u64 & 0xf) << 48;
    descriptor |= ((flags & 0xf) as u64) << 52;
    descriptor |= ((base >> 24) as u64 & 0xff) << 56;
    descriptor
}

/// Encode a 64-bit kernel code descriptor.
#[must_use]
pub const fn kernel_code_descriptor() -> u64 {
    // Present, ring 0, code, executable, conforming=0, readable.
    const ACCESS: u8 = 0b1001_1010;
    // Long mode, 64-bit, granularity=0 (limit ignored in long mode).
    const FLAGS: u8 = 0b1010;
    encode_descriptor(0, 0xfffff, ACCESS, FLAGS)
}

/// Encode a 64-bit kernel data descriptor.
#[must_use]
pub const fn kernel_data_descriptor() -> u64 {
    // Present, ring 0, data, writable.
    const ACCESS: u8 = 0b1001_0010;
    const FLAGS: u8 = 0b0000;
    encode_descriptor(0, 0xfffff, ACCESS, FLAGS)
}

/// Encode the low half of a 64-bit available TSS descriptor.
#[must_use]
#[allow(clippy::cast_possible_truncation)]
pub const fn tss_descriptor_low(tss_base: u64, tss_limit: u32) -> u64 {
    // Present, ring 0, available 64-bit TSS.
    const TSS_ACCESS: u64 = 0b1000_1001;
    let base = tss_base as u32;
    let limit = tss_limit & 0x000f_ffff;
    let mut descriptor: u64 = (limit & 0xffff) as u64;
    descriptor |= ((base & 0x00ff_ffff) as u64) << 16;
    descriptor |= TSS_ACCESS << 40;
    descriptor |= (((limit >> 16) & 0xf) as u64) << 48;
    descriptor |= ((base >> 24) as u64) << 56;
    descriptor
}

/// Encode the high half of a 64-bit available TSS descriptor.
#[must_use]
pub const fn tss_descriptor_high(tss_base: u64) -> u64 {
    tss_base >> 32
}

/// Segment selector value given a GDT index and requested privilege level.
#[must_use]
pub const fn selector(index: u16, rpl: u8) -> u16 {
    (index << 3) | (rpl as u16 & 0x3)
}

/// Initialize and load the `FinnOS` GDT.
///
/// # Safety
///
/// Must be called once, with interrupts disabled, on the BSP before any task switch.
#[allow(unsafe_code)]
pub unsafe fn init(tss: &TSS) {
    // `GDT` is a static mutable array accessed only during single-core init.
    let gdt = &raw mut GDT;
    unsafe {
        (*gdt)[0] = 0;
        (*gdt)[1] = kernel_code_descriptor();
        (*gdt)[2] = kernel_data_descriptor();
    }
    let tss_base = core::ptr::from_ref(tss) as usize as u64;
    let tss_limit = core::mem::size_of::<TSS>() as u64 - 1;
    unsafe {
        #[allow(clippy::cast_possible_truncation)]
        let tss_limit_u32 = tss_limit as u32;
        (*gdt)[3] = tss_descriptor_low(tss_base, tss_limit_u32);
        (*gdt)[4] = tss_descriptor_high(tss_base);
    }

    let pointer = GdtPointer {
        #[allow(clippy::cast_possible_truncation)]
        limit: (core::mem::size_of::<[u64; GDT_ENTRIES]>() - 1) as u16,
        base: gdt as u64,
    };

    // SAFETY: `pointer` describes the permanently resident `GDT` array.
    unsafe {
        core::arch::asm! {
            "lgdt [rdi]",
            in("rdi") core::ptr::addr_of!(pointer),
            options(nostack, preserves_flags)
        };
    }
}

/// Reload segment registers after loading a new GDT.
///
/// # Safety
///
/// Must be called after `init` with a valid GDT containing the expected descriptors.
#[allow(unsafe_code)]
pub unsafe fn reload_segments() {
    // SAFETY: The GDT contains a valid kernel data descriptor at selector 0x10.
    unsafe {
        core::arch::asm! {
            "mov ax, {data_selector}",
            "mov ds, ax",
            "mov es, ax",
            "mov fs, ax",
            "mov gs, ax",
            "mov ss, ax",
            data_selector = const KERNEL_DATA_SELECTOR,
            out("ax") _,
            options(nomem, nostack)
        };
    }

    // SAFETY: The GDT contains a valid kernel code descriptor at selector 0x08.
    // A far return is used to reload CS with the new code selector. This sequence
    // pushes a return address and selector onto the stack, so no `nomem` or `nostack`
    // option is declared.
    unsafe {
        core::arch::asm! {
            "push {code_selector}",
            "lea rax, [rip + 2f]",
            "push rax",
            "retfq",
            "2:",
            code_selector = const KERNEL_CODE_SELECTOR as u64,
            out("rax") _,
        };
    }
}

/// Load the task register with the TSS selector.
///
/// # Safety
///
/// The GDT must contain a valid TSS descriptor at `TSS_SELECTOR`.
#[allow(unsafe_code)]
pub unsafe fn load_task_register() {
    // SAFETY: `TSS_SELECTOR` points to a valid TSS descriptor installed by `init`.
    unsafe {
        core::arch::asm! {
            "ltr ax",
            in("ax") TSS_SELECTOR,
            options(nomem, nostack, preserves_flags)
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn null_descriptor_is_zero() {
        assert_eq!(encode_descriptor(0, 0, 0, 0), 0);
    }

    #[test]
    #[allow(clippy::cast_possible_truncation)]
    fn kernel_code_has_expected_access_and_flags() {
        let descriptor = kernel_code_descriptor();
        let access = (descriptor >> 40) as u8;
        let flags = (descriptor >> 52) as u8 & 0xf;
        assert_eq!(access, 0x9a);
        assert_eq!(flags, 0xa);
    }

    #[test]
    #[allow(clippy::cast_possible_truncation)]
    fn kernel_data_has_expected_access() {
        let descriptor = kernel_data_descriptor();
        let access = (descriptor >> 40) as u8;
        assert_eq!(access, 0x92);
    }

    #[test]
    #[allow(clippy::cast_possible_truncation)]
    fn tss_low_access_byte_is_available_tss() {
        let descriptor = tss_descriptor_low(0x1234_5678_9abc_def0, 0x1_ffff);
        let access = (descriptor >> 40) as u8;
        assert_eq!(access, 0x89);
    }

    #[test]
    fn tss_high_carries_upper_base() {
        let base = 0x0000_1234_5678_9abc_u64;
        let high = tss_descriptor_high(base);
        assert_eq!(high, 0x0000_1234);

        let base2 = 0xffff_ffff_0000_0000_u64;
        let high2 = tss_descriptor_high(base2);
        assert_eq!(high2, 0xffff_ffff);
    }

    #[test]
    fn selector_encoding() {
        assert_eq!(selector(1, 0), 0x08);
        assert_eq!(selector(2, 0), 0x10);
        assert_eq!(selector(3, 0), 0x18);
        assert_eq!(selector(1, 3), 0x0b);
    }

    #[test]
    fn tss_descriptor_round_trips_base_and_limit() {
        let base = 0x1234_5678_9abc_def0_u64;
        let limit = 0x000f_ffff_u32;
        let low = tss_descriptor_low(base, limit);
        let high = tss_descriptor_high(base);

        let decoded_limit = (low & 0xffff) as u32 | (((low >> 48) & 0xf) as u32) << 16;
        let decoded_base =
            ((low >> 16) & 0x00ff_ffff) | (((low >> 56) & 0xff) << 24) | (high << 32);

        assert_eq!(decoded_limit, limit);
        assert_eq!(decoded_base, base);
    }
}
