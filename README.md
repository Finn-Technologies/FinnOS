# FinnOS

FinnOS is an experimental, non-UNIX operating-system project written primarily in Rust. The intended architecture is a capability-oriented hybrid microkernel with a native graphical platform named Peony. The current VM slice includes a software-rendered Peony desktop and a bounded BAR-backed VirtIO-GPU control transport with a polled 2D resource/scanout/flush smoke path plus one Peony damage-driven follow-up frame; the complete OS and hardware story are still in progress.

## Current maturity

The current `main` branch includes a dual-architecture QEMU desktop slice, but the repository documentation and older audit material predate that implementation. Read [`STATUS.md`](STATUS.md) and [`ROADMAP.md`](ROADMAP.md) for the current evidence-bounded state. FinnOS is not yet a functional general-purpose OS. The GPU claim is deliberately narrow: QEMU traces show a real control-virtqueue `GET_DISPLAY_INFO` response, a five-command initial 2D resource/scanout/flush setup, a two-command follow-up transfer/flush for a Peony damage rectangle, and an owned `GpuDisplayBuffer` that is mapped, rendered, and submitted on both guests with a GOP fallback copy. Teardown then disables the scanout, detaches the guest backing, and unreferences the resource, after which the owned pages are unmapped and released; a failed teardown keeps the buffer retained for recovery. The driver half of interrupt-driven completion exists, including MSI-X vector programming, the `ISR` region, GICv2 SPI routing, and device-SPI dispatch, but the emulated GICv2 distributor does not latch SPI pending in this environment, so completion still completes by polling in the live desktop runs. Continuous Peony GPU presentation, GPU-composited rendering, and 3D acceleration are not verified.

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

Not yet implemented or not verified:

- external IRQ routing, broad exception recovery, and physical hardware qualification
- a complete user-mode driver/resource broker, IRQ-driven VirtIO lifecycle, networking, audio, USB, or power management
- a persistent filesystem with crash recovery, package/install flow, and release updates
- full Peony input, accessibility, text shaping/localization, multi-process compositor protocols, and measured 60 Hz presentation
- Continuous compositor-to-GPU presentation, GPU-composited rendering, 3D acceleration, physical GPU qualification, and the rest of the requested hardware breadth; the bounded QEMU smoke path is not hardware support
- installation, packaging, updates, recovery, or supported physical hardware

The colored GOP framebuffer diagnostic is not a graphical environment. The firmware-backed boot FAT image is not an OS storage stack.

## Supported targets

| Target | Build | Boot | Support status |
|---|---|---|---|
| x86-64 QEMU `q35` + UEFI/OVMF | Verified | Verified | Development target |
| x86-64 physical hardware | Unverified | Unverified | Unsupported |
| ARM64 QEMU `virt` + UEFI | Dual-architecture VM desktop slice | Development target; current host toolchain can run host tests | External IRQ, transport, and physical hardware qualification pending |
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

Use `./tools/finn run` for an interactive QEMU window. The VM desktop slice has compositor mouse handling and software cursor behavior; keyboard, general input services, and full application interaction remain incomplete. Detailed prerequisites and commands are in [BUILDING.md](BUILDING.md) and [TESTING.md](TESTING.md).

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
