# FinnOS

FinnOS is an experimental, non-UNIX operating-system project written primarily in Rust. The intended architecture is a capability-oriented hybrid microkernel with a native graphical platform named Peony. That architecture is a design direction, not the current implementation.

## Current maturity

FinnOS is an x86-64 UEFI kernel prototype for QEMU with an integrated ARM64 serial-entry port and locally verified R4 exception and early-memory slices. It is not yet a functional general-purpose OS.

Verified on 2026-07-16:

- The debug and release Rust workspaces build on an Apple ARM64 host.
- A 64 MiB FAT32 x86-64 UEFI image boots under QEMU `q35` with OVMF.
- The loader validates and loads an ELF64 kernel and passes a UEFI memory map, GOP framebuffer, and ACPI RSDP.
- The kernel installs GDT/TSS/IDT state, classifies memory, allocates physical pages, activates private W^X page tables, maps a guarded 1 MiB heap, starts a 100 Hz xAPIC timer, and runs bounded cooperative ring-0 tasks.
- The Rust and Python host suites and all nine debug x86-64 QEMU integration scenarios pass, including the preemption-context foundation.

R3 and the R4.1-R4.4 ARM64 slices are integrated. Locally reverified on
2026-07-20: AAVMF enters at EL1, installs a
FinnOS-owned vector table, resumes one controlled `BRK`, and terminates an
unarmed `BRK` through bounded fatal diagnostics, copies and validates the v3
handoff, classifies the UEFI map, constructs the early physical-page allocator,
and activates bounded supervisor-only translation tables whose four guarded
fault cases are exercised on hardware. A pinned single-BSP GICv2 path also
delivers, acknowledges, and EOIs a real self-SGI. Broader architecture parity
remains pending. The x86-64 preemption-context work provides complete ring-0
interrupt-return frames, stack-derived task attribution, and deferred reschedule
requests; the timer still returns to the interrupted task and does not perform
scheduling.

Not implemented:

- ARM64 timer, task, or shutdown parity; external IRQ routing and broad exception recovery
- user mode, processes, system calls, IPC, or capability enforcement
- device discovery, device IRQ routing, and general drivers
- block storage, filesystems, persistent data, or a shell
- networking, audio, USB, input, GPU acceleration, or power management
- compositor, window system, fonts, toolkit, desktop, or applications
- installation, packaging, updates, recovery, or supported physical hardware

The colored GOP framebuffer diagnostic is not a graphical environment. The firmware-backed boot FAT image is not an OS storage stack.

## Supported targets

| Target | Build | Boot | Support status |
|---|---|---|---|
| x86-64 QEMU `q35` + UEFI/OVMF | Verified | Verified | Development target |
| x86-64 physical hardware | Unverified | Unverified | Unsupported |
| ARM64 QEMU `virt` + UEFI | Integrated serial entry, exceptions, memory, owned MMU, and BSP GICv2 SGI | R3-R4.4 integrated and locally reverified | Timer, task, shutdown, discovery, and external IRQ parity pending |
| ARM64 physical hardware | No implementation | No | Unsupported |

See [supported platforms](SUPPORTED_PLATFORMS.md) and [hardware support](HARDWARE_SUPPORT.md).

## Build and run

```bash
./tools/finn doctor
./tools/finn check
./tools/finn image
./tools/finn test-boot
./tools/finn test-boot --profile release
```

Use `./tools/finn run` for an interactive QEMU window. The current system does not accept input; the window only displays the framebuffer diagnostic. Detailed prerequisites and commands are in [BUILDING.md](BUILDING.md) and [TESTING.md](TESTING.md).

## Repository map

- `boot/protocol/`: versioned loader/kernel handoff ABI
- `boot/uefi/`: x86-64/ARM64 UEFI loader
- `kernel/`: architecture-independent memory/task policy and x86-64 kernel code
- `tools/`: build, image, QEMU, and log-validation tooling
- `tests/`: test policy
- `docs/architecture/`: canonical architecture documentation
- `docs/proposals/adr/`: accepted architecture decisions
- `docs/audit/`: evidence-backed project audit and detailed plan
- `docs/github-planning/`: proposed repository planning metadata
- `.agents/`: mandatory agent operating procedures, skills, validation, and handoff system

## Project documents

- [Current status and completion matrix](STATUS.md)
- [Technical architecture](ARCHITECTURE.md)
- [Roadmap and critical path](ROADMAP.md)
- [Complete audit](docs/audit/2026-07-16.md)
- [Known issues and risks](docs/audit/2026-07-16.md#known-issues)
- [UI and design-system plan](UI_GUIDELINES.md)
- [Porting and ARM64 plan](PORTING.md)
- [Security policy and hardening plan](SECURITY.md)
- [Contributing](CONTRIBUTING.md)
- [Agent operating system](.agents/README.md)

## Warning

FinnOS can panic, hang, lose data once storage is introduced, and expose all code at kernel privilege. Do not use it for production workloads or sensitive information. No OS release or compatibility guarantee currently exists.

FinnOS is available under either MIT or Apache-2.0; see [the licensing note](docs/project/licensing.md).
