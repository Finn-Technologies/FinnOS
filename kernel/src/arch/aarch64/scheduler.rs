//! Single-BSP cooperative scheduler and AAPCS64 task context binding.

#![allow(dead_code)]
#![allow(clippy::empty_loop)]
#![allow(clippy::missing_const_for_fn)]
#![allow(clippy::missing_errors_doc)]
#![allow(clippy::must_use_candidate)]
#![allow(clippy::option_if_let_else)]
#![allow(unsafe_code)]

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

#[cfg(target_os = "none")]
use super::context::initialize_context;
use super::context::{ContextError, TaskContext, switch};
use super::task_stack::{TaskStackError, TaskStackMapping};
#[cfg(target_os = "none")]
use super::task_stack::{
    map_task_stack, reclaim_task_stack, restore_task_stack, validate_task_stack,
};
#[cfg(target_os = "none")]
use crate::arch::aarch64::paging::ActiveAddressSpace;
#[cfg(target_os = "none")]
use crate::memory::EarlyPhysicalPageAllocator;
use crate::task::{MAX_TASKS, Scheduler, SchedulerStats, TaskError, TaskId, TaskState};

/// Failures from the ARM64 scheduler.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SchedulerError {
    /// Scheduler used before bootstrap initialization.
    NotInitialized,
    /// Already initialized.
    AlreadyInitialized,
    /// Operation attempted from interrupt context.
    InterruptContextForbidden,
    /// Reentrant scheduling operation.
    Reentrant,
    /// Task policy error.
    Task(TaskError),
    /// Task stack error.
    Stack(TaskStackError),
    /// Stack publication error.
    Publication(super::exceptions::AttributionError),
    /// Context creation error.
    Context(ContextError),
    /// Invalid entry or state.
    InvalidEntry,
    /// Scheduler poisoned by failed rollback.
    Poisoned,
    /// Preemption guard faulted.
    PreemptionFault,
    /// Preemption is disabled.
    PreemptionDisabled,
}

impl From<super::exceptions::AttributionError> for SchedulerError {
    fn from(error: super::exceptions::AttributionError) -> Self {
        Self::Publication(error)
    }
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

struct RuntimeSlot {
    entry: Option<fn()>,
    context: TaskContext,
    stack: Option<TaskStackMapping>,
}
impl RuntimeSlot {
    const EMPTY: Self = Self {
        entry: None,
        context: TaskContext { sp: 0 },
        stack: None,
    };
}

struct Runtime {
    policy: Scheduler,
    slots: [RuntimeSlot; MAX_TASKS],
    #[allow(dead_code)]
    poisoned: bool,
}

struct SchedulerCell(UnsafeCell<Option<Runtime>>);
unsafe impl Sync for SchedulerCell {}
static RUNTIME: SchedulerCell = SchedulerCell(UnsafeCell::new(None));

static SWITCHING: AtomicBool = AtomicBool::new(false);
static IDLE_SP: AtomicU64 = AtomicU64::new(0);
static INTERRUPT_CONTEXT_ENTRIES: AtomicU64 = AtomicU64::new(0);

/// Diagnostic snapshot of a task slot.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskDiagnostics {
    /// Current lifecycle state.
    pub state: TaskState,
    /// Whether the task is in the runnable queue.
    pub queued: bool,
    /// Saved stack pointer.
    pub sp: u64,
    /// Dynamic stack start.
    pub stack_start: u64,
    /// Dynamic stack end.
    pub stack_end: u64,
}

fn runtime_ref() -> Result<&'static Runtime, SchedulerError> {
    // SAFETY: BSP-only before SMP, non-reentrant.
    let cell = unsafe { &*RUNTIME.0.get() };
    cell.as_ref().ok_or(SchedulerError::NotInitialized)
}

fn runtime_mut() -> Result<&'static mut Runtime, SchedulerError> {
    // SAFETY: BSP-only before SMP, non-reentrant.
    let cell = unsafe { &mut *RUNTIME.0.get() };
    cell.as_mut().ok_or(SchedulerError::NotInitialized)
}

