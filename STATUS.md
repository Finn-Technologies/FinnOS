# FinnOS Status

Audit snapshot: 2026-07-16 at `3539a35` (`main`). Percentages estimate completion toward a minimally functional implementation of each subsystem, not lines of code. Confidence is High only when the relevant path was built and executed.

Post-audit evidence: R1 through the ARM64 R4.1-R4.4 slices are integrated on `main` at `93ddf9d`. QEMU `virt`/AAVMF owns EL1 synchronous vectors, a strict v3 handoff and physical-memory path, bounded four-level TTBR0 tables, and a pinned BSP GICv2. During PR #17 conflict resolution, all six ARM64 modes and the full nine-mode x86 matrix passed locally. PR #17 additionally validates a preemption-ready x86 interrupt boundary but does not implement timer-driven scheduling.

## Overall status

FinnOS satisfies **Level 0: Buildable** for the integrated x86-64 and ARM64 development targets, and partially satisfies **Level 1: Bootable**. It does not satisfy Level 1 across both architectures because ARM64 still lacks timer, task, and shutdown parity plus production IRQ routing. It does not satisfy Level 2 because no userspace, storage, input, networking, or application execution exists.

## Subsystem matrix

| Subsystem | Completion | Confidence | Verified | Current maturity | Key evidence | Main blocker | Next task |
|---|---:|---|---|---|---|---|---|
| Build/tooling | 75% | High | macOS local; CI Linux | Integrated target/profile tooling | validated configuration, both x86 profiles, ARM target | Not hermetic | Add reproducible artifact comparison |
| x86 UEFI loader | 80% | High | QEMU/OVMF | Working prototype | `boot/uefi/`; nine debug integration modes | No signatures | Add malformed-input fuzzing |
| Boot protocol | 80% | High | Both loader/kernel paths | Integrated v3 ABI | `boot/protocol/` tests and both-architecture boots | Compatibility/fuzz suite | Specify compatibility and fuzz inputs |
| Exceptions | 70% | High | x86 vectors/test faults; local ARM `BRK`, aborts, and source-5 IRQ | x86 foundation plus local ARM R4.1/R4.3/R4.4 slices | architecture-specific `exceptions.rs`; QEMU tests | No user exceptions/recovery policy; FIQ/SError remain fatal | Add ARM timer IRQ policy |
| Physical memory | 65% | High | QEMU tests | Bounded early allocator | `memory/`; allocator tests | Fixed capacity; no firmware reclamation | Define scalable PMM boundary |
| Virtual memory | 55% | High | x86 and local ARM QEMU fault tests | Kernel-only W^X identity maps | architecture paging modules; CR3/TTBR/fault evidence | No user address spaces or VM objects; ARM space is immutable | Implement address-space object after parity |
| Kernel heap | 55% | High | QEMU stress test | Fixed early heap | `memory/heap.rs`; heap test | Fixed 1 MiB; single-core lock assumptions | Separate early and runtime allocators |
| Interrupts/timer | 50% | High | BSP 100 Hz xAPIC; local ARM GICv2 self-SGI | x86 timer foundation plus ARM controller slice | APIC/PIT logs; GIC IAR/EOIR evidence | No ARM timer, discovery, device IRQ routing, MSI, SMP | Add ARM architectural timer |
| Scheduling | 45% | High | Cooperative BSP tasks plus x86 preemption-context QEMU | Preemption-ready ring-0 prototype | complete frame/attribution/deferred-request evidence | No actual preemption, blocking, userspace, SMP | Implement safe thread preemption and wait queues |
| ACPI/platform | 10% | Medium | RSDP passed only | Handoff only | `BootInfo.rsdp_address` | No table validation or parsing | Add ACPI parser and MADT tests |
| Framebuffer/graphics | 5% | High | Color fill only | Diagnostic | `framebuffer.rs`, boot log | No renderer, text, input, compositor | Define display buffer protocol after IPC |
| Drivers/device model | 3% | Medium | Serial/APIC/PIT only | Platform primitives | `arch/x86_64/` | No buses, IRQ routing, DMA, isolation | PCI + VirtIO design and resource broker |
| Storage/filesystems | 0% | High | Absent | Not implemented | No kernel storage modules | Block driver and VFS absent | VirtIO block read-only prototype |
| Userspace/processes | 0% | High | Absent | Design only | `docs/architecture/processes.md` | Ring 3, syscall, address spaces absent | Run one isolated user ELF |
| IPC/capabilities | 0% | High | Absent | Design only | `docs/architecture/ipc.md` | No handle/object implementation | Specify and test minimal endpoint primitive |
| Networking | 0% | High | Absent | Not designed | QEMU uses `-net none` | Entire stack and API absent | Defer until process/driver model works |
| Peony/UI/apps | 0% | High | Absent | Design only | `docs/architecture/peony.md` | Userspace, IPC, input, display absent | Implement only after core service runtime |
| Security boundary | 5% | Medium | Kernel W^X only | Accidental-fault hardening | NX, WP, guards, null unmapped | Everything executes in ring 0 | User isolation + capability enforcement |
| ARM64 | 25% | High | Integrated R3-R4.4; local six-mode regression | Exception, memory, paging, and GIC foundation | VBAR; allocator; owned MMU/faults; GICv2 SGI IAR/EOIR; status 0 | No timer, tasks, shutdown, discovery, or hardware evidence | Add architectural generic timer |
| Release/update | 0% | High | Absent | Policy outline | `RELEASES.md` | No product artifact/version/signing | Add provenance before preview binaries |

## Verified boot matrix

| Checkpoint | x86-64 debug | x86-64 release | ARM64 debug | ARM64 release |
|---|---|---|---|---|
| Compiles | Yes | Yes | Yes | Unverified |
| Boot image generated | Yes | Yes | Yes | Unverified |
| UEFI loader starts | Yes | Yes | Yes | Unverified |
| Kernel entry reached | Yes | Yes | Yes | Unverified |
| Memory management initialized | Yes | Yes | Early physical allocator and owned MMU | No |
| Interrupts/timer initialized | Yes | Yes (local branch) | No | No |
| Kernel tasks run | Yes | Yes (local branch) | No | No |
| Userspace starts | No | No | No | No |
| Shell / GUI | No / No | No / No | No / No |
| Keyboard / pointer | No / No | No / No | No / No |
| Persistent storage read/write | No / No | No / No | No / No |
| Applications execute | No | No | No | No |
| Clean shutdown/reboot | Test-only QEMU exit; no production path | Test-only QEMU exit; no production path | No | No |

## Maturity levels

| Level | Measurable exit criteria | Current result |
|---|---|---|
| 0 Buildable | Clean documented build; supported targets in CI | Met for integrated x86-64 and ARM64 development targets |
| 1 Bootable | Both architectures reach stable kernel with logging, memory, interrupts, timer, shutdown | Partial: x86 reaches idle; ARM64 has local exception, early-memory, and owned-MMU slices, while interrupt/timer/task/shutdown parity remains absent |
| 2 Core OS functional | Isolated processes, userspace, FS, input, storage, basic network, shell | Not met |
| 3 Graphical functional | Compositor, input, fonts, windows, toolkit, core GUI apps | Not met |
| 4 Daily-use alpha | Persistent install, reliable network/storage/settings/apps | Not met |
| 5 Polished beta | UI consistency, accessibility, updates, security and automated quality targets | Not met |
| 6 Stable | Supported hardware, recovery/migration, signed releases, maintenance process | Not met |

See [the full audit](docs/audit/2026-07-16.md) for architecture parity, risks, known issues, and exact verification commands.
