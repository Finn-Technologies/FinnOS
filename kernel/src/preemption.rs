//! Allocation-free deferred reschedule control.
//!
//! This is deliberately policy-free: timer interrupts may request work, but
//! only ordinary kernel code may consume that request.  It does not switch a
//! task and it leaves maskable interrupts enabled.

use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

static DEPTH: AtomicU32 = AtomicU32::new(0);
static REQUESTED: AtomicBool = AtomicBool::new(false);
static FAULTED: AtomicBool = AtomicBool::new(false);
static QUANTUM: AtomicU64 = AtomicU64::new(0);
static QUANTUM_TICKS: AtomicU64 = AtomicU64::new(0);

/// Failure from bounded preemption nesting.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PreemptionError {
    /// Nesting counter overflowed.
    Overflow,
}

/// A single level of deferred-preemption protection.
pub struct PreemptionGuard {
    active: bool,
}

impl PreemptionGuard {
    /// Enters a protected ordinary-kernel transition.
    ///
    /// # Errors
    ///
    /// Returns `Overflow` and permanently records a diagnostic fault when the
    /// bounded nesting counter cannot be incremented.
    pub fn enter() -> Result<Self, PreemptionError> {
        if DEPTH
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |v| v.checked_add(1))
            .is_err()
        {
            FAULTED.store(true, Ordering::Release);
            return Err(PreemptionError::Overflow);
        }
        Ok(Self { active: true })
    }
}
impl Drop for PreemptionGuard {
    fn drop(&mut self) {
        if self.active
            && DEPTH
                .fetch_update(Ordering::AcqRel, Ordering::Acquire, |v| v.checked_sub(1))
                .is_err()
        {
            FAULTED.store(true, Ordering::Release);
        }
        self.active = false;
    }
}
/// Returns the bounded nesting depth.
#[must_use]
pub fn preemption_depth() -> u32 {
    DEPTH.load(Ordering::Acquire)
}
/// Returns whether preemption consumption is disabled.
#[must_use]
pub fn preemption_disabled() -> bool {
    preemption_depth() != 0
}
/// Records a deferred reschedule request without scheduling.
pub fn request_reschedule() {
    REQUESTED.store(true, Ordering::Release);
}
/// Returns whether a request is pending.
#[must_use]
pub fn reschedule_requested() -> bool {
    REQUESTED.load(Ordering::Acquire)
}
/// Atomically consumes a pending request only at depth zero.
#[must_use]
pub fn take_reschedule_request() -> bool {
    if preemption_disabled() || crate::interrupt::in_interrupt_context() {
        return false;
    }
    REQUESTED.swap(false, Ordering::AcqRel)
}
/// Returns whether a nesting fault was observed. Faults are permanent.
#[must_use]
pub fn preemption_faulted() -> bool {
    FAULTED.load(Ordering::Acquire)
}
/// Configures request generation; zero permanently disables it until changed.
pub fn configure_quantum_ticks(ticks: u64) {
    QUANTUM.store(ticks, Ordering::Release);
    QUANTUM_TICKS.store(0, Ordering::Release);
}
/// Timer-facing request generation. It is interrupt-safe and never schedules.
pub fn on_timer_tick() {
    let quantum = QUANTUM.load(Ordering::Acquire);
    if quantum == 0 {
        return;
    }
    let elapsed = QUANTUM_TICKS
        .fetch_update(Ordering::AcqRel, Ordering::Acquire, |value| {
            Some(value.saturating_add(1))
        })
        .unwrap_or(u64::MAX)
        .saturating_add(1);
    if elapsed >= quantum {
        QUANTUM_TICKS.store(0, Ordering::Release);
        request_reschedule();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn nested_request_is_deferred() {
        configure_quantum_ticks(0);
        REQUESTED.store(false, Ordering::Release);
        let outer = PreemptionGuard::enter().unwrap();
        let inner = PreemptionGuard::enter().unwrap();
        request_reschedule();
        assert!(reschedule_requested());
        assert!(!take_reschedule_request());
        drop(inner);
        assert!(reschedule_requested());
        drop(outer);
        assert!(take_reschedule_request());
        assert!(!reschedule_requested());
    }
    #[test]
    fn quantum_is_disabled_by_default_and_expires() {
        configure_quantum_ticks(0);
        REQUESTED.store(false, Ordering::Release);
        on_timer_tick();
        assert!(!reschedule_requested());
        configure_quantum_ticks(2);
        on_timer_tick();
        assert!(!reschedule_requested());
        on_timer_tick();
        assert!(take_reschedule_request());
        configure_quantum_ticks(0);
    }
}
