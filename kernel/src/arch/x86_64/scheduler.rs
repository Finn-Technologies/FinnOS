//! Single-BSP glue between the bounded task policy and x86-64 contexts.
//!
//! The global is safe on the current BSP-only design: interrupt handlers never
//! access it, public mutation checks interrupt context, and all references are
//! dropped before `finn_context_switch` changes stacks.
#![allow(unsafe_code)]

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use super::context::{ContextError, TaskContext, initialize_context, switch};
use super::task_stack::{
    TaskStackError, TaskStackMapping, map_task_stack, reclaim_task_stack, restore_task_stack,
    validate_task_stack,
};
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
    /// Stack publication failed.
    Publication(super::interrupts::AttributionError),
    /// Initial context construction failed.
    Context(ContextError),
    /// A task entry was missing or invalid.
    InvalidEntry,
    /// Cleanup after failed initialization could not restore stack resources.
    InitializationRollbackFailed {
        /// Failure that triggered cleanup.
        original: RollbackCause,
        /// Failure encountered while restoring ownership.
        cleanup: RollbackCause,
    },
    /// Cleanup after failed task creation could not restore policy and resources.
    SpawnRollbackFailed {
        /// Failure that triggered cleanup.
        original: RollbackCause,
        /// Failure encountered while restoring ownership.
        cleanup: RollbackCause,
    },
    /// Runtime ownership is preserved but scheduling is disabled after cleanup failure.
    Poisoned,
    /// The bounded preemption guard faulted while preparing a transition.
    PreemptionFault,
    /// A stack switch was requested inside a preemption-protected transition.
    PreemptionDisabled,
}

/// One side of a scheduler rollback failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RollbackCause {
    /// Bounded task-policy failure.
    Task(TaskError),
    /// Stack ownership or paging failure.
    Stack(TaskStackError),
    /// Initial-context construction failure.
    Context(ContextError),
    /// Stack publication failure.
    Publication(super::interrupts::AttributionError),
    /// Runtime bookkeeping was inconsistent.
    Runtime,
    /// Two independent cleanup operations failed.
    Combined {
        /// First cleanup failure.
        first: RollbackLeaf,
        /// Second cleanup failure.
        second: RollbackLeaf,
    },
}

/// One leaf of a combined rollback failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RollbackLeaf {
    /// Bounded task-policy failure.
    Task(TaskError),
    /// Stack ownership or paging failure.
    Stack(TaskStackError),
    /// Runtime bookkeeping was inconsistent.
    Runtime,
}

impl From<super::interrupts::AttributionError> for SchedulerError {
    fn from(error: super::interrupts::AttributionError) -> Self {
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
        context: TaskContext { rsp: 0 },
        stack: None,
    };
}

