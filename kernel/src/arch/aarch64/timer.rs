//! ARM64 Architectural Generic Timer policy, periodic ticks, and monotonic time.
//!
//! Uses the EL1 physical timer (`CNTP_*_EL0`) and `GICv2` PPI 30.

#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(clippy::missing_const_for_fn)]
#![allow(unsafe_code)]

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

/// Timer frequency in Hertz.
pub const FREQUENCY_HZ: u64 = 100;
/// Duration represented by one tick in milliseconds.
pub const TICK_MILLISECONDS: u64 = 1000 / FREQUENCY_HZ;

static INITIALIZED: AtomicBool = AtomicBool::new(false);
static TICKS: AtomicU64 = AtomicU64::new(0);
static REAL_DELIVERIES: AtomicU64 = AtomicU64::new(0);
static CONTEXT_OBSERVED: AtomicBool = AtomicBool::new(false);
static TICK_OVERFLOW: AtomicBool = AtomicBool::new(false);
static SPURIOUS: AtomicU64 = AtomicU64::new(0);
static TIMER_FREQUENCY: AtomicU64 = AtomicU64::new(0);
static INTERVAL_TICKS: AtomicU64 = AtomicU64::new(0);

/// Failures from ARM64 timer initialization and programming.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TimerError {
    /// System counter frequency reported zero or is invalid.
    ZeroFrequency,
    /// Arithmetic overflow calculating timer interval.
    IntervalOverflow,
    /// Timer was already initialized.
    AlreadyInitialized,
    /// GIC controller was not ready to route the timer interrupt.
    GicNotReady,
    /// The timer has not been initialized.
    NotInitialized,
}

/// Return the current hardware system counter frequency in Hz.
#[must_use]
pub fn counter_frequency() -> u64 {
    #[cfg(target_os = "none")]
    {
        let freq: u64;
        // SAFETY: CNTFRQ_EL0 is readable at EL1.
        unsafe {
            core::arch::asm!(
                "mrs {freq}, cntfrq_el0",
                freq = out(reg) freq,
                options(nomem, nostack, preserves_flags)
            );
        }
        freq
    }
    #[cfg(not(target_os = "none"))]
    {
        62_500_000
    }
}

/// Read the current 64-bit physical system counter count.
#[must_use]
pub fn counter_ticks() -> u64 {
    #[cfg(target_os = "none")]
    {
        let count: u64;
        // SAFETY: CNTPCT_EL0 is readable at EL1.
        unsafe {
            core::arch::asm!(
                "mrs {count}, cntpct_el0",
                count = out(reg) count,
                options(nomem, nostack, preserves_flags)
            );
        }
        count
    }
    #[cfg(not(target_os = "none"))]
    {
        0
    }
}

/// Calculate the downcounter interval for 100 Hz given the counter frequency.
///
/// # Errors
///
/// Returns `ZeroFrequency` if `cntfrq == 0` or `IntervalOverflow` if the interval
/// exceeds 32-bit capacity.
pub const fn calculate_interval(cntfrq: u64, target_hz: u64) -> Result<u64, TimerError> {
    if cntfrq == 0 || target_hz == 0 {
        return Err(TimerError::ZeroFrequency);
    }
    let interval = cntfrq / target_hz;
    if interval == 0 || interval > (u32::MAX as u64) {
        return Err(TimerError::IntervalOverflow);
    }
    Ok(interval)
}

/// Validate the independent 50 ms frequency window.
#[must_use]
pub const fn frequency_window_valid(ticks: u64) -> bool {
    ticks >= 3 && ticks <= 7
}

/// Configure and start the 100 Hz periodic EL1 physical timer and enable GIC PPI 30.
///
/// # Errors
///
/// Returns an error if frequency is invalid or GIC is uninitialized.
pub fn initialize() -> Result<(u64, u64), TimerError> {
    if INITIALIZED.swap(true, Ordering::AcqRel) {
        return Err(TimerError::AlreadyInitialized);
    }

    let freq = counter_frequency();
    let interval = calculate_interval(freq, FREQUENCY_HZ)?;

    TIMER_FREQUENCY.store(freq, Ordering::Release);
    INTERVAL_TICKS.store(interval, Ordering::Release);

    #[cfg(target_os = "none")]
    {
        // Program the initial count and enable timer:
        // CNTP_CTL_EL0: bit 0 = ENABLE (1), bit 1 = IMASK (0 = unmasked)
        unsafe {
            core::arch::asm!(
                "msr cntp_ctl_el0, {ctl}",
                "msr cntp_tval_el0, {tval}",
                "isb",
                ctl = in(reg) 1_u64,
                tval = in(reg) interval,
                options(nomem, nostack, preserves_flags)
            );
        }

        // Enable PPI 30 in the GIC
        super::gic::enable_timer_ppi().map_err(|_| TimerError::GicNotReady)?;
    }

    Ok((freq, interval))
}

