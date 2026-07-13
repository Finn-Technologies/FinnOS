#![no_std]
#![deny(missing_docs)]
#![warn(unsafe_code)]

//! Foundational types and metadata for the `FinnOS` kernel.
//!
//! The crate contains the safe early-kernel foundations used by the bootable
//! x86-64 path, including memory-map classification and physical page
//! allocation.

pub mod arch;
pub mod boot_validation;
pub mod framebuffer;
pub mod interrupt;
pub mod memory;

/// The human-readable name of the `FinnOS` kernel.
pub const KERNEL_NAME: &str = "Finn Kernel";

/// Version information for an early `FinnOS` component.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Version {
    /// Major version.
    pub major: u16,
    /// Minor version.
    pub minor: u16,
    /// Patch version.
    pub patch: u16,
}

/// The current scaffold version of the kernel crate.
pub const KERNEL_VERSION: Version = Version {
    major: 0,
    minor: 0,
    patch: 1,
};

#[cfg(test)]
extern crate std;

#[cfg(test)]
mod tests {
    use super::{KERNEL_NAME, KERNEL_VERSION, Version};

    #[test]
    fn kernel_name_is_stable() {
        assert_eq!(KERNEL_NAME, "Finn Kernel");
    }

    #[test]
    fn kernel_version_matches_the_initial_scaffold() {
        assert_eq!(
            KERNEL_VERSION,
            Version {
                major: 0,
                minor: 0,
                patch: 1
            }
        );
    }
}