struct Runtime {
    policy: Scheduler,
    slots: [RuntimeSlot; MAX_TASKS],
    poisoned: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum StackRollbackState {
    Missing,
    Reclaimed,
    Retained,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct SpawnRollbackReport {
    publication_inactive: bool,
    stack: StackRollbackState,
    policy_aborted: bool,
    slot_empty: bool,
}

impl SpawnRollbackReport {
    const fn is_consistent(self) -> bool {
        if !self.publication_inactive {
            return false;
        }
        if self.slot_empty {
            matches!(self.stack, StackRollbackState::Reclaimed) && self.policy_aborted
        } else {
            !self.policy_aborted
                && matches!(
                    self.stack,
                    StackRollbackState::Missing
                        | StackRollbackState::Reclaimed
                        | StackRollbackState::Retained
                )
        }
    }
}

struct SchedulerCell(UnsafeCell<Option<Runtime>>);
// SAFETY: only the BSP uses this until an SMP design introduces per-CPU state;
// interrupt-context calls are rejected before mutable access and ISRs do not
// touch the scheduler.
unsafe impl Sync for SchedulerCell {}
static RUNTIME: SchedulerCell = SchedulerCell(UnsafeCell::new(None));
struct FailedInitializationCell(UnsafeCell<Option<TaskStackMapping>>);
// SAFETY: initialization and its failure path are BSP-only; the retained value
// is never copied or exposed and exists solely to preserve ownership metadata.
unsafe impl Sync for FailedInitializationCell {}
static FAILED_INITIALIZATION_STACK: FailedInitializationCell =
    FailedInitializationCell(UnsafeCell::new(None));
static SWITCHING: AtomicBool = AtomicBool::new(false);
static IDLE_RSP: AtomicU64 = AtomicU64::new(0);
static INTERRUPT_CONTEXT_ENTRIES: AtomicU64 = AtomicU64::new(0);

/// Copyable diagnostic snapshot for one task slot.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskDiagnostics {
    /// Current lifecycle state.
    pub state: TaskState,
    /// Whether the task appears in the runnable queue.
    pub queued: bool,
    /// Saved stack pointer.
    pub rsp: u64,
    /// Dynamic stack start, or zero for bootstrap/vacant slots.
    pub stack_start: u64,
    /// Dynamic stack end, or zero for bootstrap/vacant slots.
    pub stack_end: u64,
}

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
    let _preemption_guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;
    // SAFETY: initialization is BSP-only and cannot race an interrupt handler.
    let cell = unsafe { &mut *RUNTIME.0.get() };
    // SAFETY: initialization is BSP-only and serialized with this access.
    let failed_stack = unsafe { &mut *FAILED_INITIALIZATION_STACK.0.get() };
    if failed_stack.is_some() {
        return Err(SchedulerError::Poisoned);
    }
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
    if let Err(error) = map_task_stack(&mut stack, address_space, allocator) {
        if stack.is_empty() {
            return Err(error.into());
        }
        runtime.slots[idle.slot()].stack = Some(stack);
        return Err(rollback_initialization(
            &mut runtime,
            idle,
            address_space,
            allocator,
            failed_stack,
            RollbackCause::Stack(error),
        ));
    }
    let context = match initialize_context(
        stack.virtual_start(),
        stack.virtual_end(),
        task_trampoline as *const () as usize as u64,
        fatal_trampoline_return as *const () as usize as u64,
    ) {
        Ok(context) => context,
        Err(error) => {
            runtime.slots[idle.slot()].stack = Some(stack);
            return Err(rollback_initialization(
                &mut runtime,
                idle,
                address_space,
                allocator,
                failed_stack,
                RollbackCause::Context(error),
            ));
        }
    };
    runtime.slots[idle.slot()] = RuntimeSlot {
        entry: Some(idle_task),
        context,
        stack: Some(stack),
    };
    let publication = super::interrupts::publish_task_stack(
        idle,
        runtime.slots[idle.slot()]
            .stack
            .as_ref()
            .ok_or(SchedulerError::InvalidEntry)?
            .virtual_start(),
        runtime.slots[idle.slot()]
            .stack
            .as_ref()
            .ok_or(SchedulerError::InvalidEntry)?
            .virtual_end(),
    );
    if let Err(error) = publication {
        return Err(rollback_initialization(
            &mut runtime,
            idle,
            address_space,
            allocator,
            failed_stack,
            RollbackCause::Publication(error),
        ));
    }
    if let Err(error) = runtime.policy.check_invariants() {
        return Err(rollback_initialization(
            &mut runtime,
            idle,
            address_space,
            allocator,
            failed_stack,
            RollbackCause::Task(error),
        ));
    }
    let bootstrap = runtime.policy.bootstrap_id();
    *cell = Some(runtime);
    Ok((bootstrap, idle))
}

fn rollback_initialization(
    runtime: &mut Runtime,
    idle: TaskId,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
    failed_stack: &mut Option<TaskStackMapping>,
    original: RollbackCause,
) -> SchedulerError {
    super::interrupts::unpublish_task_stack(idle.slot());
    let Some(mut stack) = runtime.slots[idle.slot()].stack.take() else {
        runtime.slots[idle.slot()] = RuntimeSlot::EMPTY;
        return SchedulerError::InitializationRollbackFailed {
            original,
            cleanup: RollbackCause::Runtime,
        };
    };
    runtime.slots[idle.slot()] = RuntimeSlot::EMPTY;
    match reclaim_task_stack(&mut stack, address_space, allocator) {
        Ok(()) => initialization_cleanup_result(original, Ok(())),
        Err(cleanup) => {
            *failed_stack = Some(stack);
            initialization_cleanup_result(original, Err(cleanup))
        }
    }
}

