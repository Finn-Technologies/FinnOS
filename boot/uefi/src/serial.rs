//! UEFI boot-manager serial diagnostics.

/// COM1 base address.
#[cfg(target_arch = "x86_64")]
pub const COM1: u16 = 0x3f8;

#[cfg(target_arch = "aarch64")]
const PL011_BASE: usize = 0x0900_0000;
#[cfg(target_arch = "aarch64")]
const PL011_DATA: *mut u32 = PL011_BASE as *mut u32;
#[cfg(target_arch = "aarch64")]
const PL011_FLAGS: *const u32 = (PL011_BASE + 0x18) as *const u32;
#[cfg(target_arch = "aarch64")]
const PL011_TX_FULL: u32 = 1 << 5;

/// Initialize the target's early serial diagnostics.
#[cfg(target_arch = "x86_64")]
pub fn init() {
    // SAFETY: These are the standard COM1 UART registers in firmware's x86-64 environment.
    unsafe {
        out(COM1 + 1, 0);
        out(COM1 + 3, 0x80);
        out(COM1, 1);
        out(COM1 + 1, 0);
        out(COM1 + 3, 3);
        out(COM1 + 2, 0xc7);
        out(COM1 + 4, 3);
    }
}

/// Preserve the PL011 configuration established by AAVMF.
#[cfg(target_arch = "aarch64")]
pub const fn init() {}

/// Write an ASCII diagnostic line to the target's early serial device.
pub fn line(value: &str) {
    for byte in value.bytes() {
        #[cfg(target_arch = "x86_64")]
        {
            // SAFETY: Reading the COM1 line-status register is valid for this UART.
            while unsafe { inp(COM1 + 5) } & 0x20 == 0 {}
            // SAFETY: Writing the COM1 data register is valid for this UART.
            unsafe {
                out(COM1, if byte == b'\n' { b'\r' } else { byte });
            }
            if byte == b'\n' {
                // SAFETY: Reading and writing the COM1 data register are valid for this UART.
                while unsafe { inp(COM1 + 5) } & 0x20 == 0 {}
                unsafe {
                    out(COM1, b'\n');
                }
            }
        }
        #[cfg(target_arch = "aarch64")]
        {
            write_pl011(if byte == b'\n' { b'\r' } else { byte });
            if byte == b'\n' {
                write_pl011(b'\n');
            }
        }
    }
}

/// Write a compact hexadecimal diagnostic number.
pub fn hex(prefix: &str, value: u64) {
    line(prefix);
    let mut digits = [b'0'; 16];
    let mut number = value;
    for index in (0..16).rev() {
        digits[index] = b"0123456789abcdef"[(number & 0xf) as usize];
        number >>= 4;
    }
    // SAFETY: The table contains only ASCII bytes and is valid UTF-8.
    line(unsafe { core::str::from_utf8_unchecked(&digits) });
    line("\n");
}

#[cfg(target_arch = "x86_64")]
unsafe fn out(port: u16, value: u8) {
    // SAFETY: The caller guarantees that COM1 is initialized and the port is valid.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags));
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn inp(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The caller guarantees that COM1 is initialized and the port is valid.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nomem, nostack, preserves_flags));
    }
    value
}

#[cfg(target_arch = "aarch64")]
fn write_pl011(value: u8) {
    // SAFETY: QEMU virt fixes its first PL011 at 0x0900_0000; firmware has
    // initialized it and R3 uses it only for polling, single-core diagnostics.
    unsafe {
        while core::ptr::read_volatile(PL011_FLAGS) & PL011_TX_FULL != 0 {}
        core::ptr::write_volatile(PL011_DATA, u32::from(value));
    }
}
