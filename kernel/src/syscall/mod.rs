//! Shared syscall numbers, memory safety validators, and dispatch logic.

#![allow(
    clippy::cast_possible_wrap,
    clippy::manual_let_else,
    clippy::cast_possible_truncation
)]
#![allow(unsafe_code)]

use core::sync::atomic::{AtomicBool, AtomicI64, Ordering};

/// Create a synchronous IPC channel pair.
pub const SYS_CHANNEL_CREATE: u64 = 8;
/// Stage a synchronous IPC call on the caller endpoint.
pub const SYS_IPC_CALL: u64 = 9;
/// Receive a call, send a reply, or retrieve a reply on an IPC endpoint.
pub const SYS_IPC_REPLY_RECV: u64 = 10;

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

/// Canonical start of user virtual address space (null page and low memory are unmapped).
pub const USER_SPACE_START: u64 = 0x0000_0000_0001_0000;
/// Canonical ceiling of user virtual address space (higher half is reserved for supervisor).
pub const USER_SPACE_LIMIT: u64 = 0x0000_7FFF_FFFF_0000;

/// Global recording of the last observed userspace exit code.
pub static LAST_EXIT_CODE: AtomicI64 = AtomicI64::new(-1);

/// Operation selector for [`SYS_IPC_REPLY_RECV`]: receive a pending call.
pub const IPC_OP_RECV: u64 = 0;
/// Operation selector for [`SYS_IPC_REPLY_RECV`]: reply to a received call.
pub const IPC_OP_REPLY: u64 = 1;
/// Operation selector for [`SYS_IPC_REPLY_RECV`]: retrieve a staged reply.
pub const IPC_OP_TAKE_REPLY: u64 = 2;

struct LockedChannelTable {
    locked: AtomicBool,
    inner: core::cell::UnsafeCell<crate::ipc::ChannelTable>,
}

// SAFETY: BSP-only early kernel; IPC table accesses are serialized by `locked`.
unsafe impl Sync for LockedChannelTable {}

impl LockedChannelTable {
    const fn new() -> Self {
        Self {
            locked: AtomicBool::new(false),
            inner: core::cell::UnsafeCell::new(crate::ipc::ChannelTable::new()),
        }
    }

    fn with<T>(&self, op: impl FnOnce(&mut crate::ipc::ChannelTable) -> T) -> T {
        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
        // SAFETY: Acquire exchange grants exclusive access until release below.
        let result = unsafe { op(&mut *self.inner.get()) };
        self.locked.store(false, Ordering::Release);
        result
    }
}

static IPC_TABLE: LockedChannelTable = LockedChannelTable::new();

/// Map an IPC policy error to a syscall error status.
const fn map_ipc_error(err: crate::ipc::IpcError) -> SyscallError {
    match err {
        crate::ipc::IpcError::InvalidChannel => SyscallError::BadFileDescriptor,
        crate::ipc::IpcError::MessageTooLarge
        | crate::ipc::IpcError::TooManyHandles
        | crate::ipc::IpcError::NoPendingCall
        | crate::ipc::IpcError::AlreadyPending => SyscallError::InvalidArgument,
        crate::ipc::IpcError::RightsMismatch => SyscallError::PermissionDenied,
        crate::ipc::IpcError::PeerClosed | crate::ipc::IpcError::InvalidHandle => {
            SyscallError::IoError
        }
    }
}

/// Thread-safe wrapper around [`crate::process::ProcessTable`] with spinlock synchronization.
pub struct LockedProcessTable {
    locked: AtomicBool,
    inner: core::cell::UnsafeCell<crate::process::ProcessTable>,
}

// SAFETY: BSP-only early kernel; ProcessTable accesses are serialized by `locked`.
unsafe impl Sync for LockedProcessTable {}

impl LockedProcessTable {
    const fn new() -> Self {
        Self {
            locked: AtomicBool::new(false),
            inner: core::cell::UnsafeCell::new(crate::process::ProcessTable::new()),
        }
    }