const fn initialization_cleanup_result(
    original: RollbackCause,
    cleanup: Result<(), TaskStackError>,
) -> SchedulerError {
    match cleanup {
        Ok(()) => match original {
            RollbackCause::Task(error) => SchedulerError::Task(error),
            RollbackCause::Stack(error) => SchedulerError::Stack(error),
            RollbackCause::Context(error) => SchedulerError::Context(error),
            RollbackCause::Publication(error) => SchedulerError::Publication(error),
            RollbackCause::Runtime | RollbackCause::Combined { .. } => {
                SchedulerError::InitializationRollbackFailed {
                    original,
                    cleanup: RollbackCause::Runtime,
                }
            }
        },
        Err(cleanup) => SchedulerError::InitializationRollbackFailed {
            original,
            cleanup: RollbackCause::Stack(cleanup),
        },
    }
}

struct SpawnRollbackOutcome {
    error: SchedulerError,
    report: SpawnRollbackReport,
}

fn rollback_spawn_publication(
    id: TaskId,
    runtime: &mut Runtime,
    address_space: &mut ActiveAddressSpace,
    allocator: &mut EarlyPhysicalPageAllocator,
    original: super::interrupts::AttributionError,
) -> SpawnRollbackOutcome {
    // Publication is made inactive before ownership moves. Policy abort is
    // attempted only after reclaim succeeds; a failed reclaim therefore keeps
    // the live policy entry paired with retained stack metadata.
    super::interrupts::unpublish_task_stack(id.slot());
    let publication_inactive = !super::interrupts::task_stack_published(id);
    let Some(mut owned) = runtime.slots[id.slot()].stack.take() else {
        runtime.poisoned = true;
        return SpawnRollbackOutcome {
            error: SchedulerError::SpawnRollbackFailed {
                original: RollbackCause::Publication(original),
                cleanup: RollbackCause::Runtime,
            },
            report: SpawnRollbackReport {
                publication_inactive,
                stack: StackRollbackState::Missing,
                policy_aborted: false,
                slot_empty: false,
            },
        };
    };
    let original_stack = owned.clone();
    let allocator_before = allocator.clone();
    let mapped_before = address_space.mapped_pages();
    let reclaim = reclaim_task_stack(&mut owned, address_space, allocator);
    let Err(reclaim_error) = reclaim else {
        let policy = runtime.policy.abort_spawn(id);
        let Err(policy_error) = policy else {
            runtime.slots[id.slot()] = RuntimeSlot::EMPTY;
            return SpawnRollbackOutcome {
                error: SchedulerError::Publication(original),
                report: SpawnRollbackReport {
                    publication_inactive,
                    stack: StackRollbackState::Reclaimed,
                    policy_aborted: true,
                    slot_empty: true,
                },
            };
        };
        let restore = restore_task_stack(
            &original_stack,
            address_space,
            allocator,
            &allocator_before,
            mapped_before,
        );
        runtime.slots[id.slot()].stack = Some(original_stack);
        runtime.poisoned = true;
        return SpawnRollbackOutcome {
            error: match restore {
                Ok(()) => SchedulerError::SpawnRollbackFailed {
                    original: RollbackCause::Publication(original),
                    cleanup: RollbackCause::Task(policy_error),
                },
                Err(restore_error) => SchedulerError::SpawnRollbackFailed {
                    original: RollbackCause::Publication(original),
                    cleanup: RollbackCause::Combined {
                        first: RollbackLeaf::Task(policy_error),
                        second: RollbackLeaf::Stack(restore_error),
                    },
                },
            },
            report: SpawnRollbackReport {
                publication_inactive,
                stack: StackRollbackState::Reclaimed,
                policy_aborted: false,
                slot_empty: false,
            },
        };
    };
    runtime.slots[id.slot()].stack = Some(owned);
    runtime.poisoned = true;
    SpawnRollbackOutcome {
        error: SchedulerError::SpawnRollbackFailed {
            original: RollbackCause::Publication(original),
            cleanup: RollbackCause::Stack(reclaim_error),
        },
        report: SpawnRollbackReport {
            publication_inactive,
            stack: StackRollbackState::Retained,
            policy_aborted: false,
            slot_empty: false,
        },
    }
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
    let _preemption_guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;
    if entry as usize == 0 {
        return Err(SchedulerError::InvalidEntry);
    }
    let runtime = runtime_mut()?;
    let id = runtime.policy.spawn()?;
    let mut stack = match TaskStackMapping::empty(id.slot()).map_err(TaskStackError::Layout) {
        Ok(stack) => stack,
        Err(error) => {
            if runtime.policy.abort_spawn(id).is_err() {
                runtime.poisoned = true;
                return Err(SchedulerError::SpawnRollbackFailed {
                    original: RollbackCause::Stack(error),
                    cleanup: RollbackCause::Runtime,
                });
            }
            return Err(error.into());
        }
    };
    if let Err(error) = map_task_stack(&mut stack, address_space, allocator) {
        if !stack.is_empty() {
            runtime.slots[id.slot()].stack = Some(stack);
            runtime.poisoned = true;
            return Err(SchedulerError::SpawnRollbackFailed {
                original: RollbackCause::Stack(error),
                cleanup: RollbackCause::Stack(error),
            });
        }
        if let Err(cleanup) = runtime.policy.abort_spawn(id) {
            runtime.poisoned = true;
            return Err(SchedulerError::SpawnRollbackFailed {
                original: RollbackCause::Stack(error),
                cleanup: RollbackCause::Task(cleanup),
            });
        }
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
            if let Err(cleanup) = reclaim_task_stack(&mut stack, address_space, allocator) {
                runtime.slots[id.slot()].stack = Some(stack);
                runtime.poisoned = true;
                let policy_cleanup = runtime.policy.abort_spawn(id);
                return Err(SchedulerError::SpawnRollbackFailed {
                    original: RollbackCause::Context(error),
                    cleanup: policy_cleanup
                        .err()
                        .map_or(RollbackCause::Stack(cleanup), RollbackCause::Task),
                });
            }
            if let Err(cleanup) = runtime.policy.abort_spawn(id) {
                runtime.poisoned = true;
                return Err(SchedulerError::SpawnRollbackFailed {
                    original: RollbackCause::Context(error),
                    cleanup: RollbackCause::Task(cleanup),
                });
            }
            return Err(error.into());
        }
    };
    runtime.slots[id.slot()] = RuntimeSlot {
        entry: Some(entry),
        context,
        stack: Some(stack),
    };
    let published = runtime.slots[id.slot()]
        .stack
        .as_ref()
        .ok_or(SchedulerError::InvalidEntry)?;
    if let Err(error) = super::interrupts::publish_task_stack(
        id,
        published.virtual_start(),
        published.virtual_end(),
    ) {
        let outcome = rollback_spawn_publication(id, runtime, address_space, allocator, error);
        debug_assert!(outcome.report.publication_inactive);
        debug_assert!(outcome.report.is_consistent());
        return Err(outcome.error);
    }
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

