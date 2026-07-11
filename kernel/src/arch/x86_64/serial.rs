//! COM1 serial output.

use core::fmt;
use core::fmt::Write;

const COM1: u16 = 0x3f8;

/// Minimal COM1 writer.
pub struct Serial;

impl Serial {
    /// Configure COM1 for 115200 8-N-1.
    #[allow(clippy::unused_self)]
    pub fn init(&mut self) {
        out(COM1 + 1, 0);
        out(COM1 + 3, 0x80);
        out(COM1, 1);
        out(COM1 + 1, 0);
        out(COM1 + 3, 3);
        out(COM1 + 2, 0xc7);
        out(COM1 + 4, 3);
    }
    #[allow(unsafe_code, clippy::unused_self)]
    fn write_byte(&self, byte: u8) {
        while inp(COM1 + 5) & 0x20 == 0 {}
        out(COM1, byte);
    }
}

impl fmt::Write for Serial {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        for byte in value.bytes() {
            if byte == b'\n' {
                self.write_byte(b'\r');
            }
            self.write_byte(byte);
        }
        Ok(())
    }
}

/// Write a formatted line to COM1.
pub fn log(args: fmt::Arguments<'_>) {
    let mut serial = Serial;
    serial.init();
    let _ = serial.write_fmt(args);
}

/// Format and write a line to the early COM1 logger.
#[macro_export]
macro_rules! serial_log { ($($arg:tt)*) => { $crate::arch::x86_64::serial::log(core::format_args!($($arg)*)); }; }

#[allow(unsafe_code)]
fn out(port: u16, value: u8) {
    // SAFETY: The early kernel serial path writes only to the COM1 UART ports.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags));
    }
}
#[allow(unsafe_code)]
fn inp(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The early kernel serial path reads only from the COM1 UART ports.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nomem, nostack, preserves_flags));
    }
    value
}
