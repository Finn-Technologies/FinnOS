//! Single-BSP glue between the bounded task policy and x86-64 contexts.
//!
//! The global is safe on the current BSP-only design: interrupt handlers never
//! access it, public mutation checks interrupt context, and all references are
//! dropped before `finn_context_switch` changes stacks.
#![allow(unsafe_code)]

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};

use super::context::{ContextError, TaskContext, initialize_context, switch};
use super::task_stack::{TaskStackError, TaskStackMapping, map_task_stack, reclaim_task_stack};
use crate::arch::x86_64::paging::ActiveAddressSpace;
use crate::memory::EarlyPhysicalPageAllocator;
use crate::task::{MAX_TASKS, Scheduler, TaskError, TaskId, TaskState};

/// Failures from the live x86-64 scheduler boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SchedulerError {
    /// The scheduler was used before bootstrap registration.
    NotInitialized,
    /// Initialization was requested twice.
    AlreadyInitialized,
    /// Scheduler activity from an interrupt is forbidden.
    InterruptContextForbidden,
    /// A nested scheduling operation was attempted.
    Reentrant,
    /// Bounded task policy rejected the operation.
    Task(TaskError),
    /// Task-stack management failed.
    Stack(TaskStackError),
    /// Initial context construction failed.
    Context(ContextError),
    /// A task entry was missing or invalid.
    InvalidEntry,
}
impl From<TaskError> for SchedulerError {
    fn from(error: TaskError) -> Self {
        Self::Task(error)
    }
}
impl From<TaskStackError> for SchedulerError {
    fn from(error: TaskStackError) -> Self {
        Self::Stack(error)
    }
}
impl From<ContextError> for SchedulerError {
    fn from(error: ContextError) -> Self {
        Self::Context(error)
    }
}

#[derive(Clone, Copy)]
struct RuntimeSlot {
    entry: Option<fn()>,
    context: TaskContext,
    stack: Option<TaskStackMapping>,
}
impl RuntimeSlot {
    const EMPTY: Self = Self {
        entry: None,
        context: TaskContext { rsp: 0 },
        stack: None,
    };
}

struct Runtime {
    policy: Scheduler,
    slots: [RuntimeSlot; MAX_TASKS],
}

struct SchedulerCell(UnsafeCell<Option<Runtime>>);
// SAFETY: only the BSP uses this until an SMP design introduces per-CPU state;
// interrupt-context calls are rejected before mutable access and ISRs do not
// touch the scheduler.
unsafe impl Sync for SchedulerCell {}
static RUNTIME: SchedulerCell = SchedulerCell(UnsafeCell::new(None));
static SWITCHING: AtomicBool = AtomicBool::new(false);