/// Returns non-owning task diagnostics for integration validation.
///
/// # Errors
///
/// Returns an error when the runtime is unavailable or `id` is stale or invalid.
pub fn task_diagnostics(id: TaskId) -> Result<TaskDiagnostics, SchedulerError> {
    let runtime = runtime_ref()?;
    let state = runtime.policy.state(id)?;
    let slot = &runtime.slots[id.slot()];
    let (stack_start, stack_end) = slot
        .stack
        .as_ref()
        .map_or((0, 0), |stack| (stack.virtual_start(), stack.virtual_end()));
    Ok(TaskDiagnostics {
        state,
        queued: runtime.policy.is_queued(id),
        rsp: slot.context.rsp,
        stack_start,
        stack_end,
    })
}

/// Returns allocation-free scheduler statistics.
///
/// # Errors
///
/// Returns an error when the runtime is unavailable or poisoned.
pub fn stats() -> Result<crate::task::SchedulerStats, SchedulerError> {
    Ok(runtime_ref()?.policy.stats())
}

/// Returns the most recent stack pointer captured inside idle.
pub fn idle_rsp() -> u64 {
    IDLE_RSP.load(Ordering::Relaxed)
}

/// Returns the generation-tagged idle task identity.
///
/// # Errors
///
/// Returns an error when the scheduler policy has not been initialized or
/// its idle-task invariant is unavailable.
pub fn idle_task_id() -> Result<TaskId, SchedulerError> {
    Ok(runtime_ref()?.policy.idle_id())
}

