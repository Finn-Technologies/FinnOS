//! Virtual File System (VFS) core and block device abstraction.

#![allow(unsafe_code)]
#![allow(
    clippy::cast_possible_wrap,
    clippy::manual_let_else,
    clippy::cast_possible_truncation
)]

use crate::syscall::{SyscallError, copy_from_user, copy_to_user, validate_user_buffer};
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

/// Block size in bytes (standard sector size).
pub const SECTOR_SIZE: usize = 512;
/// Number of simulated/cached partition sectors (128 sectors = 64 KiB).
pub const PARTITION_SECTORS: usize = 128;

struct PartitionStorage(UnsafeCell<[u8; PARTITION_SECTORS * SECTOR_SIZE]>);
// SAFETY: Single-BSP early kernel; partition accesses are serialized.
unsafe impl Sync for PartitionStorage {}

static PARTITION_STORAGE: PartitionStorage =
    PartitionStorage(UnsafeCell::new([0u8; PARTITION_SECTORS * SECTOR_SIZE]));
static PARTITION_INITIALIZED: AtomicBool = AtomicBool::new(false);

/// Total number of block read operations performed.
pub static BLOCK_READ_COUNT: AtomicU64 = AtomicU64::new(0);
/// Total number of block write operations performed.
pub static BLOCK_WRITE_COUNT: AtomicU64 = AtomicU64::new(0);

fn ensure_partition_initialized() {
    if !PARTITION_INITIALIZED.load(Ordering::Acquire) {
        // SAFETY: Initializing the static partition table header with a signature.
        unsafe {
            let p = &mut *PARTITION_STORAGE.0.get();
            let header = b"FINNOS:DATA_PARTITION_V1";
            p[..header.len()].copy_from_slice(header);
        }
        PARTITION_INITIALIZED.store(true, Ordering::Release);
    }
}

/// Handle a VFS write to an open file descriptor.
pub fn vfs_write(fd: u64, ptr: u64, count: usize) -> i64 {
    if let Err(err) = validate_user_buffer(ptr, count) {
        return err as i64;
    }
    match fd {
        // /dev/null and /dev/zero: discard writes and report full count.
        3 | 4 => count as i64,
        // /data/state: writes to secondary disk sector 1.
        5 => {
            let to_write = core::cmp::min(count, SECTOR_SIZE);
            ensure_partition_initialized();
            let mut buf = [0u8; SECTOR_SIZE];
            // SAFETY: `validate_user_buffer` checked the user pointer.
            if unsafe { copy_from_user(&mut buf[..to_write], ptr).is_err() } {
                return SyscallError::BadAddress as i64;
            }
            // SAFETY: Writing to sector 1 of partition storage.
            unsafe {
                let p = &mut *PARTITION_STORAGE.0.get();
                let dest = &mut p[SECTOR_SIZE..SECTOR_SIZE + to_write];
                dest.copy_from_slice(&buf[..to_write]);
            }
            BLOCK_WRITE_COUNT.fetch_add(1, Ordering::Relaxed);
            to_write as i64
        }
        _ => SyscallError::BadFileDescriptor as i64,
    }
}

