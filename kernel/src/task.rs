//! Bounded, allocation-free cooperative task bookkeeping.
//!
//! This module intentionally contains no paging, global state, or privileged
//! instructions.  It is the deterministic policy half of the first `FinnOS`
//! scheduler and can therefore be exercised by host tests.
// The Result-returning operations each document their operation and use a
// common structured error type; repeating every variant in every doc block
// would obscure the fixed-state-machine API.
#![allow(clippy::cast_possible_truncation)]
#![allow(clippy::missing_errors_doc)]
#![allow(clippy::missing_panics_doc)]
#![allow(clippy::must_use_candidate)]

/// The total number of task slots, including bootstrap and idle tasks.
pub const MAX_TASKS: usize = 8;
/// Bootstrap's fixed task-table slot.
pub const BOOTSTRAP_SLOT: usize = 0;
/// Idle's fixed task-table slot.
pub const IDLE_SLOT: usize = 1;

/// A stable task identity made from a slot and a non-zero generation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TaskId {
    slot: u8,
    generation: u32,
}

impl TaskId {
    /// Makes a task identity when its fields are valid.
    pub const fn new(slot: u8, generation: u32) -> Result<Self, TaskError> {
        if slot as usize >= MAX_TASKS {
            return Err(TaskError::InvalidTaskId);
        }
        if generation == 0 {
            return Err(TaskError::InvalidTaskId);
        }
        Ok(Self { slot, generation })
    }

    /// Returns this ID's task-table slot.
    pub const fn slot(self) -> usize {
        self.slot as usize
    }
    /// Returns this ID's generation.
    pub const fn generation(self) -> u32 {
        self.generation
    }
}

/// Explicit lifecycle state for a task-table entry.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskState {
    /// The slot has no live task and may be reused.
    Vacant,
    /// The task may be selected from the ordinary runnable queue.
    Ready,
    /// The task currently owns execution on the BSP.
    Running,
    /// The task is deliberately not eligible to run.
    Blocked,
    /// The task returned and retains resources until another task reaps it.
    Exited,
}

/// Failures from bounded task bookkeeping.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskError {
    /// Scheduler mutation was attempted from an interrupt handler.
    InterruptContextForbidden,
    /// An identity is malformed or references no current task.
    InvalidTaskId,
    /// An identity names a recycled slot.
    StaleTaskId,
    /// No ordinary task slot is free.
    CapacityExhausted,
    /// A state transition violates the task lifecycle.
    InvalidTransition,
    /// The fixed FIFO has no remaining entry.
    QueueFull,
    /// The fixed FIFO contains no entry.
    QueueEmpty,
    /// The ID is already queued.
    QueueDuplicate,
    /// A reserved task was used where an ordinary task is required.
    ReservedTask,
    /// The generation cannot be advanced safely.
    GenerationOverflow,
    /// Scheduler bookkeeping is inconsistent.
    CorruptState,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Entry {
    generation: u32,
    state: TaskState,
}

impl Entry {
    const VACANT: Self = Self {
        generation: 1,
        state: TaskState::Vacant,
    };
}

/// A deterministic, fixed-capacity FIFO of ordinary ready tasks.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RunnableQueue {
    ids: [Option<TaskId>; MAX_TASKS],
    head: usize,
    len: usize,
}

