#![no_std]
#![deny(missing_docs)]
#![allow(
    unsafe_code,
    clippy::must_use_candidate,
    clippy::cast_possible_wrap,
    clippy::empty_loop,
    clippy::bool_to_int_with_if
)]

//! `finn-libsys`: host-testable userspace ABI skeleton for `FinnOS`.
//!
//! This crate mirrors the kernel syscall numbers in
//! `kernel/src/syscall/mod.rs` (`SYS_YIELD`..`SYS_IPC_REPLY_RECV`) and the
//! synchronous IPC operation selectors, plus a total [`SysError::from_raw`]
//! mapping and a bump-allocator descriptor in [`alloc`].
//!
//! Scope: ABI constants and pure host-testable policy only. No architecture
//! assembly, no QEMU wiring, and no global allocator state live here yet.
//! The kernel remains the source of truth for dispatch semantics; the
//! duplicated constants below are pinned by unit tests asserting their exact
//! numeric values.

pub mod alloc;

/// Yield execution to the scheduler.
pub const SYS_YIELD: u64 = 0;
/// Terminate the current user process with an exit status code.
pub const SYS_EXIT: u64 = 1;
/// Write bytes to a file descriptor (1 = stdout, 2 = stderr).
pub const SYS_WRITE: u64 = 2;
/// Read bytes from a file descriptor.
pub const SYS_READ: u64 = 3;
/// Get current process ID.
pub const SYS_GETPID: u64 = 4;
/// Get system uptime / timer tick count.
pub const SYS_UPTIME: u64 = 5;
/// Read sectors from a block device.
pub const SYS_BLOCK_READ: u64 = 6;
/// Write sectors to a block device.
pub const SYS_BLOCK_WRITE: u64 = 7;
/// Create a synchronous IPC channel pair.
pub const SYS_CHANNEL_CREATE: u64 = 8;
/// Stage a synchronous IPC call on the caller endpoint.
pub const SYS_IPC_CALL: u64 = 9;
/// Receive a call, send a reply, or retrieve a reply on an IPC endpoint.
pub const SYS_IPC_REPLY_RECV: u64 = 10;
/// Spawn a new user process from an ELF binary image in memory.
pub const SYS_SPAWN: u64 = 11;
/// Wait for a process to change state or exit.
pub const SYS_WAITPID: u64 = 12;
/// Terminate or signal a process.
pub const SYS_KILL: u64 = 13;
/// Create a shared virtual memory object (VMO).
pub const SYS_VMO_CREATE: u64 = 14;
/// Map a virtual memory object (VMO) into user address space.
pub const SYS_VMO_MAP: u64 = 15;

/// Operation selector for [`SYS_IPC_REPLY_RECV`]: receive a pending call.
pub const IPC_OP_RECV: u64 = 0;
/// Operation selector for [`SYS_IPC_REPLY_RECV`]: reply to a received call.
pub const IPC_OP_REPLY: u64 = 1;
/// Operation selector for [`SYS_IPC_REPLY_RECV`]: retrieve a staged reply.
pub const IPC_OP_TAKE_REPLY: u64 = 2;

/// Userspace view of kernel syscall error status codes.
///
/// Numeric values mirror `kernel::syscall::SyscallError`: `Success` is `0`,
/// and each failure is a distinct negative `i64`. `BadFd` corresponds to the
/// kernel's `BadFileDescriptor`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i64)]
pub enum SysError {
    /// Operation succeeded.
    Success = 0,
    /// Unrecognized syscall number.
    InvalidSyscall = -1,
    /// User pointer is out of range, null, or points to kernel memory.
    BadAddress = -2,
    /// Invalid argument supplied to syscall.
    InvalidArgument = -3,
    /// File descriptor does not exist or is invalid.
    BadFd = -4,
    /// Operation is not permitted.
    PermissionDenied = -5,
    /// Block I/O or device communication failure.
    IoError = -6,
}

/// Userspace result type using [`SysError`] as the error channel.
pub type Result<T> = core::result::Result<T, SysError>;

