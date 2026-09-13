//! PS/2 mouse driver for x86-64.
//!
//! Handles i8042 auxiliary device initialization and 3-byte standard PS/2 packet decoding.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss
)]

/// A decoded PS/2 mouse packet with relative motion deltas and button states.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct MousePacket {
    /// Relative horizontal movement (positive = right, negative = left).
    pub dx: i32,
    /// Relative vertical movement in screen space (positive = down, negative = up).
    pub dy: i32,
    /// Left mouse button state.
    pub left_button: bool,
    /// Right mouse button state.
    pub right_button: bool,
    /// Middle mouse button state.
    pub middle_button: bool,
}

/// PS/2 mouse controller state machine.
pub struct Ps2Mouse {
    cycle: u8,
    packet: [u8; 3],
}

impl Default for Ps2Mouse {
    fn default() -> Self {
        Self::new()
    }
}

impl Ps2Mouse {
    /// Create a new PS/2 mouse decoder.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            cycle: 0,
            packet: [0; 3],
        }
    }

    /// Initialize the PS/2 mouse hardware via the i8042 controller.
    #[allow(unsafe_code)]
    pub fn init(&mut self) {
        // Enable auxiliary mouse port
        Self::wait_write();
        out(0x64, 0xA8);

        // Read Compaq status / command byte
        Self::wait_write();
        out(0x64, 0x20);
        let mut status = Self::read_data();

        // Bit 1: enable mouse interrupt (IRQ 12)
        // Bit 5: clear mouse clock disable
        status |= 0x02;
        status &= !0x20;

        Self::wait_write();
        out(0x64, 0x60);
        Self::wait_write();
        out(0x60, status);

        // Set default sampling/scaling settings (command 0xF6)
        Self::write_mouse(0xF6);
        let _ = Self::read_data(); // ACK (0xFA)

        // Enable data streaming / reporting (command 0xF4)
        Self::write_mouse(0xF4);
        let _ = Self::read_data(); // ACK (0xFA)

        // Flush any residual bytes from previous states
        for _ in 0..16 {
            if (inp(0x64) & 0x01) != 0 {
                let _ = inp(0x60);
            } else {
                break;
            }
        }
    }

    fn wait_write() {
        for _ in 0..100_000 {
            if (inp(0x64) & 0x02) == 0 {
                return;
            }
        }
    }

    fn read_data() -> u8 {
        for _ in 0..100_000 {
            if (inp(0x64) & 0x01) != 0 {
                return inp(0x60);
            }
        }
        0
    }

    fn write_mouse(byte: u8) {
        Self::wait_write();
        out(0x64, 0xD4);
        Self::wait_write();
        out(0x60, byte);
    }

    /// Process a single raw byte received from the mouse.
    ///
    /// Returns `Some(MousePacket)` when a complete 3-byte packet is decoded.
    #[must_use]
    pub fn process_byte(&mut self, byte: u8) -> Option<MousePacket> {
        match self.cycle {
            0 => {
                // Bit 3 of packet byte 0 must be 1 in standard PS/2 protocol.
                if (byte & 0x08) == 0 {
                    return None;
                }
                self.packet[0] = byte;
                self.cycle = 1;
                None
            }
            1 => {
                self.packet[1] = byte;
                self.cycle = 2;
                None
            }
            _ => {
                self.packet[2] = byte;
                self.cycle = 0;

                let flags = self.packet[0];
                // Drop packet if overflow flags are set
                if (flags & 0xC0) != 0 {
                    return None;
                }

                let mut dx = i32::from(self.packet[1]);
                if (flags & 0x10) != 0 {
                    dx -= 256;
                }

                let mut dy = i32::from(self.packet[2]);
                if (flags & 0x20) != 0 {
                    dy -= 256;
                }

                // Invert dy for screen space (PS/2 positive is up, screen positive is down)
                let screen_dy = -dy;

                Some(MousePacket {
                    dx,
                    dy: screen_dy,
                    left_button: (flags & 0x01) != 0,
                    right_button: (flags & 0x02) != 0,
                    middle_button: (flags & 0x04) != 0,
                })
            }
        }
    }

    /// Poll for pending mouse input from the i8042 data port.
    ///
    /// Returns `Some(MousePacket)` if a complete 3-byte packet was assembled.
    #[allow(unsafe_code)]
    pub fn poll(&mut self) -> Option<MousePacket> {
        loop {
            let status = inp(0x64);
            if (status & 0x01) == 0 {
                break;
            }
            let is_mouse = (status & 0x20) != 0;
            let byte = inp(0x60);
            if !is_mouse {
                continue;
            }

            if let Some(packet) = self.process_byte(byte) {
                return Some(packet);
            }
        }
        None
    }
}

#[allow(unsafe_code)]
fn out(port: u16, value: u8) {
    // SAFETY: Writing to standard i8042 ports 0x60 and 0x64.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags));
    }
}

#[allow(unsafe_code)]
fn inp(port: u16) -> u8 {
    let value: u8;
    // SAFETY: Reading from standard i8042 ports 0x60 and 0x64.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nomem, nostack, preserves_flags));
    }
    value
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mouse_decodes_standard_movement() {
        let mut mouse = Ps2Mouse::new();
        assert_eq!(mouse.process_byte(0x08), None);
        assert_eq!(mouse.process_byte(15), None);
        let packet = mouse.process_byte(10).unwrap();
        assert_eq!(packet.dx, 15);
        assert_eq!(packet.dy, -10);
        assert!(!packet.left_button);
    }

    #[test]
    fn mouse_decodes_negative_deltas_and_buttons() {
        let mut mouse = Ps2Mouse::new();
        // Flags: left button (0x01) + bit 3 (0x08) + sign x (0x10) + sign y (0x20) = 0x39
        assert_eq!(mouse.process_byte(0x39), None);
        assert_eq!(mouse.process_byte(250), None); // dx = 250 - 256 = -6
        let packet = mouse.process_byte(246).unwrap(); // dy = 246 - 256 = -10, screen_dy = +10
        assert_eq!(packet.dx, -6);
        assert_eq!(packet.dy, 10);
        assert!(packet.left_button);
    }

    #[test]
    fn mouse_rejects_out_of_sync_bytes() {
        let mut mouse = Ps2Mouse::new();
        assert_eq!(mouse.process_byte(0x00), None);
        assert_eq!(mouse.cycle, 0);
    }
}
