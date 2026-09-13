//! Early `AArch64` facilities for QEMU serial entry and synchronous exceptions.

#![allow(unsafe_code)]

pub mod context;
pub mod exceptions;
pub mod gic;
pub mod paging;
pub mod pci;
pub mod qemu;
pub mod scheduler;
pub mod serial;
pub mod syscall;
pub mod task_stack;
pub mod timer;