/// Return the delivered timer ticks count.
#[must_use]
pub fn ticks() -> u64 {
    TICKS.load(Ordering::Acquire)
}

/// Return the configured frequency (100 Hz).
#[must_use]
pub const fn frequency_hz() -> u64 {
    FREQUENCY_HZ
}

/// Return the duration of one tick in milliseconds (10 ms).
#[must_use]
pub const fn tick_milliseconds() -> u64 {
    TICK_MILLISECONDS
}

/// Monotonic uptime in milliseconds.
#[must_use]
pub fn uptime_milliseconds() -> u64 {
    ticks().saturating_mul(TICK_MILLISECONDS)
}

/// Return number of real timer interrupt deliveries.
#[must_use]
pub fn real_deliveries() -> u64 {
    REAL_DELIVERIES.load(Ordering::Acquire)
}

/// Return whether the timer ISR entered interrupt context.
#[must_use]
pub fn context_observed() -> bool {
    CONTEXT_OBSERVED.load(Ordering::Acquire)
}

/// Return whether tick saturation occurred.
#[must_use]
pub fn tick_overflowed() -> bool {
    TICK_OVERFLOW.load(Ordering::Acquire)
}

/// Return whether the timer is initialized.
#[must_use]
pub fn is_initialized() -> bool {
    INITIALIZED.load(Ordering::Acquire)
}

/// Return spurious dispatches count.
#[must_use]
pub fn spurious_count() -> u64 {
    SPURIOUS.load(Ordering::Acquire)
}

/// Allocation-free timer ISR body invoked from GIC dispatch.
pub fn handle_tick() {
    CONTEXT_OBSERVED.store(true, Ordering::Release);
    if !INITIALIZED.load(Ordering::Acquire) {
        return;
    }

    let interval = INTERVAL_TICKS.load(Ordering::Relaxed);
    if interval != 0 {
        #[cfg(target_os = "none")]
        unsafe {
            core::arch::asm!(
                "msr cntp_tval_el0, {tval}",
                "isb",
                tval = in(reg) interval,
                options(nomem, nostack, preserves_flags)
            );
        }
    }

    REAL_DELIVERIES.fetch_add(1, Ordering::Relaxed);
    let old = TICKS.fetch_update(Ordering::AcqRel, Ordering::Acquire, |v| v.checked_add(1));
    if old.is_err() {
        TICK_OVERFLOW.store(true, Ordering::Release);
    }
    crate::preemption::on_timer_tick();
}

/// Wait synchronously for a given duration in milliseconds using the physical counter.
pub fn spin_wait_milliseconds(ms: u64) {
    let freq = counter_frequency();
    if freq == 0 {
        return;
    }
    let counts = (freq.saturating_mul(ms)) / 1000;
    let start = counter_ticks();
    let target = start.saturating_add(counts);
    while counter_ticks() < target {
        core::hint::spin_loop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calculate_interval_deterministic() {
        // Standard QEMU Cortex-A72 frequency: 62.5 MHz
        let interval = calculate_interval(62_500_000, 100).unwrap();
        assert_eq!(interval, 625_000);
    }

    #[test]
    fn calculate_interval_zero_rejected() {
        assert_eq!(calculate_interval(0, 100), Err(TimerError::ZeroFrequency));
        assert_eq!(
            calculate_interval(62_500_000, 0),
            Err(TimerError::ZeroFrequency)
        );
    }

    #[test]
    fn frequency_window_tolerance() {
        assert!(!frequency_window_valid(0));
        assert!(!frequency_window_valid(2));
        assert!(frequency_window_valid(3));
        assert!(frequency_window_valid(5));
        assert!(frequency_window_valid(7));
        assert!(!frequency_window_valid(8));
    }
}