impl RunnableQueue {
    /// Constructs an empty runnable queue.
    pub const fn new() -> Self {
        Self {
            ids: [None; MAX_TASKS],
            head: 0,
            len: 0,
        }
    }
    /// Returns the number of queued tasks.
    pub const fn len(&self) -> usize {
        self.len
    }
    /// Returns whether no tasks are queued.
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }
    /// Adds an ID at the FIFO tail.
    pub fn push(&mut self, id: TaskId) -> Result<(), TaskError> {
        if self.len == MAX_TASKS {
            return Err(TaskError::QueueFull);
        }
        if self.contains(id) {
            return Err(TaskError::QueueDuplicate);
        }
        let tail = (self.head + self.len) % MAX_TASKS;
        self.ids[tail] = Some(id);
        self.len += 1;
        Ok(())
    }
    /// Removes the oldest ID.
    pub fn pop(&mut self) -> Result<TaskId, TaskError> {
        if self.len == 0 {
            return Err(TaskError::QueueEmpty);
        }
        let id = self.ids[self.head].take().ok_or(TaskError::CorruptState)?;
        self.head = (self.head + 1) % MAX_TASKS;
        self.len -= 1;
        Ok(id)
    }
    /// Returns whether the queue contains `id`.
    pub fn contains(&self, id: TaskId) -> bool {
        (0..self.len).any(|offset| self.ids[(self.head + offset) % MAX_TASKS] == Some(id))
    }

    fn count(&self, id: TaskId) -> usize {
        (0..self.len)
            .filter(|offset| self.ids[(self.head + offset) % MAX_TASKS] == Some(id))
            .count()
    }

    fn remove(&mut self, id: TaskId) -> Result<(), TaskError> {
        if !self.contains(id) {
            return Err(TaskError::QueueEmpty);
        }
        let mut retained = Self::new();
        while let Ok(candidate) = self.pop() {
            if candidate != id {
                retained.push(candidate)?;
            }
        }
        *self = retained;
        Ok(())
    }
}

impl Default for RunnableQueue {
    fn default() -> Self {
        Self::new()
    }
}

/// A small copyable view of scheduler state.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SchedulerStats {
    /// Number of task slots.
    pub capacity: usize,
    /// Number of vacant slots.
    pub vacant_tasks: usize,
    /// Number of ready tasks.
    pub ready_tasks: usize,
    /// Number of running tasks.
    pub running_tasks: usize,
    /// Number of blocked tasks.
    pub blocked_tasks: usize,
    /// Number of exited tasks.
    pub exited_tasks: usize,
    /// Number of queued ordinary tasks.
    pub queue_length: usize,
    /// Number of successful worker creations.
    pub created_task_count: u64,
    /// Number of worker completions.
    pub completed_task_count: u64,
    /// Number of completed task reaps.
    pub reaped_task_count: u64,
    /// Number of selected context switches.
    pub context_switch_count: u64,
    /// Number of cooperative yields.
    pub yield_count: u64,
}

/// Heap-free single-BSP cooperative scheduler policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Scheduler {
    entries: [Entry; MAX_TASKS],
    queue: RunnableQueue,
    current: TaskId,
    created: u64,
    completed: u64,
    reaped: u64,
    switches: u64,
    yields: u64,
}

/// Prevalidated, infallible task-reap policy update.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PreparedReap {
    id: TaskId,
    next_generation: u32,
}