/// Handle a VFS read from an open file descriptor.
pub fn vfs_read(fd: u64, ptr: u64, count: usize) -> i64 {
    if let Err(err) = validate_user_buffer(ptr, count) {
        return err as i64;
    }
    match fd {
        // /dev/null: EOF immediately (0 bytes read).
        3 => 0,
        // /dev/zero: stream zeroes.
        4 => {
            let zero = [0u8; 64];
            let mut offset = 0;
            while offset < count {
                let chunk = core::cmp::min(count - offset, zero.len());
                // SAFETY: `validate_user_buffer` checked the user pointer.
                if unsafe { copy_to_user(ptr + offset as u64, &zero[..chunk]).is_err() } {
                    return SyscallError::BadAddress as i64;
                }
                offset += chunk;
            }
            count as i64
        }
        // /data/state: read from secondary disk sector 1.
        5 => {
            let to_read = core::cmp::min(count, SECTOR_SIZE);
            ensure_partition_initialized();
            let mut buf = [0u8; SECTOR_SIZE];
            // SAFETY: Reading from sector 1 of partition storage.
            unsafe {
                let p = &*PARTITION_STORAGE.0.get();
                let src = &p[SECTOR_SIZE..SECTOR_SIZE + to_read];
                buf[..to_read].copy_from_slice(src);
            }
            // SAFETY: Copying verified data to user buffer.
            if unsafe { copy_to_user(ptr, &buf[..to_read]).is_err() } {
                return SyscallError::BadAddress as i64;
            }
            BLOCK_READ_COUNT.fetch_add(1, Ordering::Relaxed);
            to_read as i64
        }
        _ => SyscallError::BadFileDescriptor as i64,
    }
}

/// Read raw sectors from the secondary block device into user memory.
pub fn vfs_block_read(sector: u64, ptr: u64, sector_count: usize) -> i64 {
    let bytes = match sector_count.checked_mul(SECTOR_SIZE) {
        Some(b) => b,
        None => return SyscallError::InvalidArgument as i64,
    };
    if let Err(err) = validate_user_buffer(ptr, bytes) {
        return err as i64;
    }
    let sec = match usize::try_from(sector) {
        Ok(s) => s,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    if sec + sector_count > PARTITION_SECTORS {
        return SyscallError::InvalidArgument as i64;
    }
    ensure_partition_initialized();
    let start_offset = sec * SECTOR_SIZE;
    // SAFETY: Reading from verified partition bounds.
    let src_slice = unsafe {
        let p = &*PARTITION_STORAGE.0.get();
        &p[start_offset..start_offset + bytes]
    };
    // SAFETY: Copy to user buffer.
    if unsafe { copy_to_user(ptr, src_slice).is_err() } {
        return SyscallError::BadAddress as i64;
    }
    BLOCK_READ_COUNT.fetch_add(sector_count as u64, Ordering::Relaxed);
    sector_count as i64
}

/// Write raw sectors from user memory to the secondary block device.
pub fn vfs_block_write(sector: u64, ptr: u64, sector_count: usize) -> i64 {
    let bytes = match sector_count.checked_mul(SECTOR_SIZE) {
        Some(b) => b,
        None => return SyscallError::InvalidArgument as i64,
    };
    if let Err(err) = validate_user_buffer(ptr, bytes) {
        return err as i64;
    }
    let sec = match usize::try_from(sector) {
        Ok(s) => s,
        Err(_) => return SyscallError::InvalidArgument as i64,
    };
    if sec + sector_count > PARTITION_SECTORS {
        return SyscallError::InvalidArgument as i64;
    }
    ensure_partition_initialized();
    let start_offset = sec * SECTOR_SIZE;
    // SAFETY: Copying from user into partition slice.
    let dest_slice = unsafe {
        let p = &mut *PARTITION_STORAGE.0.get();
        &mut p[start_offset..start_offset + bytes]
    };
    if unsafe { copy_from_user(dest_slice, ptr).is_err() } {
        return SyscallError::BadAddress as i64;
    }
    BLOCK_WRITE_COUNT.fetch_add(sector_count as u64, Ordering::Relaxed);
    sector_count as i64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vfs_dev_null_discards_and_reads_eof() {
        assert_eq!(vfs_write(3, crate::syscall::USER_SPACE_START, 50), 50);
        assert_eq!(vfs_read(3, crate::syscall::USER_SPACE_START, 50), 0);
    }

    #[test]
    fn vfs_rejects_bad_fd() {
        assert_eq!(
            vfs_write(99, crate::syscall::USER_SPACE_START, 10),
            SyscallError::BadFileDescriptor as i64
        );
        assert_eq!(
            vfs_read(99, crate::syscall::USER_SPACE_START, 10),
            SyscallError::BadFileDescriptor as i64
        );
    }
}
