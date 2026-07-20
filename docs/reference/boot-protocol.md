# Boot protocol reference

> Status: Version-three reference
> Implementation: Shared protocol crate with pure validation tests

Version 3 uses `#[repr(C)]` structures and transports raw UEFI memory-map bytes plus descriptor metadata. The magic is `0x46494E4E4F534249`; the version is `3`. `BootInfo` contains `magic`, `version`, `structure_size`, `flags`, `memory_map`, `framebuffer`, `kernel_image`, `boot_info_storage`, and `rsdp_address`. Versions 1 and 2 are rejected because their weaker storage and absent-resource rules are not wire-compatible with this hardened contract.

Flags are `1 << 0` for a framebuffer, `1 << 1` for a memory map, and `1 << 2` for an ACPI RSDP. Addresses are physical addresses. The loader owns the backing pages and must keep them alive through kernel entry; the kernel must not dereference nested physical addresses until their metadata has been validated.

Validation checks magic, version, exact version-three structure size, known flags, checked required ranges, exact page-aligned 4 KiB `boot_info_storage`, descriptor stride/divisibility/version/reserved fields, absent-resource zeroing, RSDP consistency, and RGB/BGR framebuffer geometry/capacity. `PixelFormat` is an all-bit-pattern-valid transparent `u32` newtype so malformed bytes can be rejected after copying without first creating an invalid Rust enum. The raw Rust layout is not a permanent wire format beyond this explicit `repr(C)` version-three contract; future incompatible changes require a new protocol version.

## Trust and threat assumptions

The supported deployments are the integrated x86-64 QEMU `q35`/OVMF and ARM64 QEMU `virt`/AAVMF paths. UEFI firmware and the FinnOS boot manager are trusted to pass a readable, aligned, mapped `BootInfo` pointer and to retain every named physical range through consumption. The kernel checks null/alignment before an explicitly unsafe read, copies the top-level value, validates it, and requires the pointer to equal the start of its declared 4 KiB page. Pointer provenance and mapping remain loader promises. Nested addresses remain integers until their metadata and protected-range containment are validated; the raw map dereference remains an explicit unsafe boundary under inherited identity mappings.

Version 3 records used memory-map bytes, not the UEFI helper pool's larger allocation including spare descriptor capacity. This is safe in the current model because all LoaderData remains non-usable and the parser finishes before allocation. LoaderData reclamation is forbidden. A future version must add a separate full-storage length before selective reclamation.

The kernel ELF is not trusted for parser safety: the boot manager bounds its file size and validates its class, machine, program-header table, segment sizes, alignment, arithmetic, load-range overlap, and executable entry membership before allocation or transfer. Validation is deterministic and allocation remains capped at 32 loadable segments. The in-memory byte vector is loader-owned, so the validated input cannot change between validation and loading.

This boundary does not provide authenticity or recovery. Secure Boot policy, kernel signatures, measured boot, malicious or compromised firmware, hostile DMA, alternate firmware mappings, and physical attacks are outside the implemented guarantee. The early kernel temporarily depends on inherited identity mappings until it installs FinnOS-owned page tables. These are residual risks, not properties established by protocol versioning.