impl Scheduler {
    /// Initializes bootstrap as running and idle as ready but not queued.
    pub fn new() -> Self {
        let mut entries = [Entry::VACANT; MAX_TASKS];
        entries[BOOTSTRAP_SLOT] = Entry {
            generation: 1,
            state: TaskState::Running,
        };
        entries[IDLE_SLOT] = Entry {
            generation: 1,
            state: TaskState::Ready,
        };
        Self {
            entries,
            queue: RunnableQueue::new(),
            current: TaskId::new(0, 1).expect("constant task ID"),
            created: 0,
            completed: 0,
            reaped: 0,
            switches: 0,
            yields: 0,
        }
    }
    /// Returns bootstrap's stable identity.
    pub const fn bootstrap_id(&self) -> TaskId {
        TaskId {
            slot: 0,
            generation: 1,
        }
    }
    /// Returns idle's stable identity.
    pub const fn idle_id(&self) -> TaskId {
        TaskId {
            slot: 1,
            generation: 1,
        }
    }
    /// Returns the currently running task.
    pub const fn current(&self) -> TaskId {
        self.current
    }
    /// Returns a task's current state, rejecting stale IDs.
    pub fn state(&self, id: TaskId) -> Result<TaskState, TaskError> {
        Ok(self.entry(id)?.state)
    }
    /// Returns the current generation-tagged identity and state for a slot.
    pub fn slot_snapshot(&self, slot: usize) -> Result<(TaskId, TaskState), TaskError> {
        let entry = self.entries.get(slot).ok_or(TaskError::InvalidTaskId)?;
        Ok((
            TaskId::new(
                u8::try_from(slot).map_err(|_| TaskError::InvalidTaskId)?,
                entry.generation,
            )?,
            entry.state,
        ))
    }
    /// Returns whether an identity is currently queued.
    pub fn is_queued(&self, id: TaskId) -> bool {
        self.queue.contains(id)
    }
    /// Reserves the lowest vacant ordinary slot and appends it to the runnable FIFO.
    pub fn spawn(&mut self) -> Result<TaskId, TaskError> {
        ensure_not_interrupt_context()?;
        let slot = (2..MAX_TASKS)
            .find(|&slot| self.entries[slot].state == TaskState::Vacant)
            .ok_or(TaskError::CapacityExhausted)?;
        let id = TaskId::new(slot as u8, self.entries[slot].generation)?;
        let mut queue = self.queue;
        queue.push(id)?;
        self.entries[slot].state = TaskState::Ready;
        self.queue = queue;
        self.created = self.created.saturating_add(1);
        Ok(id)
    }
    /// Cancels a newly created, not-yet-running worker after resource setup failed.
    ///
    /// # Errors
    ///
    /// Returns an error unless `id` is a current ready ordinary task in the
    /// runnable queue or its generation cannot advance.
    pub fn abort_spawn(&mut self, id: TaskId) -> Result<(), TaskError> {
        ensure_not_interrupt_context()?;
        if id.slot() < 2 || self.state(id)? != TaskState::Ready {
            return Err(TaskError::InvalidTransition);
        }
        let next_generation = self
            .entry(id)?
            .generation
            .checked_add(1)
            .ok_or(TaskError::GenerationOverflow)?;
        let next_created = self.created.checked_sub(1).ok_or(TaskError::CorruptState)?;
        let mut queue = self.queue;
        queue.remove(id)?;
        let entry = self.entry_mut(id)?;
        entry.state = TaskState::Vacant;
        entry.generation = next_generation;
        self.queue = queue;
        self.created = next_created;
        Ok(())
    }
    /// Performs deterministic policy for a cooperative yield and returns the selected peer.
    /// `None` means that there was no ordinary runnable peer and no switch is needed.
    pub fn yield_current(&mut self) -> Result<Option<TaskId>, TaskError> {
        ensure_not_interrupt_context()?;
        if self.current.slot() == IDLE_SLOT {
            return self.select_from_idle();
        }
        if self.queue.is_empty() {
            return Ok(None);
        }
        let previous = self.current;
        self.entry(previous)?;
        let mut queue = self.queue;
        queue.push(previous)?;
        let next = queue.pop()?;
        self.entry(next)?;
        self.entry_mut(previous)?.state = TaskState::Ready;
        self.entry_mut(next)?.state = TaskState::Running;
        self.queue = queue;
        self.current = next;
        self.yields = self.yields.saturating_add(1);
        self.switches = self.switches.saturating_add(1);
        Ok(Some(next))
    }
    /// Marks the current ordinary task exited and selects a peer or idle.
    pub fn exit_current(&mut self) -> Result<TaskId, TaskError> {
        ensure_not_interrupt_context()?;
        if self.current.slot() < 2 {
            return Err(TaskError::ReservedTask);
        }
        let mut queue = self.queue;
        let next = if queue.is_empty() {
            self.idle_id()
        } else {
            queue.pop()?
        };
        self.entry(next)?;
        self.entry_mut(self.current)?.state = TaskState::Exited;
        self.entry_mut(next)?.state = TaskState::Running;
        self.queue = queue;
        self.current = next;
        self.completed = self.completed.saturating_add(1);
        self.switches = self.switches.saturating_add(1);
        Ok(next)
    }
    /// Prevalidates reclamation without mutating policy state.
    pub fn prepare_reap(&self, id: TaskId) -> Result<PreparedReap, TaskError> {
        ensure_not_interrupt_context()?;
        if id.slot() < 2 {
            return Err(TaskError::ReservedTask);
        }
        if id == self.current {
            return Err(TaskError::InvalidTransition);
        }
        let entry = self.entry(id)?;
        if entry.state != TaskState::Exited {
            return Err(TaskError::InvalidTransition);
        }
        let next_generation = entry
            .generation
            .checked_add(1)
            .ok_or(TaskError::GenerationOverflow)?;
        Ok(PreparedReap {
            id,
            next_generation,
        })
    }