impl SysError {
    /// Map a raw kernel return value to a [`Result`].
    ///
    /// Non-negative values are success and pass through as `Ok(raw)`,
    /// preserving byte counts and other non-negative payloads. Negative
    /// values map to the matching variant. Unknown negative values map to
    /// [`SysError::InvalidSyscall`] to keep the conversion total; the kernel
    /// only emits `-6..=0`, so callers must not rely on the fallback for
    /// kernel behavior.
    ///
    /// # Errors
    ///
    /// Returns the matching [`SysError`] variant when `raw` is negative.
    pub const fn from_raw(raw: i64) -> Result<i64> {
        match raw {
            v if v >= 0 => Ok(v),
            -1 => Err(Self::InvalidSyscall),
            -2 => Err(Self::BadAddress),
            -3 => Err(Self::InvalidArgument),
            -4 => Err(Self::BadFd),
            -5 => Err(Self::PermissionDenied),
            -6 => Err(Self::IoError),
            _ => Err(Self::InvalidSyscall),
        }
    }

    /// Return the numeric wire value of this error.
    #[must_use]
    pub const fn code(self) -> i64 {
        self as i64
    }
}

/// Execute a raw syscall with up to 6 arguments.
///
/// # Safety
///
/// The caller must ensure that arguments representing pointers point to valid, mapped memory.
#[allow(unsafe_code)]
pub unsafe fn raw_syscall(num: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64) -> i64 {
    #[cfg(target_arch = "x86_64")]
    {
        let ret: i64;
        unsafe {
            core::arch::asm!(
                "syscall",
                inlateout("rax") num as i64 => ret,
                in("rdi") a0,
                in("rsi") a1,
                in("rdx") a2,
                in("r10") a3,
                in("r8") a4,
                in("r9") a5,
                lateout("rcx") _,
                lateout("r11") _,
                options(nostack)
            );
        }
        ret
    }

    #[cfg(target_arch = "aarch64")]
    {
        let ret: i64;
        unsafe {
            core::arch::asm!(
                "svc #0",
                inlateout("x8") num => _,
                inlateout("x0") a0 as i64 => ret,
                in("x1") a1,
                in("x2") a2,
                in("x3") a3,
                in("x4") a4,
                in("x5") a5,
                options(nostack)
            );
        }
        ret
    }

    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
    {
        let _ = (num, a0, a1, a2, a3, a4, a5);
        0
    }
}

/// Yield execution to the scheduler.
///
/// # Errors
///
/// Returns [`SysError`] if the syscall fails.
pub fn yield_now() -> Result<()> {
    SysError::from_raw(unsafe { raw_syscall(SYS_YIELD, 0, 0, 0, 0, 0, 0) }).map(|_| ())
}

/// Terminate the current process with an exit status code.
pub fn exit(status: i64) -> ! {
    #[allow(clippy::cast_sign_loss)]
    unsafe {
        raw_syscall(SYS_EXIT, status as u64, 0, 0, 0, 0, 0);
    }
    loop {}
}

/// Write bytes to a file descriptor.
///
/// # Errors
///
/// Returns [`SysError`] if writing fails.
pub fn write(fd: u64, buf: &[u8]) -> Result<usize> {
    #[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_WRITE,
            fd,
            buf.as_ptr() as u64,
            buf.len() as u64,
            0,
            0,
            0,
        )
    })
    .map(|n| n as usize)
}

/// Print a string slice to standard output (file descriptor 1).
///
/// # Errors
///
/// Returns [`SysError`] if writing fails.
pub fn print(s: &str) -> Result<usize> {
    write(1, s.as_bytes())
}

/// Read bytes from a file descriptor.
///
/// # Errors
///
/// Returns [`SysError`] if reading fails.
pub fn read(fd: u64, buf: &mut [u8]) -> Result<usize> {
    #[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_READ,
            fd,
            buf.as_mut_ptr() as u64,
            buf.len() as u64,
            0,
            0,
            0,
        )
    })
    .map(|n| n as usize)
}

