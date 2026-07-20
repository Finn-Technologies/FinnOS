# Build system

> Status: Foundation metadata
> Implementation: Cargo builds only the foundational Rust crate

`Finnfile.toml` is the top-level declarative build description. The tooling validates its target/profile selection, loads architecture-specific artifact and QEMU details from `build/targets/`, cross-compiles the first-boot kernel and UEFI loader, stages the ESP, constructs the FAT image, and runs QEMU. Future tooling will add signing and broader cross-compilation. The x86-64 path runs the current kernel foundation; ARM64 supports serial first boot, isolated R4.1 resumable/fatal exception tests, and the R4.2 memory-map/allocator test.
