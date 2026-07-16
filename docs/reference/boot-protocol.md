# Boot protocol reference

> Status: Version-two reference
> Implementation: Shared protocol crate with pure validation tests

Version 2 uses `#[repr(C)]` structures and transports raw UEFI memory-map bytes plus descriptor metadata. The magic is `0x46494E4E4F534249`; the version is `2`. `BootInfo` contains `magic`, `version`, `structure_size`, `flags`, `memory_map`, `framebuffer`, `kernel_image`, `boot_info_storage`, and `rsdp_address`.

Flags are `1 << 0` for a framebuffer, `1 << 1` for a memory map, and `1 << 2` for an ACPI RSDP. Addresses are physical addresses. The loader owns the backing pages and must keep them alive through kernel entry; the kernel must not dereference nested physical addresses until their metadata has been validated.

Validation checks magic, version, exact version-two structure size, map metadata, `boot_info_storage` presence when the memory map is present, RGB/BGR framebuffer consistency, stride, dimensions, and byte capacity. The raw Rust layout is not a permanent wire format beyond this explicit `repr(C)` version-two contract; future incompatible changes require a new protocol version.

## Trust and threat assumptions

The supported deployment is the current x86-64 QEMU `q35`/OVMF path. UEFI firmware and the FinnOS boot manager are trusted to pass an aligned, mapped, immutable `BootInfo` pointer and to own and retain every physical range named by that structure through kernel entry. The kernel rejects a null pointer, but it must dereference a non-null pointer before structural validation; pointer provenance and mapping are therefore loader promises, not properties established by version 2. Nested addresses remain integers while the kernel validates the version, structure size, flags, lengths, descriptor metadata, and arithmetic bounds. Structural validation does not prove that a nested address is mapped, uniquely owned, immutable, or backed by the claimed resource.

The kernel ELF is not trusted for parser safety: the boot manager bounds its file size and validates its class, machine, program-header table, segment sizes, alignment, arithmetic, load-range overlap, and executable entry membership before allocation or transfer. Validation is deterministic and allocation remains capped at 32 loadable segments. The in-memory byte vector is loader-owned, so the validated input cannot change between validation and loading.

This boundary does not provide authenticity or recovery. Secure Boot policy, kernel signatures, measured boot, malicious or compromised firmware, hostile DMA, alternate firmware mappings, and physical attacks are outside the implemented guarantee. The early kernel temporarily depends on inherited identity mappings until it installs FinnOS-owned page tables. These are residual risks, not properties established by protocol versioning.
