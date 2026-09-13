//! Bounded, allocation-free process table and lifecycle management.
//!
//! This module implements the deterministic process table policy for FinnOS,
//! tracking process identities (PIDs), parent-child relationships, lifecycle states
//! (`Ready`, `Running`, `Blocked`, `Exited`), and exit statuses.

#![allow(
    clippy::cast_possible_truncation,
    clippy::missing_errors_doc,
    clippy::must_use_candidate,
    clippy::doc_markdown,
    clippy::cast_sign_loss,
    clippy::cast_possible_wrap,
    clippy::unreadable_literal,
    clippy::missing_const_for_fn,
    clippy::manual_let_else,
    clippy::new_without_default
)]

/// Maximum number of concurrent processes tracked by the kernel.
pub const MAX_PROCESSES: usize = 16;

/// Process ID reserved for the userspace init daemon.
pub const INIT_PID: u64 = 1;

/// Process lifecycle states.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProcessState {
    /// Slot is vacant and ready for allocation.
    Unused,
    /// Process is ready to be scheduled.
    Ready,
    /// Process currently has active execution on the CPU.
    Running,
    /// Process is blocked waiting on IPC or I/O.
    Blocked,
    /// Process has terminated and retains exit status until reaped by parent.
    Exited(i64),
}

/// Errors originating from process table operations.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProcessError {
    /// Process with the given PID was not found.
    ProcessNotFound,
    /// Process table has reached maximum capacity.
    TableFull,
    /// Invalid state transition attempted on process.
    InvalidStateTransition,
    /// Caller is not the parent of the specified process.
    NotParent,
    /// Target process is still running.
    StillRunning,
    /// Invalid argument supplied to process operation.
    InvalidArgument,
}

/// Process Control Block (PCB) tracking a single process.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Process {
    /// Unique process identifier.
    pub pid: u64,
    /// PID of parent process (0 for kernel/bootstrap, 1 for init).
    pub parent_pid: u64,
    /// Current lifecycle state.
    pub state: ProcessState,
    /// Human-readable process name.
    pub name: [u8; 32],
    /// Length of process name in bytes.
    pub name_len: usize,
    /// Entry point virtual address in user space.
    pub entry_point: u64,
    /// Initial stack top virtual address.
    pub stack_top: u64,
}

impl Process {
    const UNUSED: Self = Self {
        pid: 0,
        parent_pid: 0,
        state: ProcessState::Unused,
        name: [0u8; 32],
        name_len: 0,
        entry_point: 0,
        stack_top: 0,
    };

    /// Return the process name as a UTF-8 string slice.
    pub fn name_str(&self) -> &str {
        core::str::from_utf8(&self.name[..self.name_len]).unwrap_or("<invalid-name>")
    }
}

/// Bounded process table managing all system processes.
pub struct ProcessTable {
    slots: [Process; MAX_PROCESSES],
    next_pid: u64,
    current_pid: u64,
}

impl ProcessTable {
    /// Initialize an empty process table.
    pub const fn new() -> Self {
        Self {
            slots: [Process::UNUSED; MAX_PROCESSES],
            next_pid: 1,
            current_pid: 0,
        }
    }

    /// Return the currently running process PID.
    pub const fn current_pid(&self) -> u64 {
        self.current_pid
    }

    /// Set the currently running process PID.
    pub fn set_current_pid(&mut self, pid: u64) {
        self.current_pid = pid;
    }

    /// Allocate a new process slot and spawn a process.
    pub fn spawn(
        &mut self,
        name: &str,
        parent_pid: u64,
        entry_point: u64,
        stack_top: u64,
    ) -> Result<u64, ProcessError> {
        let slot_idx = self
            .slots
            .iter()
            .position(|p| p.state == ProcessState::Unused)
            .ok_or(ProcessError::TableFull)?;

        let pid = self.next_pid;
        self.next_pid = self
            .next_pid
            .checked_add(1)
            .ok_or(ProcessError::TableFull)?;

        let mut name_buf = [0u8; 32];
        let bytes = name.as_bytes();
        let copy_len = core::cmp::min(bytes.len(), 32);
        name_buf[..copy_len].copy_from_slice(&bytes[..copy_len]);

        self.slots[slot_idx] = Process {
            pid,
            parent_pid,
            state: ProcessState::Ready,
            name: name_buf,
            name_len: copy_len,
            entry_point,
            stack_top,
        };

        Ok(pid)
    }

