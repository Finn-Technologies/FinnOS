//! UEFI boot-manager serial diagnostics.

/// COM1 base address.
pub const COM1: u16 = 0x3f8;

/// Initialize COM1 for 115200 8-N-1 diagnostics.
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

/// Write an ASCII diagnostic line to COM1.
pub fn line(value: &str) {
    for byte in value.bytes() {
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

unsafe fn out(port: u16, value: u8) {
    // SAFETY: The caller guarantees that COM1 is initialized and the port is valid.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags));
    }
}

unsafe fn inp(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The caller guarantees that COM1 is initialized and the port is valid.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nomem, nostack, preserves_flags));
    }
    value
}
