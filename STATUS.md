# FinnOS Status

Audit snapshot: 2026-07-16 at `3539a35` (`main`). Percentages estimate completion toward a minimally functional implementation of each subsystem, not lines of code. Confidence is High only when the relevant path was built and executed.

## Overall status

FinnOS satisfies **Level 0: Buildable** for its declared x86-64 QEMU development target and partially satisfies **Level 1: Bootable**. It does not satisfy Level 1 across both promised architectures because ARM64 is metadata-only. It does not satisfy Level 2 because no userspace, storage, input, networking, or application execution exists.

## Subsystem matrix

| Subsystem | Completion | Confidence | Verified | Current maturity | Key evidence | Main blocker | Next task |
|---|---:|---|---|---|---|---|---|
| Build/tooling | 75% | High | macOS + CI Linux | Integrated x86 workflow | `tools/finnlib/`, local `check`, CI | Hard-coded x86/debug; not hermetic | Make target/profile data drive builds |
| x86 UEFI loader | 80% | High | QEMU/OVMF | Working prototype | `boot/uefi/`; eight boots | Trusted pointers/mappings; no signatures | Add malformed-input fuzzing |
| Boot protocol | 75% | High | Loader/kernel v2 | Integrated ABI | `boot/protocol/` tests and boot | Compatibility/fuzz suite | Specify compatibility and fuzz inputs |
| Exceptions | 70% | High | Vectors/test faults | Kernel foundation | `exceptions.rs`; exception tests | No user exceptions/recovery policy | Define user fault delivery |
| Physical memory | 65% | High | QEMU tests | Bounded early allocator | `memory/`; allocator tests | Fixed capacity; no firmware reclamation | Define scalable PMM boundary |
| Virtual memory | 55% | High | QEMU fault test | Kernel-only W^X map | `paging.rs`; CR3/fault evidence | No user address spaces or VM objects | Implement address-space object |
| Kernel heap | 55% | High | QEMU stress test | Fixed early heap | `memory/heap.rs`; heap test | Fixed 1 MiB; single-core lock assumptions | Separate early and runtime allocators |
| Interrupts/timer | 45% | High | BSP 100 Hz xAPIC | Timer foundation | APIC/PIT logs and timer test | No ACPI MADT, IOAPIC, MSI, SMP | Parse MADT and route one device IRQ |
| Scheduling | 40% | High | Cooperative BSP tasks | Ring-0 prototype | task/context/scheduler tests | No preemption, blocking, userspace, SMP | Land preemption-ready context design |
| ACPI/platform | 10% | Medium | RSDP passed only | Handoff only | `BootInfo.rsdp_address` | No table validation or parsing | Add ACPI parser and MADT tests |
| Framebuffer/graphics | 5% | High | Color fill only | Diagnostic | `framebuffer.rs`, boot log | No renderer, text, input, compositor | Define display buffer protocol after IPC |
| Drivers/device model | 3% | Medium | Serial/APIC/PIT only | Platform primitives | `arch/x86_64/` | No buses, IRQ routing, DMA, isolation | PCI + VirtIO design and resource broker |
| Storage/filesystems | 0% | High | Absent | Not implemented | No kernel storage modules | Block driver and VFS absent | VirtIO block read-only prototype |
| Userspace/processes | 0% | High | Absent | Design only | `docs/architecture/processes.md` | Ring 3, syscall, address spaces absent | Run one isolated user ELF |
| IPC/capabilities | 0% | High | Absent | Design only | `docs/architecture/ipc.md` | No handle/object implementation | Specify and test minimal endpoint primitive |
| Networking | 0% | High | Absent | Not designed | QEMU uses `-net none` | Entire stack and API absent | Defer until process/driver model works |
| Peony/UI/apps | 0% | High | Absent | Design only | `docs/architecture/peony.md` | Userspace, IPC, input, display absent | Implement only after core service runtime |
| Security boundary | 5% | Medium | Kernel W^X only | Accidental-fault hardening | NX, WP, guards, null unmapped | Everything executes in ring 0 | User isolation + capability enforcement |
| ARM64 | 0% | High | No executable | Planned metadata | `arm64-qemu.toml` says non-bootable | No loader, entry, MMU, GIC, timer | Minimal serial first boot in QEMU `virt` |
| Release/update | 0% | High | Absent | Policy outline | `RELEASES.md` | No product artifact/version/signing | Add provenance before preview binaries |

## Verified boot matrix

| Checkpoint | x86-64 debug | x86-64 release | ARM64 debug | ARM64 release |
|---|---|---|---|---|
| Compiles | Yes | Yes | Unsupported | Unsupported |
| Boot image generated | Yes | Wrapper is debug-only | No | No |
| UEFI loader starts | Yes | Not boot-tested | No | No |
| Kernel entry reached | Yes | Not boot-tested | No | No |
| Memory management initialized | Yes | Not boot-tested | No | No |
| Interrupts/timer initialized | Yes | Not boot-tested | No | No |
| Kernel tasks run | Yes | Not boot-tested | No | No |
| Userspace starts | No | No | No | No |
| Shell / GUI | No / No | No / No | No / No |
| Keyboard / pointer | No / No | No / No | No / No |
| Persistent storage read/write | No / No | No / No | No / No |
| Applications execute | No | No | No | No |
| Clean shutdown/reboot | Test-only QEMU exit; no production path | Unknown | No | No |

## Maturity levels

| Level | Measurable exit criteria | Current result |
|---|---|---|
| 0 Buildable | Clean documented build; supported targets in CI | Met for x86-64 only; ARM64 and reproducibility evidence missing |
| 1 Bootable | Both architectures reach stable kernel with logging, memory, interrupts, timer, shutdown | Partial: x86 reaches idle; ARM64 and production shutdown absent |
| 2 Core OS functional | Isolated processes, userspace, FS, input, storage, basic network, shell | Not met |
| 3 Graphical functional | Compositor, input, fonts, windows, toolkit, core GUI apps | Not met |
| 4 Daily-use alpha | Persistent install, reliable network/storage/settings/apps | Not met |
| 5 Polished beta | UI consistency, accessibility, updates, security and automated quality targets | Not met |
| 6 Stable | Supported hardware, recovery/migration, signed releases, maintenance process | Not met |

See [the full audit](docs/audit/2026-07-16.md) for architecture parity, risks, known issues, and exact verification commands.