    /// Commits a previously prepared reap without a fallible operation.
    pub const fn commit_reap(&mut self, prepared: PreparedReap) {
        let entry = &mut self.entries[prepared.id.slot()];
        entry.state = TaskState::Vacant;
        entry.generation = prepared.next_generation;
        self.reaped = self.reaped.saturating_add(1);
    }

    /// Reclaims an exited ordinary task, invalidating its former identity.
    pub fn reap(&mut self, id: TaskId) -> Result<(), TaskError> {
        let prepared = self.prepare_reap(id)?;
        self.commit_reap(prepared);
        Ok(())
    }
    /// Blocks bootstrap permanently and selects the dedicated idle task.
    ///
    /// # Errors
    ///
    /// Returns an error unless bootstrap is the current running task or when
    /// called from interrupt context.
    pub fn park_bootstrap(&mut self) -> Result<TaskId, TaskError> {
        ensure_not_interrupt_context()?;
        if self.current.slot() != BOOTSTRAP_SLOT
            || self.entries[BOOTSTRAP_SLOT].state != TaskState::Running
            || self.entries[IDLE_SLOT].state != TaskState::Ready
        {
            return Err(TaskError::InvalidTransition);
        }
        let switches = self.switches.saturating_add(1);
        self.entries[BOOTSTRAP_SLOT].state = TaskState::Blocked;
        self.entries[IDLE_SLOT].state = TaskState::Running;
        self.current = self.idle_id();
        self.switches = switches;
        Ok(self.current)
    }

    /// Selects idle once while queueing bootstrap for a controlled test probe.
    ///
    /// # Errors
    ///
    /// Returns an error unless bootstrap is currently running and idle is ready.
    pub fn begin_idle_probe(&mut self) -> Result<TaskId, TaskError> {
        ensure_not_interrupt_context()?;
        if self.current.slot() != BOOTSTRAP_SLOT
            || self.entries[BOOTSTRAP_SLOT].state != TaskState::Running
            || self.entries[IDLE_SLOT].state != TaskState::Ready
            || !self.queue.is_empty()
        {
            return Err(TaskError::InvalidTransition);
        }
        let bootstrap = self.bootstrap_id();
        let mut queue = self.queue;
        queue.push(bootstrap)?;
        self.entries[BOOTSTRAP_SLOT].state = TaskState::Ready;
        self.queue = queue;
        self.entries[IDLE_SLOT].state = TaskState::Running;
        self.current = self.idle_id();
        self.switches = self.switches.saturating_add(1);
        Ok(self.current)
    }
    /// Checks the bounded task-table and queue invariants.
    pub fn check_invariants(&self) -> Result<(), TaskError> {
        let mut running = 0;
        for (slot, entry) in self.entries.iter().enumerate() {
            if entry.generation == 0 {
                return Err(TaskError::CorruptState);
            }
            if entry.state == TaskState::Running {
                running += 1;
            }
            if entry.state == TaskState::Vacant
                && self
                    .queue
                    .contains(TaskId::new(slot as u8, entry.generation)?)
            {
                return Err(TaskError::CorruptState);
            }
            if slot >= 2 || slot == BOOTSTRAP_SLOT {
                let id = TaskId::new(slot as u8, entry.generation)?;
                let expected = usize::from(entry.state == TaskState::Ready);
                if self.queue.count(id) != expected {
                    return Err(TaskError::CorruptState);
                }
            }
        }
        if running != 1 || self.state(self.current)? != TaskState::Running {
            return Err(TaskError::CorruptState);
        }
        if self.entries[BOOTSTRAP_SLOT].state == TaskState::Vacant
            || self.entries[IDLE_SLOT].state == TaskState::Vacant
        {
            return Err(TaskError::CorruptState);
        }
        for offset in 0..self.queue.len {
            let id = self.queue.ids[(self.queue.head + offset) % MAX_TASKS]
                .ok_or(TaskError::CorruptState)?;
            if id.slot() == IDLE_SLOT || self.state(id)? != TaskState::Ready || id == self.current {
                return Err(TaskError::CorruptState);
            }
        }
        Ok(())
    }
    /// Returns non-allocating scheduler statistics.
    pub fn stats(&self) -> SchedulerStats {
        let mut stats = SchedulerStats {
            capacity: MAX_TASKS,
            vacant_tasks: 0,
            ready_tasks: 0,
            running_tasks: 0,
            blocked_tasks: 0,
            exited_tasks: 0,
            queue_length: self.queue.len(),
            created_task_count: self.created,
            completed_task_count: self.completed,
            reaped_task_count: self.reaped,
            context_switch_count: self.switches,
            yield_count: self.yields,
        };
        for entry in self.entries {
            match entry.state {
                TaskState::Vacant => stats.vacant_tasks += 1,
                TaskState::Ready => stats.ready_tasks += 1,
                TaskState::Running => stats.running_tasks += 1,
                TaskState::Blocked => stats.blocked_tasks += 1,
                TaskState::Exited => stats.exited_tasks += 1,
            }
        }
        stats
    }
    fn select_from_idle(&mut self) -> Result<Option<TaskId>, TaskError> {
        if self.queue.is_empty() {
            return Ok(None);
        }
        let mut queue = self.queue;
        let next = queue.pop()?;
        self.entry(next)?;
        self.entries[IDLE_SLOT].state = TaskState::Ready;
        self.entry_mut(next)?.state = TaskState::Running;
        self.queue = queue;
        self.current = next;
        self.switches = self.switches.saturating_add(1);
        Ok(Some(next))
    }
    fn entry(&self, id: TaskId) -> Result<&Entry, TaskError> {
        let entry = self
            .entries
            .get(id.slot())
            .ok_or(TaskError::InvalidTaskId)?;
        if entry.generation != id.generation() {
            return Err(TaskError::StaleTaskId);
        }
        Ok(entry)
    }
    fn entry_mut(&mut self, id: TaskId) -> Result<&mut Entry, TaskError> {
        let entry = self
            .entries
            .get_mut(id.slot())
            .ok_or(TaskError::InvalidTaskId)?;
        if entry.generation != id.generation() {
            return Err(TaskError::StaleTaskId);
        }
        Ok(entry)
    }
}

