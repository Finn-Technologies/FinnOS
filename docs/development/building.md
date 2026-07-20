# Building

> Status: Current workflow
> Implementation: Builds the host workspace and first-boot artifacts

```bash
./tools/finn doctor
./tools/finn build
cargo build --workspace
```

`build-boot` produces separate UEFI and kernel artifacts; `image` produces the real 64 MiB FAT image used by QEMU. The artifact is a first-boot diagnostic system, not a usable operating-system build.

Build commands accept `--target x86_64-qemu|arm64-qemu` and
`--profile development|release`. The target/profile model is validated from
`Finnfile.toml` and `build/targets/`. ARM64 supports serial first boot, the R4.1
synchronous-exception modes, R4.2 memory-map/allocator mode, R4.3 owned-MMU
mode, and R4.4 pinned-GICv2 self-SGI mode. Generic timer, task, and later x86
integration modes still reject it.