/// Returns attempts to enter scheduler mutation from interrupt context.
pub fn interrupt_context_entry_count() -> u64 {
    INTERRUPT_CONTEXT_ENTRIES.load(Ordering::Relaxed)
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

/// Validates policy state together with live entries, contexts, and stack ownership.
///
/// # Errors
///
/// Returns an error when any runtime resource disagrees with task policy or
/// active page-table mappings.
pub fn check_runtime_invariants(address_space: &ActiveAddressSpace) -> Result<(), SchedulerError> {
    let runtime = runtime_ref()?;
    runtime.policy.check_invariants()?;
    for slot_index in 0..MAX_TASKS {
        let (_, state) = runtime.policy.slot_snapshot(slot_index)?;
        let slot = &runtime.slots[slot_index];
        validate_slot_metadata(slot_index, state, slot)?;
        if slot_index == 0 {
            if !super::interrupts::task_stack_published(runtime.policy.bootstrap_id()) {
                return Err(SchedulerError::InvalidEntry);
            }
            continue;
        }
        match state {
            TaskState::Vacant => {
                if super::interrupts::task_stack_published(
                    TaskId::new(
                        u8::try_from(slot_index).map_err(|_| SchedulerError::InvalidEntry)?,
                        runtime.policy.slot_snapshot(slot_index)?.0.generation(),
                    )
                    .map_err(|_| SchedulerError::InvalidEntry)?,
                ) {
                    return Err(SchedulerError::InvalidEntry);
                }
            }
            TaskState::Ready | TaskState::Running | TaskState::Exited => {
                let stack = slot.stack.as_ref().ok_or(SchedulerError::InvalidEntry)?;
                if !stack.contains(slot.context.rsp) {
                    return Err(SchedulerError::InvalidEntry);
                }
                let id = runtime.policy.slot_snapshot(slot_index)?.0;
                if !super::interrupts::task_stack_published(id) {
                    return Err(SchedulerError::InvalidEntry);
                }
                validate_task_stack(stack, address_space)?;
            }
            TaskState::Blocked => return Err(SchedulerError::InvalidEntry),
        }
    }
    for left in 1..MAX_TASKS {
        let Some(left_stack) = runtime.slots[left].stack.as_ref() else {
            continue;
        };
        for right in (left + 1)..MAX_TASKS {
            let Some(right_stack) = runtime.slots[right].stack.as_ref() else {
                continue;
            };
            if left_stack.virtual_start() < right_stack.virtual_end()
                && right_stack.virtual_start() < left_stack.virtual_end()
            {
                return Err(SchedulerError::InvalidEntry);
            }
            for left_page in 0..super::task_stack::TASK_STACK_PAGE_COUNT {
                for right_page in 0..super::task_stack::TASK_STACK_PAGE_COUNT {
                    if left_stack.physical_page(left_page) == right_stack.physical_page(right_page)
                    {
                        return Err(SchedulerError::InvalidEntry);
                    }
                }
            }
        }
    }
    let bootstrap_rsp = runtime.slots[0].context.rsp;
    if bootstrap_rsp != 0 {
        for slot in 1..MAX_TASKS {
            if runtime.slots[slot]
                .stack
                .as_ref()
                .is_some_and(|stack| stack.contains(bootstrap_rsp))
            {
                return Err(SchedulerError::InvalidEntry);
            }
        }
    }
    Ok(())
}

fn validate_slot_metadata(
    slot_index: usize,
    state: TaskState,
    slot: &RuntimeSlot,
) -> Result<(), SchedulerError> {
    let valid = if slot_index == 0 {
        slot.entry.is_none() && slot.stack.is_none()
    } else {
        match state {
            TaskState::Vacant => {
                slot.entry.is_none() && slot.context.rsp == 0 && slot.stack.is_none()
            }
            TaskState::Ready | TaskState::Running | TaskState::Exited => {
                slot.entry.is_some() && slot.context.rsp != 0 && slot.stack.is_some()
            }
            TaskState::Blocked => false,
        }
    };
    valid.then_some(()).ok_or(SchedulerError::InvalidEntry)
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
    let _preemption_guard =
        crate::preemption::PreemptionGuard::enter().map_err(|_| SchedulerError::PreemptionFault)?;
    let runtime = runtime_mut()?;
    let prepared = runtime.policy.prepare_reap(id)?;
    let slot = &mut runtime.slots[id.slot()];
    let mut stack = slot.stack.take().ok_or(SchedulerError::InvalidEntry)?;
    if let Err(error) = reclaim_task_stack(&mut stack, address_space, allocator) {
        slot.stack = Some(stack);
        return Err(error.into());
    }
    *slot = RuntimeSlot::EMPTY;
    super::interrupts::unpublish_task_stack(id.slot());
    runtime.policy.commit_reap(prepared);
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
    if reject_interrupt_context().is_err()
        || reject_preemption_disabled().is_err()
        || SWITCHING.swap(true, Ordering::Acquire)
    {
        fatal_scheduler();
    }
    let plan = (|| {
        let _preemption_guard = crate::preemption::PreemptionGuard::enter().ok()?;
        let runtime = runtime_mut().ok()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let next = candidate.park_bootstrap().ok()?;
        commit_switch_candidate(runtime, old, candidate, next).ok()
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
    reject_preemption_disabled()?;
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result: Result<(*mut u64, u64), SchedulerError> = (|| {
        let _preemption_guard = crate::preemption::PreemptionGuard::enter()
            .map_err(|_| SchedulerError::PreemptionFault)?;
        let runtime = runtime_mut()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let next = candidate.begin_idle_probe()?;
        commit_switch_candidate(runtime, old, candidate, next)
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
    reject_preemption_disabled()?;
    if SWITCHING.swap(true, Ordering::Acquire) {
        return Err(SchedulerError::Reentrant);
    }
    let result = (|| {
        let _preemption_guard = crate::preemption::PreemptionGuard::enter()
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
    if reject_interrupt_context().is_err()
        || reject_preemption_disabled().is_err()
        || SWITCHING.swap(true, Ordering::Acquire)
    {
        fatal_scheduler();
    }
    let plan = (|| {
        let _preemption_guard = crate::preemption::PreemptionGuard::enter().ok()?;
        let runtime = runtime_mut().ok()?;
        let old = runtime.policy.current();
        let mut candidate = runtime.policy;
        let next = candidate.exit_current().ok()?;
        commit_switch_candidate(runtime, old, candidate, next).ok()
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

fn validate_selected_context(runtime: &Runtime, id: TaskId) -> Result<(), SchedulerError> {
    let slot = &runtime.slots[id.slot()];
    if slot.context.rsp == 0 {
        return Err(SchedulerError::InvalidEntry);
    }
    if id.slot() == 0 {
        if slot.entry.is_some() || slot.stack.is_some() {
            return Err(SchedulerError::InvalidEntry);
        }
        return Ok(());
    }
    let stack = slot.stack.as_ref().ok_or(SchedulerError::InvalidEntry)?;
    if slot.entry.is_none() || !stack.contains(slot.context.rsp) {
        return Err(SchedulerError::InvalidEntry);
    }
    Ok(())
}

fn commit_switch_candidate(
    runtime: &mut Runtime,
    old: TaskId,
    candidate: Scheduler,
    next: TaskId,
) -> Result<(*mut u64, u64), SchedulerError> {
    validate_selected_context(runtime, next)?;
    let old_rsp = &raw mut runtime.slots[old.slot()].context.rsp;
    let new_rsp = runtime.slots[next.slot()].context.rsp;
    runtime.policy = candidate;
    Ok((old_rsp, new_rsp))
}
fn idle_task() {
    loop {
        let rsp: u64;
        // SAFETY: reading RSP has no side effects and records only diagnostic state.
        unsafe {
            core::arch::asm!("mov {}, rsp", out(reg) rsp, options(nomem, nostack, preserves_flags));
        }
        IDLE_RSP.store(rsp, Ordering::Relaxed);
        super::cpu::halt_once();
        if yield_now().is_err() {
            fatal_scheduler();
        }
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
fn runtime_ref() -> Result<&'static Runtime, SchedulerError> {
    reject_interrupt_context()?;
    unsafe {
        let runtime = (&*RUNTIME.0.get())
            .as_ref()
            .ok_or(SchedulerError::NotInitialized)?;
        if runtime.poisoned {
            Err(SchedulerError::Poisoned)
        } else {
            Ok(runtime)
        }
    }
}
fn runtime_mut() -> Result<&'static mut Runtime, SchedulerError> {
    reject_interrupt_context()?;
    unsafe {
        let runtime = (&mut *RUNTIME.0.get())
            .as_mut()
            .ok_or(SchedulerError::NotInitialized)?;
        if runtime.poisoned {
            Err(SchedulerError::Poisoned)
        } else {
            Ok(runtime)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn empty_runtime(policy: Scheduler) -> Runtime {
        Runtime {
            policy,
            slots: [const { RuntimeSlot::EMPTY }; MAX_TASKS],
            poisoned: false,
        }
    }

    fn test_entry() {}

    #[test]
    fn runtime_metadata_detects_vacant_nonempty_slot() {
        let mut runtime = empty_runtime(Scheduler::new());
        runtime.slots[2].entry = Some(test_entry);
        assert_eq!(
            validate_slot_metadata(2, TaskState::Vacant, &runtime.slots[2]),
            Err(SchedulerError::InvalidEntry)
        );

        runtime.slots[2] = RuntimeSlot::EMPTY;
        runtime.slots[2].context.rsp = 1;
        assert_eq!(
            validate_slot_metadata(2, TaskState::Vacant, &runtime.slots[2]),
            Err(SchedulerError::InvalidEntry)
        );
    }

    #[test]
    fn initialization_rollback_preserves_original_or_reports_cleanup() {
        let publication = RollbackCause::Publication(
            super::super::interrupts::AttributionError::AlreadyPublished,
        );
        assert_eq!(
            initialization_cleanup_result(publication, Ok(())),
            SchedulerError::Publication(
                super::super::interrupts::AttributionError::AlreadyPublished
            )
        );
        let cleanup = TaskStackError::CorruptMapping;
        assert_eq!(
            initialization_cleanup_result(publication, Err(cleanup)),
            SchedulerError::InitializationRollbackFailed {
                original: publication,
                cleanup: RollbackCause::Stack(cleanup),
            }
        );
    }

    #[test]
    fn worker_publication_rollback_reports_keep_slot_and_policy_agreeing() {
        let cases = [
            SpawnRollbackReport {
                publication_inactive: true,
                stack: StackRollbackState::Reclaimed,
                policy_aborted: true,
                slot_empty: true,
            },
            SpawnRollbackReport {
                publication_inactive: true,
                stack: StackRollbackState::Retained,
                policy_aborted: false,
                slot_empty: false,
            },
            SpawnRollbackReport {
                publication_inactive: true,
                stack: StackRollbackState::Reclaimed,
                policy_aborted: false,
                slot_empty: false,
            },
        ];
        assert!(cases.into_iter().all(SpawnRollbackReport::is_consistent));
        assert!(
            !SpawnRollbackReport {
                publication_inactive: true,
                stack: StackRollbackState::Missing,
                policy_aborted: true,
                slot_empty: true,
            }
            .is_consistent()
        );
    }

    #[test]
    fn invalid_selected_context_never_commits_candidate_policy() {
        let mut yielding_policy = Scheduler::new();
        yielding_policy.spawn().unwrap();
        let mut yielding = empty_runtime(yielding_policy);
        let before = yielding.policy;
        let old = before.current();
        let mut candidate = before;
        let next = candidate.yield_current().unwrap().unwrap();
        assert_eq!(
            commit_switch_candidate(&mut yielding, old, candidate, next),
            Err(SchedulerError::InvalidEntry)
        );
        assert_eq!(yielding.policy, before);

        let mut exiting_policy = Scheduler::new();
        let worker = exiting_policy.spawn().unwrap();
        assert_eq!(exiting_policy.yield_current(), Ok(Some(worker)));
        let mut exiting = empty_runtime(exiting_policy);
        let before = exiting.policy;
        let old = before.current();
        let mut candidate = before;
        let next = candidate.exit_current().unwrap();
        assert_eq!(
            commit_switch_candidate(&mut exiting, old, candidate, next),
            Err(SchedulerError::InvalidEntry)
        );
        assert_eq!(exiting.policy, before);

        for transition in [Scheduler::park_bootstrap, Scheduler::begin_idle_probe] {
            let mut runtime = empty_runtime(Scheduler::new());
            let before = runtime.policy;
            let old = before.current();
            let mut candidate = before;
            let next = transition(&mut candidate).unwrap();
            assert_eq!(
                commit_switch_candidate(&mut runtime, old, candidate, next),
                Err(SchedulerError::InvalidEntry)
            );
            assert_eq!(runtime.policy, before);
        }
    }

    #[test]
    fn protected_transition_rejects_stack_switch() {
        let _lock = crate::preemption::TEST_LOCK.lock().unwrap();
        assert_eq!(reject_preemption_disabled(), Ok(()));
        let guard = crate::preemption::PreemptionGuard::enter().unwrap();
        assert_eq!(
            reject_preemption_disabled(),
            Err(SchedulerError::PreemptionDisabled)
        );
        drop(guard);
        assert_eq!(reject_preemption_disabled(), Ok(()));
    }
}
