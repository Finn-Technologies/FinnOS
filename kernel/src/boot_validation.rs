//! Kernel-side validation and diagnostics for the firmware handoff.

use core::mem::{align_of, size_of};

use finn_boot_protocol::{BOOT_INFO_PAGE_SIZE, BootInfo, BootInfoError, validate};

/// Copy and validate a boot-information structure from its firmware pointer.
///
/// Null and alignment checks happen before the structure is read. Returning a
/// value copy prevents later use from silently re-reading mutable handoff bytes.
///
/// # Safety
///
/// A non-null, aligned `pointer` must identify a readable, initialized
/// `BootInfo`. The containing physical page must remain readable for the
/// duration of this call.
///
/// # Errors
///
/// Returns a structured error when the pointer, structure, or declared storage
/// page violates the version-three protocol.
#[allow(unsafe_code)]
pub unsafe fn validate_pointer(pointer: *const BootInfo) -> Result<BootInfo, BootInfoError> {
    if pointer.is_null() {
        return Err(BootInfoError::NullPointer);
    }
    if !(pointer as usize).is_multiple_of(align_of::<BootInfo>()) {
        return Err(BootInfoError::MisalignedPointer);
    }

    // SAFETY: The caller guarantees that the checked, aligned pointer is
    // readable and points to an initialized BootInfo value.
    let info = unsafe { pointer.read() };
    validate(&info)?;

    if !info
        .boot_info_storage
        .start
        .is_multiple_of(BOOT_INFO_PAGE_SIZE)
        || info.boot_info_storage.byte_len != BOOT_INFO_PAGE_SIZE
    {
        return Err(BootInfoError::InvalidBootInfoStorage);
    }

    let pointer_start = u64::try_from(pointer as usize)
        .map_err(|_| BootInfoError::BootInfoOutsideDeclaredStorage)?;
    let pointer_end = pointer_start
        .checked_add(u64::try_from(size_of::<BootInfo>()).unwrap_or(u64::MAX))
        .ok_or(BootInfoError::BootInfoOutsideDeclaredStorage)?;
    let storage_end = info
        .boot_info_storage
        .start
        .checked_add(info.boot_info_storage.byte_len)
        .ok_or(BootInfoError::InvalidBootInfoStorage)?;
    if pointer_start != info.boot_info_storage.start || pointer_end > storage_end {
        return Err(BootInfoError::BootInfoOutsideDeclaredStorage);
    }

    Ok(info)
}

#[cfg(test)]
#[allow(unsafe_code)]
mod tests {
    use super::*;
    use finn_boot_protocol::PhysicalRange;

    const PADDING_LEN: usize = 4096 - size_of::<BootInfo>();

    #[repr(C, align(4096))]
    struct BootInfoPage {
        info: BootInfo,
        padding: [u8; PADDING_LEN],
    }

    fn page() -> BootInfoPage {
        let mut page = BootInfoPage {
            info: BootInfo::empty(),
            padding: [0; PADDING_LEN],
        };
        page.info.kernel_image = PhysicalRange {
            start: 0x10_0000,
            byte_len: 0x20_0000,
        };
        page
    }

    fn set_storage_to_actual_page(page: &mut BootInfoPage) {
        page.info.boot_info_storage = PhysicalRange {
            start: (&raw const page.info) as u64,
            byte_len: BOOT_INFO_PAGE_SIZE,
        };
    }

    #[test]
    fn copies_a_valid_page_owned_handoff() {
        let mut page = page();
        set_storage_to_actual_page(&mut page);
        // SAFETY: `page.info` is initialized, aligned, readable, and its page
        // remains alive for the call.
        let copy = unsafe { validate_pointer(&raw const page.info) }.unwrap();
        assert_eq!(copy, page.info);
        page.info.version = u32::MAX;
        assert_ne!(copy.version, page.info.version);
    }

    #[test]
    fn rejects_null_without_reading() {
        // SAFETY: A null pointer is rejected before any read.
        assert_eq!(
            unsafe { validate_pointer(core::ptr::null()) },
            Err(BootInfoError::NullPointer)
        );
    }

    #[test]
    fn rejects_misalignment_without_reading() {
        let bytes = [0u8; size_of::<BootInfo>() + align_of::<BootInfo>()];
        let base = bytes.as_ptr() as usize;
        let offset = (1..=align_of::<BootInfo>())
            .find(|offset| !(base + offset).is_multiple_of(align_of::<BootInfo>()))
            .unwrap();
        #[allow(clippy::cast_ptr_alignment)]
        let pointer = bytes.as_ptr().wrapping_add(offset).cast::<BootInfo>();
        // SAFETY: The deliberately misaligned pointer is rejected before read.
        assert_eq!(
            unsafe { validate_pointer(pointer) },
            Err(BootInfoError::MisalignedPointer)
        );
    }

    #[test]
    fn rejects_storage_that_does_not_name_the_pointer_page() {
        let mut page = page();
        set_storage_to_actual_page(&mut page);
        page.info.boot_info_storage.start += BOOT_INFO_PAGE_SIZE;
        // SAFETY: The actual object remains initialized, aligned, and readable.
        assert_eq!(
            unsafe { validate_pointer(&raw const page.info) },
            Err(BootInfoError::BootInfoOutsideDeclaredStorage)
        );
    }

    #[test]
    fn rejects_storage_that_is_not_exactly_one_page() {
        let mut page = page();
        set_storage_to_actual_page(&mut page);
        page.info.boot_info_storage.byte_len = BOOT_INFO_PAGE_SIZE * 2;
        // SAFETY: The actual object remains initialized, aligned, and readable.
        assert_eq!(
            unsafe { validate_pointer(&raw const page.info) },
            Err(BootInfoError::InvalidBootInfoStorage)
        );
    }
}