    /// Access the underlying process table exclusively.
    pub fn with<T>(&self, op: impl FnOnce(&mut crate::process::ProcessTable) -> T) -> T {
        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
        // SAFETY: Acquire exchange grants exclusive access until release below.
        let result = unsafe { op(&mut *self.inner.get()) };
        self.locked.store(false, Ordering::Release);
        result
    }
}

/// Global process table tracking active system processes.
pub static PROCESS_TABLE: LockedProcessTable = LockedProcessTable::new();

/// Map a process error to a syscall error status.
const fn map_process_error(err: crate::process::ProcessError) -> SyscallError {
    match err {
        crate::process::ProcessError::ProcessNotFound => SyscallError::BadFileDescriptor,
        crate::process::ProcessError::TableFull => SyscallError::IoError,
        crate::process::ProcessError::InvalidStateTransition
        | crate::process::ProcessError::InvalidArgument => SyscallError::InvalidArgument,
        crate::process::ProcessError::NotParent => SyscallError::PermissionDenied,
        crate::process::ProcessError::StillRunning => SyscallError::Success,
    }
}

/// Standard syscall error status codes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i64)]
pub enum SyscallError {
    /// Operation succeeded.
    Success = 0,
    /// Unrecognized syscall number.
    InvalidSyscall = -1,
    /// User pointer is out of range, null, or points to kernel memory.
    BadAddress = -2,
    /// Invalid argument supplied to syscall.
    InvalidArgument = -3,
    /// File descriptor does not exist or is invalid.
    BadFileDescriptor = -4,
    /// Operation is not permitted.
    PermissionDenied = -5,
    /// Block I/O or device communication failure.
    IoError = -6,
}

/// Validate that a user buffer `[ptr, ptr + len)` is strictly contained within
/// the allowed user virtual address range and does not wrap or touch kernel memory.
///
/// # Errors
///
/// Returns `SyscallError::BadAddress` if the range is invalid or out of bounds.
pub fn validate_user_buffer(ptr: u64, len: usize) -> Result<(), SyscallError> {
    if len == 0 {
        return Ok(());
    }
    if ptr < USER_SPACE_START {
        return Err(SyscallError::BadAddress);
    }
    let len_u64 = u64::try_from(len).map_err(|_| SyscallError::BadAddress)?;
    let end = ptr.checked_add(len_u64).ok_or(SyscallError::BadAddress)?;
    if end > USER_SPACE_LIMIT {
        return Err(SyscallError::BadAddress);
    }
    Ok(())
}

/// Safely copy a slice of bytes from user memory into kernel memory.
///
/// # Errors
///
/// Returns `SyscallError::BadAddress` if the range is outside canonical user space.
///
/// # Safety
///
/// The caller must ensure that the user memory is mapped and readable.
#[allow(unsafe_code)]
pub unsafe fn copy_from_user(dest: &mut [u8], src_user_ptr: u64) -> Result<(), SyscallError> {
    validate_user_buffer(src_user_ptr, dest.len())?;
    // SAFETY: The range has been verified to be within canonical user space bounds.
    unsafe {
        core::ptr::copy_nonoverlapping(src_user_ptr as *const u8, dest.as_mut_ptr(), dest.len());
    }
    Ok(())
}

/// Safely copy a slice of bytes from kernel memory into user memory.
///
/// # Errors
///
/// Returns `SyscallError::BadAddress` if the range is outside canonical user space.
///
/// # Safety
///
/// The caller must ensure that the user memory is mapped and writable.
#[allow(unsafe_code)]
pub unsafe fn copy_to_user(dest_user_ptr: u64, src: &[u8]) -> Result<(), SyscallError> {
    validate_user_buffer(dest_user_ptr, src.len())?;
    // SAFETY: The range has been verified to be within canonical user space bounds.
    unsafe {
        core::ptr::copy_nonoverlapping(src.as_ptr(), dest_user_ptr as *mut u8, src.len());
    }
    Ok(())
}

