# FinnOS

## What is FinnOS?

FinnOS is an original operating system project. It is not a Linux distribution and is not UNIX-like by design. Its long-term architecture is capability-based and intended for x86-64 and ARM64 computers, tablets, and phones.

## What is Peony?

Peony is the native user-interface and application platform: Peony Display, Peony Shell, Peony Framework, rendering, input, accessibility, and adaptive environments.

## Project status

FinnOS now boots a separate kernel in x86-64 QEMU through UEFI. The boot manager loads and validates the kernel ELF, hands off the memory map and GOP framebuffer, and the kernel installs its own GDT, TSS, IDT, and exception handlers before completing First Boot. There is no usable desktop, mobile environment, or application runtime yet.

## Intended platforms

x86-64 QEMU is the intended first boot target; ARM64 QEMU is the intended second architecture target. Real hardware and phone support are long-term goals.

## Architectural direction

The project is pursuing a hybrid microkernel direction with typed IPC and explicit capabilities. Compatibility environments may be added later, but will not define native FinnOS architecture.

## Repository status

FinnOS currently builds a bootable x86-64 UEFI image for QEMU. Automated tests verify the boot manager, separate kernel loading, framebuffer handoff, exception handling, FinnOS-owned page-table activation, UEFI memory-map classification, and early physical page allocation. FinnOS does not yet have a kernel heap, user space, drivers, or Peony.

## Building the current scaffold

```bash
./tools/finn doctor
./tools/finn build
./tools/finn test
./tools/finn check
./tools/finn build-boot
./tools/finn image
./tools/finn run
./tools/finn test-boot
./tools/finn test-exceptions
./tools/finn test-memory-map
./tools/finn check-all
```

## Repository layout

The current layout is documented in [the repository layout guide](docs/development/repository-layout.md).

## Documentation

Start with [the documentation index](docs/README.md), [architecture](docs/architecture/README.md), and [building](docs/development/building.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [ROADMAP.md](ROADMAP.md).

## Security

FinnOS is experimental. See [SECURITY.md](SECURITY.md).

## Licensing

Contributors may use the project under either the MIT License or Apache License 2.0; see [the licensing note](docs/project/licensing.md).
