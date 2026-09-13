//! Polling PL011 diagnostics for QEMU `virt` serial first boot.

const PL011_BASE: usize = 0x0900_0000;
const DATA: *mut u32 = PL011_BASE as *mut u32;
const FLAGS: *const u32 = (PL011_BASE + 0x18) as *const u32;
const TX_FULL: u32 = 1 << 5;

/// Write one ASCII string to QEMU `virt`'s first PL011.
pub fn line(value: &str) {
    for byte in value.bytes() {
        write(if byte == b'\n' { b'\r' } else { byte });
        if byte == b'\n' {
            write(b'\n');
        }
    }
}

/// Write raw bytes to PL011.
pub fn write_bytes(bytes: &[u8]) {
    for &byte in bytes {
        if byte == b'\n' {
            write(b'\r');
        }
        write(byte);
    }
}

/// Write an allocation-free diagnostic label and one fixed-width hexadecimal value.
pub fn hex_line(label: &str, value: u64) {
    for byte in label.bytes() {
        write(byte);
    }
    for shift in (0..16).rev() {
        let digit = ((value >> (shift * 4)) & 0xf) as u8;
        write(if digit < 10 {
            b'0' + digit
        } else {
            b'a' + digit - 10
        });
    }
    write(b'\r');
    write(b'\n');
}

/// Write an allocation-free diagnostic label and one decimal value.
pub fn dec_line(label: &str, mut value: u64) {
    for byte in label.bytes() {
        write(byte);
    }
    if value == 0 {
        write(b'0');
    } else {
        let mut buffer = [0u8; 20];
        let mut count = 0;
        while value > 0 {
            buffer[count] = b'0' + (value % 10) as u8;
            value /= 10;
            count += 1;
        }
        for byte in buffer[..count].iter().rev() {
            write(*byte);
        }
    }
    write(b'\r');
    write(b'\n');
}

fn write(value: u8) {
    // SAFETY: R3 supports only QEMU `virt`, whose first PL011 is fixed at
    // 0x0900_0000. This BSP-only path polls the FIFO and performs one MMIO write.
    unsafe {
        while core::ptr::read_volatile(FLAGS) & TX_FULL != 0 {}
        core::ptr::write_volatile(DATA, u32::from(value));
    }
}