/// Get the current process identifier.
///
/// # Errors
///
/// Returns [`SysError`] if the syscall fails.
pub fn getpid() -> Result<u64> {
    #[allow(clippy::cast_sign_loss)]
    SysError::from_raw(unsafe { raw_syscall(SYS_GETPID, 0, 0, 0, 0, 0, 0) }).map(|n| n as u64)
}

/// Get the system uptime tick count.
///
/// # Errors
///
/// Returns [`SysError`] if the syscall fails.
pub fn uptime() -> Result<u64> {
    #[allow(clippy::cast_sign_loss)]
    SysError::from_raw(unsafe { raw_syscall(SYS_UPTIME, 0, 0, 0, 0, 0, 0) }).map(|n| n as u64)
}

/// Spawn a child process from an in-memory ELF image.
///
/// # Errors
///
/// Returns [`SysError`] if image validation or spawning fails.
pub fn spawn(image: &[u8]) -> Result<u64> {
    #[allow(clippy::cast_sign_loss)]
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_SPAWN,
            image.as_ptr() as u64,
            image.len() as u64,
            0,
            0,
            0,
            0,
        )
    })
    .map(|n| n as u64)
}

/// Wait for a child process to exit.
///
/// # Errors
///
/// Returns [`SysError`] if waiting fails.
pub fn waitpid(pid: u64) -> Result<i64> {
    SysError::from_raw(unsafe { raw_syscall(SYS_WAITPID, pid, 0, 0, 0, 0, 0) })
}

/// Terminate or signal a process.
///
/// # Errors
///
/// Returns [`SysError`] if signalling fails.
pub fn kill(pid: u64, sig: u64) -> Result<()> {
    SysError::from_raw(unsafe { raw_syscall(SYS_KILL, pid, sig, 0, 0, 0, 0) }).map(|_| ())
}

/// Create a synchronous IPC channel pair.
///
/// # Errors
///
/// Returns [`SysError`] if creation fails.
pub fn channel_create() -> Result<(u64, u64)> {
    let mut caller: u64 = 0;
    let mut responder: u64 = 0;
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_CHANNEL_CREATE,
            core::ptr::from_mut(&mut caller) as u64,
            core::ptr::from_mut(&mut responder) as u64,
            0,
            0,
            0,
            0,
        )
    })?;
    Ok((caller, responder))
}

/// Stage a synchronous call on an IPC endpoint.
///
/// # Errors
///
/// Returns [`SysError`] if the call fails.
pub fn ipc_call(endpoint: u64, msg: &[u8]) -> Result<()> {
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_IPC_CALL,
            endpoint,
            msg.as_ptr() as u64,
            msg.len() as u64,
            0,
            0,
            0,
        )
    })
    .map(|_| ())
}

/// Receive a call on an IPC endpoint.
///
/// # Errors
///
/// Returns [`SysError`] if receive fails.
pub fn ipc_recv(endpoint: u64, buf: &mut [u8]) -> Result<usize> {
    #[allow(clippy::cast_possible_truncation, clippy::cast_sign_loss)]
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_IPC_REPLY_RECV,
            endpoint,
            buf.as_mut_ptr() as u64,
            buf.len() as u64,
            IPC_OP_RECV,
            0,
            0,
        )
    })
    .map(|n| n as usize)
}

/// Send a reply on an IPC endpoint.
///
/// # Errors
///
/// Returns [`SysError`] if sending the reply fails.
pub fn ipc_reply(endpoint: u64, reply: &[u8]) -> Result<()> {
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_IPC_REPLY_RECV,
            endpoint,
            reply.as_ptr() as u64,
            reply.len() as u64,
            IPC_OP_REPLY,
            0,
            0,
        )
    })
    .map(|_| ())
}