    /// Look up a process by PID.
    pub fn get(&self, pid: u64) -> Option<&Process> {
        self.slots
            .iter()
            .find(|p| p.pid == pid && p.state != ProcessState::Unused)
    }

    /// Look up a process by PID mutably.
    pub fn get_mut(&mut self, pid: u64) -> Option<&mut Process> {
        self.slots
            .iter_mut()
            .find(|p| p.pid == pid && p.state != ProcessState::Unused)
    }

    /// Set process state to Running.
    pub fn set_running(&mut self, pid: u64) -> Result<(), ProcessError> {
        let process = self.get_mut(pid).ok_or(ProcessError::ProcessNotFound)?;
        match process.state {
            ProcessState::Ready | ProcessState::Running => {
                process.state = ProcessState::Running;
                self.current_pid = pid;
                Ok(())
            }
            ProcessState::Unused | ProcessState::Blocked | ProcessState::Exited(_) => {
                Err(ProcessError::InvalidStateTransition)
            }
        }
    }

    /// Record process exit with status code.
    pub fn exit(&mut self, pid: u64, code: i64) -> Result<(), ProcessError> {
        let process = self.get_mut(pid).ok_or(ProcessError::ProcessNotFound)?;
        match process.state {
            ProcessState::Running | ProcessState::Ready | ProcessState::Blocked => {
                process.state = ProcessState::Exited(code);
                Ok(())
            }
            ProcessState::Unused | ProcessState::Exited(_) => {
                Err(ProcessError::InvalidStateTransition)
            }
        }
    }

    /// Terminate a process with a signal code.
    pub fn kill(&mut self, pid: u64, sig: i64) -> Result<(), ProcessError> {
        let process = self.get_mut(pid).ok_or(ProcessError::ProcessNotFound)?;
        match process.state {
            ProcessState::Running | ProcessState::Ready | ProcessState::Blocked => {
                process.state = ProcessState::Exited(-sig);
                Ok(())
            }
            ProcessState::Unused | ProcessState::Exited(_) => {
                Err(ProcessError::InvalidStateTransition)
            }
        }
    }

    /// Wait for a child process to exit and reap its status.
    ///
    /// If `target_pid > 0`, waits for that specific child.
    /// If `target_pid == -1` or `0`, waits for any child of `parent_pid`.
    pub fn waitpid(
        &mut self,
        parent_pid: u64,
        target_pid: i64,
    ) -> Result<(u64, i64), ProcessError> {
        if target_pid > 0 {
            let pid = target_pid as u64;
            let slot_idx = self
                .slots
                .iter()
                .position(|p| p.pid == pid && p.state != ProcessState::Unused)
                .ok_or(ProcessError::ProcessNotFound)?;

            let process = &self.slots[slot_idx];
            if process.parent_pid != parent_pid && parent_pid != 0 {
                return Err(ProcessError::NotParent);
            }

            match process.state {
                ProcessState::Exited(code) => {
                    let reaped_pid = process.pid;
                    self.slots[slot_idx] = Process::UNUSED;
                    Ok((reaped_pid, code))
                }
                _ => Err(ProcessError::StillRunning),
            }
        } else {
            // Find any exited child of parent_pid
            let slot_idx = self
                .slots
                .iter()
                .position(|p| {
                    p.state != ProcessState::Unused
                        && (p.parent_pid == parent_pid || parent_pid == 0)
                        && matches!(p.state, ProcessState::Exited(_))
                })
                .ok_or(ProcessError::StillRunning)?;

            let process = &self.slots[slot_idx];
            let reaped_pid = process.pid;
            let code = match process.state {
                ProcessState::Exited(c) => c,
                _ => unreachable!(),
            };
            self.slots[slot_idx] = Process::UNUSED;
            Ok((reaped_pid, code))
        }
    }

