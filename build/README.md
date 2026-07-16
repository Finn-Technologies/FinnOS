# Build system

> Status: Foundation metadata
> Implementation: Cargo builds only the foundational Rust crate

`Finnfile.toml` is the top-level declarative build description. The tooling validates its target/profile selection, loads architecture-specific artifact and QEMU details from `build/targets/`, cross-compiles the first-boot kernel and UEFI loader, stages the ESP, constructs the FAT image, and runs QEMU. Future tooling will add signing and broader cross-compilation; current executable target metadata remains limited to x86-64 first boot, while ARM64 is explicitly planned and non-bootable.