fn ensure_not_interrupt_context() -> Result<(), TaskError> {
    if crate::interrupt::in_interrupt_context() {
        return Err(TaskError::InterruptContextForbidden);
    }
    Ok(())
}

impl Default for Scheduler {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ids_reject_invalid_fields() {
        assert_eq!(TaskId::new(8, 1), Err(TaskError::InvalidTaskId));
        assert_eq!(TaskId::new(1, 0), Err(TaskError::InvalidTaskId));
    }
    #[test]
    fn queue_wraps_fifo() {
        let mut q = RunnableQueue::new();
        for slot in 2..8 {
            q.push(TaskId::new(slot, 1).unwrap()).unwrap();
        }
        for slot in 2..5 {
            assert_eq!(q.pop().unwrap().slot(), usize::try_from(slot).unwrap());
        }
        for slot in 2..5 {
            q.push(TaskId::new(slot, 2).unwrap()).unwrap();
        }
        let mut seen = 0;
        while q.pop().is_ok() {
            seen += 1;
        }
        assert_eq!(seen, 6);
    }
    #[test]
    fn scheduler_round_robin_and_reuse() {
        let mut s = Scheduler::new();
        let a = s.spawn().unwrap();
        let b = s.spawn().unwrap();
        assert_eq!(s.yield_current().unwrap(), Some(a));
        assert_eq!(s.yield_current().unwrap(), Some(b));
        assert_eq!(s.exit_current().unwrap(), s.bootstrap_id());
        assert_eq!(s.state(b), Ok(TaskState::Exited));
        s.reap(b).unwrap();
        assert_eq!(s.state(b), Err(TaskError::StaleTaskId));
        let replacement = s.spawn().unwrap();
        assert_eq!(replacement.slot(), b.slot());
        assert_ne!(replacement.generation(), b.generation());
        s.check_invariants().unwrap();
    }