    /// Return an iterator over all active processes.
    pub fn iter(&self) -> impl Iterator<Item = &Process> {
        self.slots
            .iter()
            .filter(|p| p.state != ProcessState::Unused)
    }

    /// Return total active process count.
    pub fn count(&self) -> usize {
        self.slots
            .iter()
            .filter(|p| p.state != ProcessState::Unused)
            .count()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spawn_and_get_processes() {
        let mut table = ProcessTable::new();
        let init_pid = table.spawn("init", 0, 0x400000, 0x810000).unwrap();
        assert_eq!(init_pid, 1);
        assert_eq!(table.count(), 1);

        let shell_pid = table.spawn("shell", init_pid, 0x500000, 0x820000).unwrap();
        assert_eq!(shell_pid, 2);
        assert_eq!(table.count(), 2);

        let init = table.get(init_pid).unwrap();
        assert_eq!(init.name_str(), "init");
        assert_eq!(init.parent_pid, 0);
        assert_eq!(init.state, ProcessState::Ready);

        let shell = table.get(shell_pid).unwrap();
        assert_eq!(shell.name_str(), "shell");
        assert_eq!(shell.parent_pid, 1);
    }

    #[test]
    fn process_lifecycle_and_waitpid() {
        let mut table = ProcessTable::new();
        let init_pid = table.spawn("init", 0, 0x400000, 0x810000).unwrap();
        let child_pid = table.spawn("worker", init_pid, 0x600000, 0x830000).unwrap();

        table.set_running(child_pid).unwrap();
        assert_eq!(table.current_pid(), child_pid);

        // Child is still running; waitpid should indicate StillRunning
        assert_eq!(
            table.waitpid(init_pid, child_pid as i64),
            Err(ProcessError::StillRunning)
        );

        // Child exits with status 42
        table.exit(child_pid, 42).unwrap();

        // Parent reaps child
        let (reaped, code) = table.waitpid(init_pid, child_pid as i64).unwrap();
        assert_eq!(reaped, child_pid);
        assert_eq!(code, 42);

        // After reaping, child is gone
        assert_eq!(table.get(child_pid), None);
        assert_eq!(table.count(), 1);
    }

    #[test]
    fn waitpid_any_child() {
        let mut table = ProcessTable::new();
        let init_pid = table.spawn("init", 0, 0x400000, 0x810000).unwrap();
        let c1 = table.spawn("c1", init_pid, 0x1000, 0x2000).unwrap();
        let c2 = table.spawn("c2", init_pid, 0x3000, 0x4000).unwrap();

        table.exit(c2, 100).unwrap();

        let (reaped, code) = table.waitpid(init_pid, -1).unwrap();
        assert_eq!(reaped, c2);
        assert_eq!(code, 100);

        table.exit(c1, 200).unwrap();
        let (reaped2, code2) = table.waitpid(init_pid, 0).unwrap();
        assert_eq!(reaped2, c1);
        assert_eq!(code2, 200);
    }

    #[test]
    fn process_kill_signal() {
        let mut table = ProcessTable::new();
        let p = table.spawn("proc", 1, 0x1000, 0x2000).unwrap();
        table.kill(p, 9).unwrap();

        let proc = table.get(p).unwrap();
        assert_eq!(proc.state, ProcessState::Exited(-9));
    }

    #[test]
    fn capacity_exhaustion() {
        let mut table = ProcessTable::new();
        for i in 0..MAX_PROCESSES {
            let pid = table.spawn("task", 0, 0x1000, 0x2000).unwrap();
            assert_eq!(pid, (i + 1) as u64);
        }
        assert_eq!(
            table.spawn("overflow", 0, 0x1000, 0x2000),
            Err(ProcessError::TableFull)
        );
    }
}
