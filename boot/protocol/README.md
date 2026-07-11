# Boot protocol

> Status: Versioned contract
> Implementation: Version 2 `#[repr(C)]` layout defined and used by x86-64 UEFI boot manager and kernel

The boot manager–kernel contract carries firmware information, a raw UEFI memory map, framebuffer information, the loaded kernel image range, the `BootInfo` storage range, and an optional ACPI RSDP address. Version 2 added the `boot_info_storage` field so the kernel can reserve the handoff structure itself. Concrete binary layouts and versioning rules are enforced by the `BootInfo` validation function.