    #[test]
    fn scheduler_rejects_interrupt_context_mutation() {
        let mut scheduler = Scheduler::new();
        let guard = crate::interrupt::InterruptContextGuard::enter().unwrap();
        assert_eq!(scheduler.spawn(), Err(TaskError::InterruptContextForbidden));
        assert_eq!(
            scheduler.yield_current(),
            Err(TaskError::InterruptContextForbidden)
        );
        drop(guard);
    }

    #[test]
    fn saturating_counters_do_not_abort_committed_transitions() {
        let mut scheduler = Scheduler::new();
        let worker = scheduler.spawn().unwrap();
        scheduler.switches = u64::MAX;
        scheduler.yields = u64::MAX;
        assert_eq!(scheduler.yield_current(), Ok(Some(worker)));
        assert_eq!(
            scheduler.yield_current(),
            Ok(Some(scheduler.bootstrap_id()))
        );
        assert_eq!(scheduler.switches, u64::MAX);
        assert_eq!(scheduler.yields, u64::MAX);
        scheduler.check_invariants().unwrap();
    }

    #[test]
    fn generation_overflow_preserves_exited_slot() {
        let mut scheduler = Scheduler::new();
        scheduler.entries[2] = Entry {
            generation: u32::MAX,
            state: TaskState::Exited,
        };
        let id = TaskId::new(2, u32::MAX).unwrap();
        let reaped = scheduler.reaped;
        assert_eq!(scheduler.reap(id), Err(TaskError::GenerationOverflow));
        assert_eq!(scheduler.entries[2].state, TaskState::Exited);
        assert_eq!(scheduler.entries[2].generation, u32::MAX);
        assert_eq!(scheduler.reaped, reaped);
    }

    #[test]
    fn abort_generation_overflow_preserves_ready_queue_entry() {
        let mut scheduler = Scheduler::new();
        let id = TaskId::new(2, u32::MAX).unwrap();
        scheduler.entries[2] = Entry {
            generation: u32::MAX,
            state: TaskState::Ready,
        };
        scheduler.queue.push(id).unwrap();
        scheduler.created = 1;
        assert_eq!(
            scheduler.abort_spawn(id),
            Err(TaskError::GenerationOverflow)
        );
        assert_eq!(scheduler.entries[2].state, TaskState::Ready);
        assert!(scheduler.queue.contains(id));
        assert_eq!(scheduler.created, 1);
        scheduler.check_invariants().unwrap();
    }

    #[test]
    fn bootstrap_queue_states_are_invariant_valid() {
        let mut worker_run = Scheduler::new();
        let worker = worker_run.spawn().unwrap();
        worker_run.check_invariants().unwrap();
        assert_eq!(worker_run.yield_current(), Ok(Some(worker)));
        assert!(worker_run.is_queued(worker_run.bootstrap_id()));
        worker_run.check_invariants().unwrap();
        assert_eq!(
            worker_run.yield_current(),
            Ok(Some(worker_run.bootstrap_id()))
        );
        worker_run.check_invariants().unwrap();

        let mut probe = Scheduler::new();
        assert_eq!(probe.begin_idle_probe(), Ok(probe.idle_id()));
        assert!(probe.is_queued(probe.bootstrap_id()));
        probe.check_invariants().unwrap();
        assert_eq!(probe.yield_current(), Ok(Some(probe.bootstrap_id())));
        assert!(probe.queue.is_empty());
        probe.check_invariants().unwrap();

        let mut parked = Scheduler::new();
        assert_eq!(parked.park_bootstrap(), Ok(parked.idle_id()));
        parked.check_invariants().unwrap();
    }

    #[test]
    fn prepared_reap_overflow_is_bitwise_non_mutating() {
        let mut scheduler = Scheduler::new();
        scheduler.entries[2] = Entry {
            generation: u32::MAX,
            state: TaskState::Exited,
        };
        let before = scheduler;
        let id = TaskId::new(2, u32::MAX).unwrap();
        assert_eq!(
            scheduler.prepare_reap(id),
            Err(TaskError::GenerationOverflow)
        );
        assert_eq!(scheduler, before);
    }
}
