//! BSP local APIC periodic timer policy and monotonic ticks.

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use super::{
    apic::{self, ApicError, LocalApic},
    pit::{self, PitError},
};
use crate::interrupt::InterruptContextGuard;

/// Timer frequency.
pub const FREQUENCY_HZ: u64 = 100;
/// Duration represented by one tick.
pub const TICK_MILLISECONDS: u64 = 1000 / FREQUENCY_HZ;

static INITIALIZED: AtomicBool = AtomicBool::new(false);
static TICKS: AtomicU64 = AtomicU64::new(0);
static REAL_DELIVERIES: AtomicU64 = AtomicU64::new(0);
static CONTEXT_OBSERVED: AtomicBool = AtomicBool::new(false);
static SPURIOUS: AtomicU64 = AtomicU64::new(0);
static TICK_OVERFLOW: AtomicBool = AtomicBool::new(false);

/// Timer setup failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TimerError {
    /// The PIT reference failed.
    Pit(PitError),
    /// An APIC operation failed.
    Apic(ApicError),
    /// A calibration input was zero.
    CalibrationZero,
    /// Checked calibration arithmetic overflowed.
    CalibrationOverflow,
    /// The programmed initial count was zero.
    TimerInitialCountZero,
    /// A timer-dependent API was called before initialization.
    TimerNotInitialized,
    /// Initialization was attempted twice.
    AlreadyInitialized,
    /// The independent frequency window was outside tolerance.
    FrequencyWindowOutOfRange,
}

/// Calculate the initial count for a periodic timer.
///
/// Integer division intentionally truncates toward zero after checked arithmetic.
///
/// # Errors
///
/// Returns an error for zero inputs, overflow, or a result outside `u32`.
pub fn periodic_initial_count(
    elapsed_counts: u32,
    reference_milliseconds: u64,
    target_frequency_hz: u64,
) -> Result<u32, TimerError> {
    if elapsed_counts == 0 || reference_milliseconds == 0 || target_frequency_hz == 0 {
        return Err(TimerError::CalibrationZero);
    }
    let numerator = u64::from(elapsed_counts)
        .checked_mul(1000)
        .ok_or(TimerError::CalibrationOverflow)?;
    let denominator = reference_milliseconds
        .checked_mul(target_frequency_hz)
        .ok_or(TimerError::CalibrationOverflow)?;
    let result = numerator
        .checked_div(denominator)
        .ok_or(TimerError::CalibrationZero)?;
    if result == 0 || result > u64::from(u32::MAX) {
        return Err(TimerError::CalibrationOverflow);
    }
    u32::try_from(result).map_err(|_| TimerError::CalibrationOverflow)
}

/// Validate the independent 50 ms frequency window.
#[must_use]
pub fn frequency_window_valid(ticks: u64) -> bool {
    (3..=7).contains(&ticks)
}

/// Configure and start the calibrated 100 Hz periodic APIC timer.
///
/// # Errors
///
/// Returns an error if calibration or APIC programming fails.
pub fn initialize(apic: LocalApic) -> Result<(u32, u32, u32), TimerError> {
    if INITIALIZED.swap(true, Ordering::AcqRel) {
        return Err(TimerError::AlreadyInitialized);
    }
    let result = (|| {
        apic.write(0x320, apic::LVT_MASKED)
            .map_err(TimerError::Apic)?;
        apic.write(0x3e0, 0x3).map_err(TimerError::Apic)?;
        apic.write(0x380, u32::MAX).map_err(TimerError::Apic)?;
        let reference = pit::wait_reference(10).map_err(TimerError::Pit)?;
        let current = apic.timer_current().map_err(TimerError::Apic)?;
        let elapsed = u32::MAX
            .checked_sub(current)
            .ok_or(TimerError::CalibrationZero)?;
        let initial = periodic_initial_count(elapsed, TICK_MILLISECONDS, FREQUENCY_HZ)?;
        if initial == 0 {
            return Err(TimerError::TimerInitialCountZero);
        }
        apic.program_timer(initial).map_err(TimerError::Apic)?;
        Ok((u32::from(reference), elapsed, initial))
    })();
    if result.is_err() {
        INITIALIZED.store(false, Ordering::Release);
    }
    result
}

