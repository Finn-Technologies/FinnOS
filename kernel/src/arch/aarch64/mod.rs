//! Early `AArch64` facilities for QEMU serial entry and synchronous exceptions.

#![allow(unsafe_code)]

pub mod exceptions;
pub mod gic;
pub mod paging;
pub mod qemu;
pub mod serial;
