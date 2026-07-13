//! Architecture-neutral interrupt-context tracking.

use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

static INTERRUPT_DEPTH: AtomicUsize = AtomicUsize::new(0);
static INTERRUPT_FAULT: AtomicBool = AtomicBool::new(false);

/// Errors from the bounded interrupt-context counter.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InterruptContextError {
    /// The nesting counter reached its maximum.
    Overflow,
    /// The nesting counter was released while already zero.
    Underflow,
}

/// A non-allocating guard for one level of interrupt dispatch.
pub struct InterruptContextGuard {
    active: bool,
}

impl InterruptContextGuard {
    /// Enter interrupt context.
    ///
    /// # Errors
    ///
    /// Returns `Overflow` if the nesting counter is exhausted.
    pub fn enter() -> Result<Self, InterruptContextError> {
        let result = INTERRUPT_DEPTH.fetch_update(Ordering::AcqRel, Ordering::Acquire, |depth| {
            depth.checked_add(1)
        });
        if result.is_ok() {
            Ok(Self { active: true })
        } else {
            INTERRUPT_FAULT.store(true, Ordering::Release);
            Err(InterruptContextError::Overflow)
        }
    }
}

impl Drop for InterruptContextGuard {
    fn drop(&mut self) {
        if !self.active {
            return;
        }
        let result = INTERRUPT_DEPTH.fetch_update(Ordering::AcqRel, Ordering::Acquire, |depth| {
            depth.checked_sub(1)
        });
        if result.is_err() {
            INTERRUPT_FAULT.store(true, Ordering::Release);
        }
        self.active = false;
    }
}

/// Return whether the current execution is inside interrupt dispatch.
#[must_use]
pub fn in_interrupt_context() -> bool {
    INTERRUPT_DEPTH.load(Ordering::Acquire) != 0
}

/// Return the current nesting depth.
#[must_use]
pub fn interrupt_depth() -> usize {
    INTERRUPT_DEPTH.load(Ordering::Acquire)
}

/// Return whether a counter fault has been observed.
#[must_use]
pub fn interrupt_context_faulted() -> bool {
    INTERRUPT_FAULT.load(Ordering::Acquire)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guard_tracks_nesting() {
        assert!(!in_interrupt_context());
        let first = InterruptContextGuard::enter().unwrap();
        let second = InterruptContextGuard::enter().unwrap();
        assert!(in_interrupt_context());
        assert_eq!(interrupt_depth(), 2);
        drop(second);
        assert_eq!(interrupt_depth(), 1);
        drop(first);
        assert!(!in_interrupt_context());
    }
}