/// Return the number of delivered timer ticks.
#[must_use]
pub fn ticks() -> u64 {
    TICKS.load(Ordering::Acquire)
}
/// Return configured frequency.
#[must_use]
pub const fn frequency_hz() -> u64 {
    FREQUENCY_HZ
}
/// Return configured tick duration.
#[must_use]
pub const fn tick_milliseconds() -> u64 {
    TICK_MILLISECONDS
}
/// Convert ticks to saturated monotonic milliseconds.
#[must_use]
pub fn uptime_milliseconds() -> u64 {
    TICKS
        .load(Ordering::Acquire)
        .saturating_mul(TICK_MILLISECONDS)
}
/// Return how many timer ISR deliveries were observed.
#[must_use]
pub fn real_deliveries() -> u64 {
    REAL_DELIVERIES.load(Ordering::Acquire)
}
/// Return whether the ISR entered interrupt context.
#[must_use]
pub fn context_observed() -> bool {
    CONTEXT_OBSERVED.load(Ordering::Acquire)
}
/// Return whether tick saturation occurred.
#[must_use]
pub fn tick_overflowed() -> bool {
    TICK_OVERFLOW.load(Ordering::Acquire)
}
/// Return whether timer initialization completed.
#[must_use]
pub fn is_initialized() -> bool {
    INITIALIZED.load(Ordering::Acquire)
}

/// The allocation-free timer ISR body.
pub fn handle_tick() {
    let Ok(_guard) = InterruptContextGuard::enter() else {
        apic::timer_eoi();
        return;
    };
    CONTEXT_OBSERVED.store(true, Ordering::Release);
    if !INITIALIZED.load(Ordering::Acquire) {
        apic::timer_eoi();
        return;
    }
    REAL_DELIVERIES.fetch_add(1, Ordering::Relaxed);
    let old = TICKS.fetch_update(Ordering::AcqRel, Ordering::Acquire, |value| {
        value.checked_add(1)
    });
    if old.is_err() {
        TICK_OVERFLOW.store(true, Ordering::Release);
    }
    apic::timer_eoi();
}

/// Handle a spurious dispatch without EOI.
pub fn handle_spurious() {
    let Ok(_guard) = InterruptContextGuard::enter() else {
        return;
    };
    SPURIOUS.fetch_add(1, Ordering::Relaxed);
}

/// Return the number of spurious dispatches.
#[must_use]
pub fn spurious_count() -> u64 {
    SPURIOUS.load(Ordering::Acquire)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn calibration_is_integer_and_deterministic() {
        assert_eq!(periodic_initial_count(637_937, 10, 100), Ok(637_937));
        assert_eq!(periodic_initial_count(1_000_000, 20, 100), Ok(500_000));
    }
    #[test]
    fn zero_calibration_is_rejected() {
        assert_eq!(
            periodic_initial_count(0, 10, 100),
            Err(TimerError::CalibrationZero)
        );
    }

    #[test]
    fn frequency_window_tolerance_is_three_to_seven() {
        assert!(!frequency_window_valid(0));
        assert!(!frequency_window_valid(1));
        assert!(frequency_window_valid(3));
        assert!(frequency_window_valid(5));
        assert!(frequency_window_valid(7));
        assert!(!frequency_window_valid(8));
    }

    #[test]
    fn calibration_rejects_zero_frequency_and_overflow() {
        assert_eq!(
            periodic_initial_count(1, 10, 0),
            Err(TimerError::CalibrationZero)
        );
        assert_eq!(
            periodic_initial_count(1, 0, 100),
            Err(TimerError::CalibrationZero)
        );
        assert_eq!(
            periodic_initial_count(1, u64::MAX, 2),
            Err(TimerError::CalibrationOverflow)
        );
        assert_eq!(
            periodic_initial_count(u32::MAX, 1, 1),
            Err(TimerError::CalibrationOverflow)
        );
    }
}