/// Create a shared virtual memory object (VMO).
///
/// # Errors
///
/// Returns [`SysError`] if creation fails.
pub fn vmo_create(size: u64) -> Result<u64> {
    #[allow(clippy::cast_sign_loss)]
    SysError::from_raw(unsafe { raw_syscall(SYS_VMO_CREATE, size, 0, 0, 0, 0, 0) })
        .map(|n| n as u64)
}

/// Map a virtual memory object into address space.
///
/// # Errors
///
/// Returns [`SysError`] if mapping fails.
pub fn vmo_map(handle: u64, vaddr: u64, writable: bool) -> Result<()> {
    SysError::from_raw(unsafe {
        raw_syscall(
            SYS_VMO_MAP,
            handle,
            vaddr,
            if writable { 1 } else { 0 },
            0,
            0,
            0,
        )
    })
    .map(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn syscall_numbers_match_kernel_abi() {
        // Pinned to kernel/src/syscall/mod.rs SYS_YIELD..SYS_IPC_REPLY_RECV.
        assert_eq!(SYS_YIELD, 0);
        assert_eq!(SYS_EXIT, 1);
        assert_eq!(SYS_WRITE, 2);
        assert_eq!(SYS_READ, 3);
        assert_eq!(SYS_GETPID, 4);
        assert_eq!(SYS_UPTIME, 5);
        assert_eq!(SYS_BLOCK_READ, 6);
        assert_eq!(SYS_BLOCK_WRITE, 7);
        assert_eq!(SYS_CHANNEL_CREATE, 8);
        assert_eq!(SYS_IPC_CALL, 9);
        assert_eq!(SYS_IPC_REPLY_RECV, 10);
        assert_eq!(SYS_SPAWN, 11);
        assert_eq!(SYS_WAITPID, 12);
        assert_eq!(SYS_KILL, 13);
        assert_eq!(SYS_VMO_CREATE, 14);
        assert_eq!(SYS_VMO_MAP, 15);
    }

    #[test]
    fn ipc_op_selectors_match_kernel_abi() {
        assert_eq!(IPC_OP_RECV, 0);
        assert_eq!(IPC_OP_REPLY, 1);
        assert_eq!(IPC_OP_TAKE_REPLY, 2);
    }

    #[test]
    fn error_codes_match_kernel_abi() {
        assert_eq!(SysError::Success.code(), 0);
        assert_eq!(SysError::InvalidSyscall.code(), -1);
        assert_eq!(SysError::BadAddress.code(), -2);
        assert_eq!(SysError::InvalidArgument.code(), -3);
        assert_eq!(SysError::BadFd.code(), -4);
        assert_eq!(SysError::PermissionDenied.code(), -5);
        assert_eq!(SysError::IoError.code(), -6);
    }

    #[test]
    fn from_raw_preserves_success_payloads() {
        assert_eq!(SysError::from_raw(0), Ok(0));
        assert_eq!(SysError::from_raw(1), Ok(1));
        assert_eq!(SysError::from_raw(42), Ok(42));
        assert_eq!(SysError::from_raw(4096), Ok(4096));
        assert_eq!(SysError::from_raw(i64::MAX), Ok(i64::MAX));
    }

    #[test]
    fn from_raw_maps_each_error() {
        assert_eq!(SysError::from_raw(-1), Err(SysError::InvalidSyscall));
        assert_eq!(SysError::from_raw(-2), Err(SysError::BadAddress));
        assert_eq!(SysError::from_raw(-3), Err(SysError::InvalidArgument));
        assert_eq!(SysError::from_raw(-4), Err(SysError::BadFd));
        assert_eq!(SysError::from_raw(-5), Err(SysError::PermissionDenied));
        assert_eq!(SysError::from_raw(-6), Err(SysError::IoError));
    }

    #[test]
    fn from_raw_maps_unknown_negatives_to_invalid_syscall() {
        assert_eq!(SysError::from_raw(-7), Err(SysError::InvalidSyscall));
        assert_eq!(SysError::from_raw(-100), Err(SysError::InvalidSyscall));
        assert_eq!(SysError::from_raw(i64::MIN), Err(SysError::InvalidSyscall));
    }
}
