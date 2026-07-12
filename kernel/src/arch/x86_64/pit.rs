//! Polled PIT channel-2 reference used only during local APIC calibration.
#![allow(missing_docs)]

const CHANNEL2: u16 = 0x42;
const COMMAND: u16 = 0x43;
const SPEAKER: u16 = 0x61;
/// PIT input frequency.
pub const INPUT_HZ: u64 = 1_193_182;
const POLL_LIMIT: u32 = 20_000_000;

/// PIT calibration failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PitError {
    InvalidDuration,
    CountOverflow,
    Timeout,
}

/// Convert a duration to a valid 16-bit PIT count.
pub fn duration_count(milliseconds: u64) -> Result<u16, PitError> {
    if milliseconds == 0 {
        return Err(PitError::InvalidDuration);
    }
    let count = INPUT_HZ
        .checked_mul(milliseconds)
        .ok_or(PitError::CountOverflow)?
        / 1000;
    if count == 0 || count > u64::from(u16::MAX) {
        return Err(PitError::CountOverflow);
    }
    Ok(count as u16)
}

#[allow(unsafe_code)]
unsafe fn out_u8(port: u16, value: u8) {
    // SAFETY: PIT and speaker ports are byte-wide legacy x86 I/O ports.
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nostack, preserves_flags))
    };
}
#[allow(unsafe_code)]
unsafe fn in_u8(port: u16) -> u8 {
    let value: u8;
    // SAFETY: PIT and speaker ports are byte-wide legacy x86 I/O ports.
    unsafe {
        core::arch::asm!("in al, dx", out("al") value, in("dx") port, options(nostack, preserves_flags))
    };
    value
}

/// Run a 10 ms channel-2 one-shot and return its count.
#[allow(unsafe_code)]
pub fn wait_reference(milliseconds: u64) -> Result<u16, PitError> {
    let count = duration_count(milliseconds)?;
    // SAFETY: This sequence is executed with IF clear and restores port 0x61 before return.
    unsafe {
        let original = in_u8(SPEAKER);
        out_u8(SPEAKER, original & !0x02 & !0x01);
        out_u8(COMMAND, 0xb0); // channel 2, lobyte/hibyte, mode 0, binary
        out_u8(CHANNEL2, count as u8);
        out_u8(CHANNEL2, (count >> 8) as u8);
        out_u8(SPEAKER, (original & !0x02) & !0x01);
        out_u8(SPEAKER, ((original & !0x02) & !0x01) | 0x01);
        let mut polls = 0;
        while polls < POLL_LIMIT {
            if in_u8(SPEAKER) & 0x20 != 0 {
                out_u8(SPEAKER, original);
                return Ok(count);
            }
            polls += 1;
        }
        out_u8(SPEAKER, original);
    }
    Err(PitError::Timeout)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ten_ms_is_11931_or_11932() {
        assert!((11_931..=11_932).contains(&duration_count(10).unwrap()));
    }
    #[test]
    fn invalid_duration_is_rejected() {
        assert_eq!(duration_count(0), Err(PitError::InvalidDuration));
    }
    #[test]
    fn count_overflow_is_rejected() {
        assert_eq!(duration_count(100), Err(PitError::CountOverflow));
    }
}
