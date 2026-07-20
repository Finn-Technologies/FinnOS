# Boot protocol

> Status: Versioned contract
> Implementation: Version 3 `#[repr(C)]` layout defined and used by the UEFI boot manager and both kernels

The boot manager–kernel contract carries firmware information, a raw UEFI memory map, framebuffer information, the loaded kernel image range, the `BootInfo` storage range, and an optional ACPI RSDP address. Version 2 added the `boot_info_storage` field; version 3 makes the entire page-owned handoff and absent-resource encodings exact so the kernel can safely copy before consuming nested resources. Concrete binary layouts and versioning rules are enforced by the `BootInfo` validation function. Versions 1 and 2 are deliberately rejected.
