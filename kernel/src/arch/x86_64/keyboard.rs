//! Host-testable PS/2 keyboard scancode decoding.

/// A decoded PS/2 keyboard transition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct KeyboardScancode {
    /// True for a key press, false for a key release.
    pub pressed: bool,
    /// Canonical PS/2 set-1 make code.
    pub make_code: u8,
}

/// Decode one ordinary PS/2 set-1 scancode transition.
///
/// Extended `0xE0` prefixes are deliberately rejected. The desktop shortcut
/// layer only needs the ordinary control, modifier, tab, escape, and
/// printable-key subset until a complete keymap service exists.
#[must_use]
pub const fn decode_scancode(byte: u8) -> Option<KeyboardScancode> {
    match byte {
        0x1D | 0x2A | 0x36 | 0x38 | 0x3A | 0x1C | 0x32 | 0x39 | 0x1E | 0x1F | 0x21 | 0x22
        | 0x23 | 0x24 | 0x2B | 0x34 | 0x35 => Some(KeyboardScancode {
            pressed: true,
            make_code: byte,
        }),
        0x9D | 0xAA | 0xB6 | 0xB8 | 0xBA | 0x9C | 0xB2 | 0xB9 | 0x9E | 0x9F | 0xA1 | 0xA2
        | 0xA3 | 0xA4 | 0xAB | 0xB4 | 0xB5 => Some(KeyboardScancode {
            pressed: false,
            make_code: byte & 0x7F,
        }),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::{KeyboardScancode, decode_scancode};

    #[test]
    fn decoder_handles_presses_releases_and_prefixes() {
        assert_eq!(
            decode_scancode(0x1D),
            Some(KeyboardScancode {
                pressed: true,
                make_code: 0x1D,
            })
        );
        assert_eq!(
            decode_scancode(0x9D),
            Some(KeyboardScancode {
                pressed: false,
                make_code: 0x1D,
            })
        );
        assert_eq!(decode_scancode(0xE0), None);
        assert_eq!(decode_scancode(0xFF), None);
    }
}