/// Initializes bootstrap task and idle task.
#[cfg(target_os = "none")]
pub fn initialize(
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(TaskId, TaskId), SchedulerError> {
    reject_interrupt_context()?;
    let _guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;

    let cell = unsafe { &mut *RUNTIME.0.get() };
    if cell.is_some() {
        return Err(SchedulerError::AlreadyInitialized);
    }

    let mut runtime = Runtime {
        policy: Scheduler::new(),
        slots: [const { RuntimeSlot::EMPTY }; MAX_TASKS],
        poisoned: false,
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

    let bootstrap = runtime.policy.bootstrap_id();
    super::exceptions::publish_task_stack(
        idle,
        runtime.slots[idle.slot()]
            .stack
            .as_ref()
            .unwrap()
            .virtual_start(),
        runtime.slots[idle.slot()]
            .stack
            .as_ref()
            .unwrap()
            .virtual_end(),
    )?;

    runtime.policy.check_invariants()?;
    *cell = Some(runtime);

    Ok((bootstrap, idle))
}

/// Spawns a new worker task.
#[cfg(target_os = "none")]
pub fn spawn(
    entry: fn(),
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<TaskId, SchedulerError> {
    reject_interrupt_context()?;
    let _guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;

    let runtime = runtime_mut()?;
    let id = runtime.policy.spawn()?;

    let mut stack = TaskStackMapping::empty(id.slot()).map_err(TaskStackError::Layout)?;
    if let Err(e) = map_task_stack(&mut stack, address_space, allocator) {
        runtime.policy.abort_spawn(id).ok();
        return Err(e.into());
    }

    let context = match initialize_context(
        stack.virtual_start(),
        stack.virtual_end(),
        task_trampoline as *const () as usize as u64,
        fatal_trampoline_return as *const () as usize as u64,
    ) {
        Ok(c) => c,
        Err(e) => {
            restore_task_stack(&mut stack, address_space, allocator).ok();
            runtime.policy.abort_spawn(id).ok();
            return Err(e.into());
        }
    };

    runtime.slots[id.slot()] = RuntimeSlot {
        entry: Some(entry),
        context,
        stack: Some(stack),
    };

    if let Err(e) = super::exceptions::publish_task_stack(
        id,
        runtime.slots[id.slot()]
            .stack
            .as_ref()
            .unwrap()
            .virtual_start(),
        runtime.slots[id.slot()]
            .stack
            .as_ref()
            .unwrap()
            .virtual_end(),
    ) {
        let mut st = runtime.slots[id.slot()].stack.take().unwrap();
        restore_task_stack(&mut st, address_space, allocator).ok();
        runtime.slots[id.slot()] = RuntimeSlot::EMPTY;
        runtime.policy.abort_spawn(id).ok();
        return Err(e.into());
    }

    Ok(id)
}

/// Yields execution to the next runnable task.
pub fn yield_now() -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    let switch_plan = prepare_yield()?;
    let Some((old_sp, new_sp)) = switch_plan else {
        return Ok(());
    };
    unsafe {
        switch(old_sp, new_sp);
    }
    check_invariants()
}

fn prepare_yield() -> Result<Option<(*mut u64, u64)>, SchedulerError> {
    reject_preemption_disabled()?;
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result = (|| {
        let _guard = crate::preemption::PreemptionGuard::enter()
            .map_err(|_| SchedulerError::PreemptionFault)?;
        let runtime = runtime_mut()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let Some(next) = candidate.yield_current()? else {
            return Ok(None);
        };
        commit_switch_candidate(runtime, old, candidate, next).map(Some)
    })();
    SWITCHING.store(false, Ordering::Release);
    result
}

/// Reaps an exited task and frees its stack.
#[cfg(target_os = "none")]
pub fn reap(
    id: TaskId,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
) -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    let _guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;

    let runtime = runtime_mut()?;
    let prepared = runtime.policy.prepare_reap(id)?;

    super::exceptions::unpublish_task_stack(id.slot());
    if let Some(mut stack) = runtime.slots[id.slot()].stack.take() {
        reclaim_task_stack(&mut stack, address_space, allocator)?;
    }
    runtime.slots[id.slot()] = RuntimeSlot::EMPTY;
    runtime.policy.commit_reap(prepared);
    Ok(())
}

/// Returns state of task `id`.
pub fn task_state(id: TaskId) -> Result<TaskState, SchedulerError> {
    runtime_ref()?.policy.state(id).map_err(Into::into)
}

/// Returns snapshot of scheduler statistics.
pub fn stats() -> Result<SchedulerStats, SchedulerError> {
    Ok(runtime_ref()?.policy.stats())
}

/// Returns diagnostics for a task.
pub fn task_diagnostics(id: TaskId) -> Result<TaskDiagnostics, SchedulerError> {
    let runtime = runtime_ref()?;
    let state = runtime.policy.state(id)?;
    let queued = runtime.policy.is_queued(id);
    let slot = &runtime.slots[id.slot()];
    let (stack_start, stack_end) = match slot.stack.as_ref() {
        Some(s) => (s.virtual_start(), s.virtual_end()),
        None => (0, 0),
    };
    Ok(TaskDiagnostics {
        state,
        queued,
        sp: slot.context.sp,
        stack_start,
        stack_end,
    })
}

/// Return currently running task.
pub fn current_task() -> Result<TaskId, SchedulerError> {
    Ok(runtime_ref()?.policy.current())
}

/// Check runtime invariants.
#[cfg(target_os = "none")]
pub fn check_runtime_invariants(address_space: &ActiveAddressSpace) -> Result<(), SchedulerError> {
    let runtime = runtime_ref()?;
    runtime.policy.check_invariants()?;
    for (i, slot) in runtime.slots.iter().enumerate() {
        if i == 0 {
            continue;
        }
        if let Some(stack) = slot.stack.as_ref() {
            validate_task_stack(stack, address_space)?;
        }
    }
    Ok(())
}

/// Checks internal scheduler consistency.
pub fn check_invariants() -> Result<(), SchedulerError> {
    runtime_ref()?.policy.check_invariants().map_err(Into::into)
}

/// Switches to the idle task once for testing.
pub fn probe_idle_once() -> Result<(), SchedulerError> {
    reject_interrupt_context()?;
    reject_preemption_disabled()?;
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result: Result<(*mut u64, u64), SchedulerError> = (|| {
        let _guard = crate::preemption::PreemptionGuard::enter()
            .map_err(|_| SchedulerError::PreemptionFault)?;
        let runtime = runtime_mut()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let next = candidate.begin_idle_probe()?;
        commit_switch_candidate(runtime, old, candidate, next)
    })();
    SWITCHING.store(false, Ordering::Release);
    let (old_sp, new_sp) = result?;
    unsafe {
        switch(old_sp, new_sp);
    }
    check_invariants()
}

/// Return saved idle stack pointer.
#[must_use]
pub fn idle_sp() -> u64 {
    IDLE_SP.load(Ordering::Acquire)
}

/// Return count of invalid interrupt-context scheduler entries.
#[must_use]
pub fn interrupt_context_entry_count() -> u64 {
    INTERRUPT_CONTEXT_ENTRIES.load(Ordering::Acquire)
}

fn commit_switch_candidate(
    runtime: &mut Runtime,
    old: TaskId,
    candidate: Scheduler,
    next: TaskId,
) -> Result<(*mut u64, u64), SchedulerError> {
    validate_selected_context(runtime, next)?;
    let old_sp = core::ptr::addr_of_mut!(runtime.slots[old.slot()].context.sp);
    let new_sp = runtime.slots[next.slot()].context.sp;
    runtime.policy = candidate;
    Ok((old_sp, new_sp))
}

fn validate_selected_context(runtime: &Runtime, id: TaskId) -> Result<(), SchedulerError> {
    let slot = &runtime.slots[id.slot()];
    if slot.context.sp == 0 && id.slot() != 0 {
        return Err(SchedulerError::InvalidEntry);
    }
    if id.slot() != 0 {
        let stack = slot.stack.as_ref().ok_or(SchedulerError::InvalidEntry)?;
        if !stack.contains(slot.context.sp) {
            return Err(SchedulerError::InvalidEntry);
        }
    }
    Ok(())
}

extern "C" fn task_trampoline() -> ! {
    let entry = runtime_ref()
        .ok()
        .and_then(|runtime| runtime.slots[runtime.policy.current().slot()].entry)
        .unwrap_or_else(|| fatal_scheduler());
    entry();
    exit_current()
}

/// Exit current task and switch to next runnable task.
pub fn exit_current() -> ! {
    if reject_interrupt_context().is_err()
        || reject_preemption_disabled().is_err()
        || SWITCHING.swap(true, Ordering::Acquire)
    {
        fatal_scheduler();
    }
    let plan = (|| {
        let _guard = crate::preemption::PreemptionGuard::enter().ok()?;
        let runtime = runtime_mut().ok()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let next = candidate.exit_current().ok()?;
        commit_switch_candidate(runtime, old, candidate, next).ok()
    })();
    SWITCHING.store(false, Ordering::Release);
    let Some((old_sp, new_sp)) = plan else {
        fatal_scheduler();
    };
    unsafe {
        switch(old_sp, new_sp);
    }
    fatal_scheduler()
}

fn idle_task() {
    loop {
        let sp: u64;
        #[cfg(target_os = "none")]
        unsafe {
            core::arch::asm!("mov {}, sp", out(reg) sp, options(nomem, nostack, preserves_flags));
        }
        #[cfg(not(target_os = "none"))]
        {
            sp = 0;
        }
        IDLE_SP.store(sp, Ordering::Relaxed);
        #[cfg(target_os = "none")]
        unsafe {
            core::arch::asm!("wfi", options(nomem, nostack, preserves_flags));
        }
        if yield_now().is_err() {
            fatal_scheduler();
        }
    }
}

fn fatal_trampoline_return() -> ! {
    fatal_scheduler()
}

fn fatal_scheduler() -> ! {
    #[cfg(target_os = "none")]
    unsafe {
        core::arch::asm!(
            "msr daifset, #0xf",
            options(nomem, nostack, preserves_flags)
        );
    }
    loop {
        #[cfg(target_os = "none")]
        unsafe {
            core::arch::asm!("wfe", options(nomem, nostack, preserves_flags));
        }
        #[cfg(not(target_os = "none"))]
        core::hint::spin_loop();
    }
}

fn reject_interrupt_context() -> Result<(), SchedulerError> {
    if crate::interrupt::in_interrupt_context() {
        INTERRUPT_CONTEXT_ENTRIES.fetch_add(1, Ordering::Relaxed);
        Err(SchedulerError::InterruptContextForbidden)
    } else {
        Ok(())
    }
}

fn reject_preemption_disabled() -> Result<(), SchedulerError> {
    if crate::preemption::preemption_disabled() {
        Err(SchedulerError::PreemptionDisabled)
    } else {
        Ok(())
    }
}