/// Initializes bootstrap bookkeeping and maps the dedicated idle stack.
///
/// # Errors
///
/// Returns an error if called twice, from interrupt context, or if idle stack
/// allocation/mapping/context setup fails.
pub fn initialize(
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(TaskId, TaskId), SchedulerError> {
    reject_interrupt_context()?;
    // SAFETY: initialization is BSP-only and cannot race an interrupt handler.
    let cell = unsafe { &mut *RUNTIME.0.get() };
    if cell.is_some() {
        return Err(SchedulerError::AlreadyInitialized);
    }
    let mut runtime = Runtime {
        policy: Scheduler::new(),
        slots: [RuntimeSlot::EMPTY; MAX_TASKS],
    };
    let idle = runtime.policy.idle_id();
    let mut stack = TaskStackMapping::empty(idle.slot()).map_err(TaskStackError::Layout)?;
    map_task_stack(&mut stack, address_space, allocator)?;
    let context = initialize_context(
        stack.virtual_start(),
        stack.virtual_end(),
        task_trampoline as *const () as usize as u64,
        fatal_trampoline_return as *const () as usize as u64,
    )?;
    runtime.slots[idle.slot()] = RuntimeSlot {
        entry: Some(idle_task),
        context,
        stack: Some(stack),
    };
    runtime.policy.check_invariants()?;
    let bootstrap = runtime.policy.bootstrap_id();
    *cell = Some(runtime);
    Ok((bootstrap, idle))
}

/// Creates an ordinary task with a fully mapped stack and initial context.
///
/// # Errors
///
/// Returns an error if the scheduler is unavailable, the entry is invalid, or
/// resource setup fails.
pub fn spawn(
    entry: fn(),
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<TaskId, SchedulerError> {
    reject_interrupt_context()?;
    if entry as usize == 0 {
        return Err(SchedulerError::InvalidEntry);
    }
    let runtime = runtime_mut()?;
    let id = runtime.policy.spawn()?;
    let mut stack = TaskStackMapping::empty(id.slot()).map_err(TaskStackError::Layout)?;
    if let Err(error) = map_task_stack(&mut stack, address_space, allocator) {
        runtime.policy.abort_spawn(id)?;
        return Err(error.into());
    }
    let context = match initialize_context(
        stack.virtual_start(),
        stack.virtual_end(),
        task_trampoline as *const () as usize as u64,
        fatal_trampoline_return as *const () as usize as u64,
    ) {
        Ok(context) => context,
        Err(error) => {
            reclaim_task_stack(&mut stack, address_space, allocator)?;
            runtime.policy.abort_spawn(id)?;
            return Err(error.into());
        }
    };
    runtime.slots[id.slot()] = RuntimeSlot {
        entry: Some(entry),
        context,
        stack: Some(stack),
    };
    Ok(id)
}

/// Returns the live current task ID.
///
/// # Errors
///
/// Returns an error before scheduler initialization.
pub fn current_task() -> Result<TaskId, SchedulerError> {
    Ok(runtime_ref()?.policy.current())
}

/// Returns the lifecycle state for a current generation-tagged ID.
///
/// # Errors
///
/// Returns an error before initialization or for invalid and stale IDs.
pub fn task_state(id: TaskId) -> Result<TaskState, SchedulerError> {
    runtime_ref()?.policy.state(id).map_err(Into::into)
}

/// Validates the live task-policy invariants without allocating.
///
/// # Errors
///
/// Returns an error before initialization, in interrupt context, or if bounded
/// task bookkeeping is inconsistent.
pub fn check_invariants() -> Result<(), SchedulerError> {
    runtime_ref()?.policy.check_invariants().map_err(Into::into)
}

/// Cooperatively yields to the next ready task, if any.
///
/// # Errors
///
/// Returns structured scheduler failures; on a real switch it returns only
/// after this task is selected again.
pub fn yield_now() -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    let plan = prepare_yield()?;
    if let Some((old_rsp, new_rsp)) = plan {
        // SAFETY: prepare_yield mutates all policy state and returns stable raw
        // context storage with no retained borrow or re-entry flag.
        unsafe {
            switch(old_rsp, new_rsp);
        }
    }
    Ok(())
}

/// Reclaims an exited worker's stack from a different running task.
///
/// # Errors
///
/// Returns an error for stale, reserved, non-exited, or current tasks and for
/// failed stack reclamation.
pub fn reap(
    id: TaskId,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    let runtime = runtime_mut()?;
    if id == runtime.policy.current()
        || id.slot() < 2
        || runtime.policy.state(id)? != TaskState::Exited
    {
        return Err(SchedulerError::Task(TaskError::InvalidTransition));
    }
    let slot = &mut runtime.slots[id.slot()];
    let mut stack = slot.stack.take().ok_or(SchedulerError::InvalidEntry)?;
    reclaim_task_stack(&mut stack, address_space, allocator)?;
    *slot = RuntimeSlot::EMPTY;
    runtime.policy.reap(id)?;
    Ok(())
}

/// Permanently leaves bootstrap execution and starts the dedicated idle task.
///
/// This operation never returns. It is the normal successful-boot handoff.
///
/// # Panics
///
/// This function does not unwind; invalid scheduler state enters the fatal
/// kernel halt path instead.
pub fn park_bootstrap_and_run_idle() -> ! {
    if reject_interrupt_context().is_err() || SWITCHING.swap(true, Ordering::Acquire) {
        fatal_scheduler();
    }
    let plan = (|| {
        let runtime = runtime_mut().ok()?;
        let old = runtime.policy.current();
        let next = runtime.policy.park_bootstrap().ok()?;
        let old_rsp = &raw mut runtime.slots[old.slot()].context.rsp;
        let new_rsp = runtime.slots[next.slot()].context.rsp;
        (new_rsp != 0).then_some((old_rsp, new_rsp))
    })();
    SWITCHING.store(false, Ordering::Release);
    let Some((old_rsp, new_rsp)) = plan else {
        fatal_scheduler();
    };
    // SAFETY: bootstrap is marked blocked and the idle synthetic frame is
    // validated during initialization; no scheduler borrow survives this call.
    unsafe {
        switch(old_rsp, new_rsp);
    }
    fatal_scheduler()
}

/// Switches to the real idle task once and returns after idle yields bootstrap.
///
/// # Errors
///
/// Returns a structured error if the controlled probe cannot be prepared.
pub fn probe_idle_once() -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result = (|| {
        let runtime = runtime_mut()?;
        let old = runtime.policy.current();
        let next = runtime.policy.begin_idle_probe()?;
        let old_rsp = &raw mut runtime.slots[old.slot()].context.rsp;
        let new_rsp = runtime.slots[next.slot()].context.rsp;
        if new_rsp == 0 {
            return Err(SchedulerError::InvalidEntry);
        }
        Ok((old_rsp, new_rsp))
    })();
    SWITCHING.store(false, Ordering::Release);
    let (old_rsp, new_rsp) = result?;
    // SAFETY: the idle context was validated at initialization and bootstrap is
    // queued before all runtime borrows are released.
    unsafe {
        switch(old_rsp, new_rsp);
    }
    check_invariants()
}