/// Dispatch and handle a syscall invoked from user mode (Ring 3 or EL0).
///
/// Returns an integer status code (>= 0 on success, negative `SyscallError` on failure).
#[allow(unsafe_code)]
pub fn dispatch(
    num: u64,
    arg0: u64,
    arg1: u64,
    arg2: u64,
    arg3: u64,
    _arg4: u64,
    _arg5: u64,
) -> i64 {
    match num {
        SYS_YIELD => 0,
        SYS_EXIT => {
            let status = arg0 as i64;
            LAST_EXIT_CODE.store(status, Ordering::Release);
            emit_exit_marker(arg0);
            #[cfg(any(
                feature = "qemu-test-userspace",
                feature = "qemu-test-elf-loader",
                feature = "qemu-test-init",
                feature = "qemu-test-desktop"
            ))]
            {
                emit_pass_marker_and_exit(arg0);
            }
            #[cfg(not(any(
                feature = "qemu-test-userspace",
                feature = "qemu-test-elf-loader",
                feature = "qemu-test-init",
                feature = "qemu-test-desktop"
            )))]
            status
        }
        SYS_WRITE => {
            let fd = arg0;
            let ptr = arg1;
            let count = match usize::try_from(arg2) {
                Ok(c) => c,
                Err(_) => return SyscallError::InvalidArgument as i64,
            };
            if fd == 1 || fd == 2 {
                if let Err(err) = validate_user_buffer(ptr, count) {
                    return err as i64;
                }
                // Stream chunks safely through stack buffer to console output.
                let mut offset = 0;
                let mut chunk = [0u8; 128];
                while offset < count {
                    let to_read = core::cmp::min(count - offset, chunk.len());
                    // SAFETY: `validate_user_buffer` verified the range within user space.
                    unsafe {
                        let src = (ptr + offset as u64) as *const u8;
                        core::ptr::copy_nonoverlapping(src, chunk.as_mut_ptr(), to_read);
                    }
                    write_console_bytes(&chunk[..to_read]);
                    offset += to_read;
                }
                count as i64
            } else {
                crate::fs::vfs::vfs_write(fd, ptr, count)
            }
        }
        SYS_READ => {
            let fd = arg0;
            let ptr = arg1;
            let count = match usize::try_from(arg2) {
                Ok(c) => c,
                Err(_) => return SyscallError::InvalidArgument as i64,
            };
            crate::fs::vfs::vfs_read(fd, ptr, count)
        }
        SYS_GETPID => {
            let pid = PROCESS_TABLE.with(|t| t.current_pid());
            if pid == 0 { 1 } else { pid as i64 }
        }
        SYS_UPTIME => get_uptime_ticks() as i64,
        SYS_BLOCK_READ => {
            let sector = arg0;
            let ptr = arg1;
            let sector_count = match usize::try_from(arg2) {
                Ok(c) => c,
                Err(_) => return SyscallError::InvalidArgument as i64,
            };
            crate::fs::vfs::vfs_block_read(sector, ptr, sector_count)
        }
        SYS_BLOCK_WRITE => {
            let sector = arg0;
            let ptr = arg1;
            let sector_count = match usize::try_from(arg2) {
                Ok(c) => c,
                Err(_) => return SyscallError::InvalidArgument as i64,
            };
            crate::fs::vfs::vfs_block_write(sector, ptr, sector_count)
        }
        SYS_CHANNEL_CREATE => sys_channel_create(arg0, arg1),
        SYS_IPC_CALL => sys_ipc_call(arg0, arg1, arg2),
        SYS_IPC_REPLY_RECV => sys_ipc_reply_recv(arg0, arg1, arg2, arg3),
        SYS_SPAWN => sys_spawn(arg0, arg1),
        SYS_WAITPID => sys_waitpid(arg0, arg1),
        SYS_KILL => sys_kill(arg0, arg1),
        SYS_VMO_CREATE => sys_vmo_create(arg0),
        SYS_VMO_MAP => sys_vmo_map(arg0, arg1, arg2),
        _ => SyscallError::InvalidSyscall as i64,
    }
}

