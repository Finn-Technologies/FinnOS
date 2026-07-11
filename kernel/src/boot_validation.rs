//! Kernel-side validation and diagnostics for the firmware handoff.

use finn_boot_protocol::{BootInfo, BootInfoError, validate};

/// Validate a boot-information pointer without dereferencing a null pointer.
///
/// # Errors
///
/// Returns a structured protocol error when the pointer is null or its contents fail validation.
#[allow(unsafe_code)]
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn validate_pointer(pointer: *const BootInfo) -> Result<&'static BootInfo, BootInfoError> {
    if pointer.is_null() {
        return Err(BootInfoError::BadMagic);
    }
    // SAFETY: The boot manager promises a page-owned, immutable BootInfo object
    // that remains alive after ExitBootServices. The caller supplies that pointer.
    let info = unsafe { &*pointer };
    validate(info)?;
    Ok(info)
}