fn prepare_yield() -> Result<Option<(*mut u64, u64)>, SchedulerError> {
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result = (|| {
        let runtime = runtime_mut()?;
        let old = runtime.policy.current();
        let Some(next) = runtime.policy.yield_current()? else {
            return Ok(None);
        };
        let old_rsp = &raw mut runtime.slots[old.slot()].context.rsp;
        let new_rsp = runtime.slots[next.slot()].context.rsp;
        if new_rsp == 0 {
            return Err(SchedulerError::InvalidEntry);
        }
        Ok(Some((old_rsp, new_rsp)))
    })();
    SWITCHING.store(false, Ordering::Release);
    result
}

extern "C" fn task_trampoline() -> ! {
    let entry = runtime_ref()
        .ok()
        .and_then(|runtime| runtime.slots[runtime.policy.current().slot()].entry)
        .unwrap_or_else(|| fatal_scheduler());
    entry();
    exit_current()
}

/// Marks the current worker exited and switches away from its active stack.
pub fn exit_current() -> ! {
    if reject_interrupt_context().is_err() || SWITCHING.swap(true, Ordering::Acquire) {
        fatal_scheduler();
    }
    let plan = (|| {
        let runtime = runtime_mut().ok()?;
        let old = runtime.policy.current();
        let next = runtime.policy.exit_current().ok()?;
        let old_rsp = &raw mut runtime.slots[old.slot()].context.rsp;
        let new_rsp = runtime.slots[next.slot()].context.rsp;
        (new_rsp != 0).then_some((old_rsp, new_rsp))
    })();
    SWITCHING.store(false, Ordering::Release);
    let Some((old_rsp, new_rsp)) = plan else {
        fatal_scheduler();
    };
    // SAFETY: policy bookkeeping is complete and no borrowed runtime state
    // survives the switch; the exited stack remains mapped until reaped.
    unsafe {
        switch(old_rsp, new_rsp);
    }
    fatal_scheduler()
}
fn idle_task() {
    loop {
        super::cpu::halt_once();
        let _ = yield_now();
    }
}
fn fatal_trampoline_return() -> ! {
    fatal_scheduler()
}
fn fatal_scheduler() -> ! {
    super::cpu::disable_interrupts();
    loop {
        super::cpu::halt_once();
    }
}

fn reject_interrupt_context() -> Result<(), SchedulerError> {
    if crate::interrupt::in_interrupt_context() {
        Err(SchedulerError::InterruptContextForbidden)
    } else {
        Ok(())
    }
}
fn runtime_ref() -> Result<&'static Runtime, SchedulerError> {
    reject_interrupt_context()?;
    unsafe {
        (&*RUNTIME.0.get())
            .as_ref()
            .ok_or(SchedulerError::NotInitialized)
    }
}
fn runtime_mut() -> Result<&'static mut Runtime, SchedulerError> {
    reject_interrupt_context()?;
    unsafe {
        (&mut *RUNTIME.0.get())
            .as_mut()
            .ok_or(SchedulerError::NotInitialized)
    }
}