/// Validate and stage a user process spawn from an in-memory ELF image.
#[allow(unsafe_code)]
fn sys_spawn(image_ptr: u64, image_len: u64) -> i64 {
    let len = match usize::try_from(image_len) {
        Ok(l) => l,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    if len == 0 || len > 16 * 1024 * 1024 {
        return SyscallError::InvalidArgument as i64;
    }
    if validate_user_buffer(image_ptr, len).is_err() {
        return SyscallError::BadAddress as i64;
    }
    // SAFETY: validate_user_buffer verified bounds and canonical user address.
    let bytes = unsafe { core::slice::from_raw_parts(image_ptr as *const u8, len) };
    let validated = match crate::loader::validate_elf(bytes) {
        Ok(v) => v,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    let current_pid = PROCESS_TABLE.with(|t| t.current_pid());
    let parent = if current_pid == 0 { 1 } else { current_pid };
    match PROCESS_TABLE
        .with(|t| t.spawn("user_proc", parent, validated.entry, 0x0000_0000_0081_0000))
    {
        Ok(pid) => pid as i64,
        Err(err) => map_process_error(err) as i64,
    }
}

fn sys_waitpid(pid: u64, _options: u64) -> i64 {
    let current_pid = PROCESS_TABLE.with(|t| t.current_pid());
    let parent = if current_pid == 0 { 1 } else { current_pid };
    match PROCESS_TABLE.with(|t| t.waitpid(parent, pid as i64)) {
        Ok((_reaped_pid, code)) => code,
        Err(crate::process::ProcessError::StillRunning) => 0,
        Err(err) => map_process_error(err) as i64,
    }
}

fn sys_kill(pid: u64, sig: u64) -> i64 {
    match PROCESS_TABLE.with(|t| t.kill(pid, sig as i64)) {
        Ok(()) => 0,
        Err(err) => map_process_error(err) as i64,
    }
}

const fn sys_vmo_create(size: u64) -> i64 {
    if size == 0 || size > 64 * 1024 * 1024 {
        return SyscallError::InvalidArgument as i64;
    }
    1
}

#[allow(clippy::missing_const_for_fn, clippy::manual_range_contains)]
fn sys_vmo_map(_handle: u64, vaddr: u64, _writable: u64) -> i64 {
    if vaddr < USER_SPACE_START || vaddr >= USER_SPACE_LIMIT {
        return SyscallError::BadAddress as i64;
    }
    0
}

/// Create a channel pair and write the two endpoint IDs to user memory.
///
/// `caller_out` and `responder_out` each name an 8-byte user buffer.
#[allow(unsafe_code)]
fn sys_channel_create(caller_out: u64, responder_out: u64) -> i64 {
    if validate_user_buffer(caller_out, 8).is_err()
        || validate_user_buffer(responder_out, 8).is_err()
    {
        return SyscallError::BadAddress as i64;
    }
    let ids = IPC_TABLE.with(crate::ipc::ChannelTable::create_channel);
    let (caller, responder) = match ids {
        Ok(pair) => pair,
        Err(err) => return map_ipc_error(err) as i64,
    };
    // SAFETY: Both ranges were validated as 8-byte user buffers above.
    unsafe {
        core::ptr::write_unaligned(caller_out as *mut u64, u64::from(caller));
        core::ptr::write_unaligned(responder_out as *mut u64, u64::from(responder));
    }
    0
}

/// Stage a synchronous call from user memory onto a caller endpoint.
#[allow(unsafe_code)]
fn sys_ipc_call(endpoint: u64, msg_ptr: u64, msg_len: u64) -> i64 {
    let count = match usize::try_from(msg_len) {
        Ok(c) => c,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    if count > crate::ipc::MAX_MESSAGE_BYTES {
        return SyscallError::InvalidArgument as i64;
    }
    if validate_user_buffer(msg_ptr, count).is_err() {
        return SyscallError::BadAddress as i64;
    }
    let endpoint_id = match u32::try_from(endpoint) {
        Ok(id) => id,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    let mut staged = [0u8; crate::ipc::MAX_MESSAGE_BYTES];
    // SAFETY: `validate_user_buffer` verified the source range.
    if count > 0 {
        unsafe {
            core::ptr::copy_nonoverlapping(msg_ptr as *const u8, staged.as_mut_ptr(), count);
        }
    }
    match IPC_TABLE.with(|table| table.call(endpoint_id, &staged[..count], &[])) {
        Ok(()) => 0,
        Err(err) => map_ipc_error(err) as i64,
    }
}

/// Multiplexed receive/reply/take-reply on an endpoint.
///
/// `op` selects [`IPC_OP_RECV`], [`IPC_OP_REPLY`], or [`IPC_OP_TAKE_REPLY`].
/// Returns staged byte count on success, negative [`SyscallError`] otherwise.
#[allow(unsafe_code)]
fn sys_ipc_reply_recv(endpoint: u64, buf_ptr: u64, buf_len: u64, op: u64) -> i64 {
    let capacity = match usize::try_from(buf_len) {
        Ok(c) => c,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    if capacity > crate::ipc::MAX_MESSAGE_BYTES {
        return SyscallError::InvalidArgument as i64;
    }
    if validate_user_buffer(buf_ptr, capacity).is_err() {
        return SyscallError::BadAddress as i64;
    }
    let endpoint_id = match u32::try_from(endpoint) {
        Ok(id) => id,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    match op {
        IPC_OP_RECV => {
            let mut staged = [0u8; crate::ipc::MAX_MESSAGE_BYTES];
            let mut handles = [0u32; crate::ipc::MAX_TRANSFERRED_HANDLES];
            let (msg_len, _handles) = match IPC_TABLE.with(|table| {
                let out_len = core::cmp::min(capacity, staged.len());
                table.recv(endpoint_id, &mut staged[..out_len], &mut handles)
            }) {
                Ok(v) => v,
                Err(err) => return map_ipc_error(err) as i64,
            };
            // SAFETY: Destination range was validated above.
            if msg_len > 0 {
                unsafe {
                    core::ptr::copy_nonoverlapping(staged.as_ptr(), buf_ptr as *mut u8, msg_len);
                }
            }
            msg_len as i64
        }
        IPC_OP_REPLY => {
            let mut staged = [0u8; crate::ipc::MAX_MESSAGE_BYTES];
            // SAFETY: Source range was validated above.
            if capacity > 0 {
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        buf_ptr as *const u8,
                        staged.as_mut_ptr(),
                        capacity,
                    );
                }
            }
            match IPC_TABLE.with(|table| table.reply(endpoint_id, &staged[..capacity])) {
                Ok(()) => 0,
                Err(err) => map_ipc_error(err) as i64,
            }
        }
        IPC_OP_TAKE_REPLY => {
            let mut staged = [0u8; crate::ipc::MAX_MESSAGE_BYTES];
            let out_len = core::cmp::min(capacity, staged.len());
            let reply_len = match IPC_TABLE
                .with(|table| table.take_reply(endpoint_id, &mut staged[..out_len]))
            {
                Ok(n) => n,
                Err(err) => return map_ipc_error(err) as i64,
            };
            // SAFETY: Destination range was validated above.
            if reply_len > 0 {
                unsafe {
                    core::ptr::copy_nonoverlapping(staged.as_ptr(), buf_ptr as *mut u8, reply_len);
                }
            }
            reply_len as i64
        }
        _ => SyscallError::InvalidArgument as i64,
    }
}

#[allow(clippy::missing_const_for_fn)]
fn write_console_bytes(bytes: &[u8]) {
    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    crate::arch::x86_64::serial::write_bytes(bytes);

    #[cfg(all(target_arch = "aarch64", target_os = "none"))]
    crate::arch::aarch64::serial::write_bytes(bytes);

    #[cfg(not(target_os = "none"))]
    {
        let _ = bytes;
    }
}

#[allow(clippy::missing_const_for_fn)]
fn emit_exit_marker(status: u64) {
    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    crate::arch::x86_64::serial::log(core::format_args!("FINNOS:USER:EXIT status=0x{status:x}\n"));

    #[cfg(all(target_arch = "aarch64", target_os = "none"))]
    crate::arch::aarch64::serial::hex_line("FINNOS:USER:EXIT status=0x", status);

    #[cfg(not(target_os = "none"))]
    {
        let _ = status;
    }
}

#[cfg(any(
    feature = "qemu-test-userspace",
    feature = "qemu-test-elf-loader",
    feature = "qemu-test-init",
    feature = "qemu-test-desktop"
))]
fn emit_pass_marker_and_exit(status: u64) -> ! {
    #[cfg(feature = "qemu-test-userspace")]
    let marker = "FINNOS:USER:PASS\n";
    #[cfg(feature = "qemu-test-elf-loader")]
    let marker = "FINNOS:TEST:ELF_LOADER:PASS\n";
    #[cfg(feature = "qemu-test-init")]
    let marker = "FINNOS:TEST:INIT:PASS\n";
    #[cfg(feature = "qemu-test-desktop")]
    let marker = "FINNOS:TEST:DESKTOP:PASS\n";

    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    {
        crate::arch::x86_64::serial::log(core::format_args!("{}", marker));
        if status == 0 {
            crate::arch::x86_64::qemu::exit(0x10);
        } else {
            crate::arch::x86_64::qemu::exit(0x11);
        }
    }

    #[cfg(all(target_arch = "aarch64", target_os = "none"))]
    {
        crate::arch::aarch64::serial::line(marker);
        if status == 0 {
            crate::arch::aarch64::qemu::success();
        } else {
            crate::arch::aarch64::qemu::failure();
        }
    }

    #[cfg(not(target_os = "none"))]
    {
        let _ = status;
        let _ = marker;
        loop {}
    }
}

#[allow(clippy::missing_const_for_fn)]
fn get_uptime_ticks() -> u64 {
    #[cfg(all(target_arch = "x86_64", target_os = "none"))]
    return crate::arch::x86_64::timer::ticks();

    #[cfg(all(target_arch = "aarch64", target_os = "none"))]
    return crate::arch::aarch64::timer::ticks();

    #[cfg(not(target_os = "none"))]
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_user_buffer_rejects_null_and_low_memory() {
        assert_eq!(validate_user_buffer(0, 10), Err(SyscallError::BadAddress));
        assert_eq!(
            validate_user_buffer(0x1000, 10),
            Err(SyscallError::BadAddress)
        );
        assert_eq!(
            validate_user_buffer(USER_SPACE_START - 1, 1),
            Err(SyscallError::BadAddress)
        );
    }

    #[test]
    fn validate_user_buffer_rejects_kernel_addresses_and_overflow() {
        assert_eq!(
            validate_user_buffer(0xffff_8000_0000_0000, 10),
            Err(SyscallError::BadAddress)
        );
        assert_eq!(
            validate_user_buffer(USER_SPACE_LIMIT, 1),
            Err(SyscallError::BadAddress)
        );
        assert_eq!(
            validate_user_buffer(u64::MAX - 5, 10),
            Err(SyscallError::BadAddress)
        );
    }

    #[test]
    fn validate_user_buffer_accepts_valid_ranges() {
        assert_eq!(validate_user_buffer(USER_SPACE_START, 10), Ok(()));
        assert_eq!(validate_user_buffer(0x0000_0040_0000, 4096), Ok(()));
        assert_eq!(validate_user_buffer(USER_SPACE_LIMIT - 100, 100), Ok(()));
    }

    #[test]
    fn invalid_syscall_number_returns_error() {
        assert_eq!(
            dispatch(999, 0, 0, 0, 0, 0, 0),
            SyscallError::InvalidSyscall as i64
        );
    }

    #[test]
    fn exit_records_status() {
        assert_eq!(dispatch(SYS_EXIT, 42, 0, 0, 0, 0, 0), 42);
        assert_eq!(LAST_EXIT_CODE.load(Ordering::Acquire), 42);
    }

    #[test]
    fn ipc_syscall_numbers_are_stable() {
        assert_eq!(SYS_CHANNEL_CREATE, 8);
        assert_eq!(SYS_IPC_CALL, 9);
        assert_eq!(SYS_IPC_REPLY_RECV, 10);
        assert_eq!(IPC_OP_RECV, 0);
        assert_eq!(IPC_OP_REPLY, 1);
        assert_eq!(IPC_OP_TAKE_REPLY, 2);
    }

    #[test]
    fn channel_create_rejects_bad_user_address() {
        assert_eq!(
            dispatch(SYS_CHANNEL_CREATE, 0, USER_SPACE_START, 0, 0, 0, 0),
            SyscallError::BadAddress as i64
        );
        assert_eq!(
            dispatch(
                SYS_CHANNEL_CREATE,
                USER_SPACE_START,
                0xffff_8000_0000_0000,
                0,
                0,
                0,
                0
            ),
            SyscallError::BadAddress as i64
        );
    }

    #[test]
    fn ipc_call_rejects_oversize_and_bad_address() {
        assert_eq!(
            dispatch(
                SYS_IPC_CALL,
                1,
                USER_SPACE_START,
                crate::ipc::MAX_MESSAGE_BYTES as u64 + 1,
                0,
                0,
                0
            ),
            SyscallError::InvalidArgument as i64
        );
        assert_eq!(
            dispatch(SYS_IPC_CALL, 1, 0, 10, 0, 0, 0),
            SyscallError::BadAddress as i64
        );
    }

    #[test]
    fn ipc_call_unknown_endpoint_maps_to_bad_fd() {
        assert_eq!(
            dispatch(SYS_IPC_CALL, 0xFFFF_FFFF, USER_SPACE_START, 0, 0, 0, 0),
            SyscallError::BadFileDescriptor as i64
        );
    }

    #[test]
    fn ipc_reply_recv_rejects_bad_op_and_address() {
        assert_eq!(
            dispatch(SYS_IPC_REPLY_RECV, 1, USER_SPACE_START, 8, 99, 0, 0),
            SyscallError::InvalidArgument as i64
        );
        assert_eq!(
            dispatch(SYS_IPC_REPLY_RECV, 1, 0, 8, IPC_OP_RECV, 0, 0),
            SyscallError::BadAddress as i64
        );
        assert_eq!(
            dispatch(
                SYS_IPC_REPLY_RECV,
                1,
                USER_SPACE_START,
                crate::ipc::MAX_MESSAGE_BYTES as u64 + 1,
                IPC_OP_RECV,
                0,
                0
            ),
            SyscallError::InvalidArgument as i64
        );
    }

    #[test]
    fn ipc_table_roundtrip_through_kernel_table() {
        let (caller, responder) = IPC_TABLE
            .with(crate::ipc::ChannelTable::create_channel)
            .expect("channel");
        IPC_TABLE
            .with(|table| table.call(caller, b"ping", &[]))
            .expect("call");
        let mut msg = [0u8; 16];
        let mut handles = [0u32; 4];
        let (n, h) = IPC_TABLE
            .with(|table| table.recv(responder, &mut msg, &mut handles))
            .expect("recv");
        assert_eq!(n, 4);
        assert_eq!(h, 0);
        assert_eq!(&msg[..n], b"ping");
        IPC_TABLE
            .with(|table| table.reply(responder, b"pong"))
            .expect("reply");
        let mut out = [0u8; 16];
        let m = IPC_TABLE
            .with(|table| table.take_reply(caller, &mut out))
            .expect("take");
        assert_eq!(&out[..m], b"pong");
        IPC_TABLE.with(|table| table.close(caller)).expect("close");
        IPC_TABLE
            .with(|table| table.close(responder))
            .expect("close");
    }

    #[test]
    fn ipc_error_mapping_is_documented() {
        assert_eq!(
            map_ipc_error(crate::ipc::IpcError::InvalidChannel),
            SyscallError::BadFileDescriptor
        );
        assert_eq!(
            map_ipc_error(crate::ipc::IpcError::RightsMismatch),
            SyscallError::PermissionDenied
        );
        assert_eq!(
            map_ipc_error(crate::ipc::IpcError::AlreadyPending),
            SyscallError::InvalidArgument
        );
        assert_eq!(
            map_ipc_error(crate::ipc::IpcError::PeerClosed),
            SyscallError::IoError
        );
    }
}
