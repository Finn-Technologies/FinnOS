//! Legacy 8259 PIC ownership. Both controllers remain fully masked.
/// Remapped master PIC vector.
pub const MASTER_OFFSET: u8 = 0x20;
/// Remapped slave PIC vector.
pub const SLAVE_OFFSET: u8 = 0x28;
const MASTER_COMMAND: u16 = 0x20;
const MASTER_DATA: u16 = 0x21;
const SLAVE_COMMAND: u16 = 0xa0;
const SLAVE_DATA: u16 = 0xa1;
const IO_WAIT: u16 = 0x80;

/// PIC initialization failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PicError {
    /// Hardware readback did not retain the required mask.
    ReadbackFailed {
        /// Master mask read back from port 0x21.
        master: u8,
        /// Slave mask read back from port 0xa1.
        slave: u8,
    },
}

/// Read the current master and slave masks.
#[allow(unsafe_code)]
pub fn masks() -> (u8, u8) {
    // SAFETY: These are documented byte-wide PIC data ports; reads have no memory aliasing.
    unsafe { (in_u8(MASTER_DATA), in_u8(SLAVE_DATA)) }
}

#[allow(unsafe_code)]
unsafe fn out_u8(port: u16, value: u8) {
    // SAFETY: The caller supplies one of the documented byte-wide 8259 ports.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nostack, preserves_flags))
    };
}

#[allow(unsafe_code)]
unsafe fn in_u8(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The caller supplies one of the documented byte-wide 8259 ports.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nostack, preserves_flags))
    };
    value
}

#[allow(unsafe_code)]
fn wait() {
    // SAFETY: Port 0x80 is the conventional harmless I/O delay port on x86 firmware/QEMU.
    unsafe { out_u8(IO_WAIT, 0) };
}

/// Remap both PICs and verify that every legacy IRQ remains masked.
///
/// # Errors
///
/// Returns an error if either mask fails readback verification.
#[allow(unsafe_code)]
pub fn initialize() -> Result<(u8, u8), PicError> {
    // SAFETY: Initialization runs on the BSP while IF is clear and uses byte-width accesses.
    unsafe {
        out_u8(MASTER_COMMAND, 0x11);
        wait();
        out_u8(SLAVE_COMMAND, 0x11);
        wait();
        out_u8(MASTER_DATA, MASTER_OFFSET);
        wait();
        out_u8(SLAVE_DATA, SLAVE_OFFSET);
        wait();
        out_u8(MASTER_DATA, 0x04);
        wait();
        out_u8(SLAVE_DATA, 0x02);
        wait();
        out_u8(MASTER_DATA, 0x01);
        wait();
        out_u8(SLAVE_DATA, 0x01);
        wait();
        out_u8(MASTER_DATA, 0xff);
        out_u8(SLAVE_DATA, 0xff);
        let master = in_u8(MASTER_DATA);
        let slave = in_u8(SLAVE_DATA);
        if master != 0xff || slave != 0xff {
            return Err(PicError::ReadbackFailed { master, slave });
        }
        Ok((master, slave))
    }
}

/// Pure validation used by host tests and diagnostics.
#[must_use]
pub const fn masks_are_fully_masked(master: u8, slave: u8) -> bool {
    master == 0xff && slave == 0xff
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn policy_is_remapped_and_fully_masked() {
        assert_eq!(MASTER_OFFSET, 0x20);
        assert_eq!(SLAVE_OFFSET, 0x28);
        assert!(masks_are_fully_masked(0xff, 0xff));
        assert!(!masks_are_fully_masked(0xfe, 0xff));
    }
}
