# Boot protocol reference

> Status: Version-two reference
> Implementation: Shared protocol crate with pure validation tests

Version 2 uses `#[repr(C)]` structures and transports raw UEFI memory-map bytes plus descriptor metadata. The magic is `0x46494E4E4F534249`; the version is `2`. `BootInfo` contains `magic`, `version`, `structure_size`, `flags`, `memory_map`, `framebuffer`, `kernel_image`, `boot_info_storage`, and `rsdp_address`.

Flags are `1 << 0` for a framebuffer, `1 << 1` for a memory map, and `1 << 2` for an ACPI RSDP. Addresses are physical addresses. The loader owns the backing pages and must keep them alive through kernel entry and the kernel must not dereference them until validated.

Validation checks magic, version, exact version-two structure size, map metadata, `boot_info_storage` presence when the memory map is present, RGB/BGR framebuffer consistency, stride, dimensions, and byte capacity. The raw Rust layout is not a permanent wire format beyond this explicit `repr(C)` version-two contract; future incompatible changes require a new protocol version.
