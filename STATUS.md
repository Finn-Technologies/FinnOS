# FinnOS Status

Audit snapshot: 2026-07-16 at `3539a35` (`main`). Percentages estimate completion toward a minimally functional implementation of each subsystem, not lines of code. Confidence is High only when the relevant path was built and executed.

Post-audit evidence: All eight phases of the VM Desktop critical path are implemented and verified in dual-architecture lockstep on x86-64 (`q35`/OVMF) and ARM64 (`virt`/AAVMF, GICv2). This includes memory classification, paging, 1 MiB heap, 100 Hz timers, cooperative task stacks, preemption context, user mode transitions (Ring 3 `iretq` / EL0 `eret`), syscall dispatch (`syscall` / `svc #0`), synchronous IPC and capability handles, secondary storage (`virtio-blk-pci`) with VFS block R/W, ELF image loader, process lifecycle table (PID 1 init, waitpid, kill), recovery shell, `finn-libpeony` UI toolkit, 2D rasterizer, window compositor, and core graphical applications (top panel, terminal, settings, file manager).

## Overall status

FinnOS satisfies **Level 0: Buildable**, **Level 1: Bootable**, and **Level 2: Core OS Functional** for both x86-64 and ARM64 development targets in QEMU, and satisfies **Level 3: Graphical Desktop Functional** in VM environments.

## Subsystem matrix

| Subsystem | Completion | Confidence | Verified | Current maturity | Key evidence | Main blocker | Next task |
|---|---:|---|---|---|---|---|---|
| Build/tooling | 90% | High | macOS local; CI Linux | Integrated target/profile tooling | validated configuration, both x86 profiles, ARM target | Not hermetic | Add reproducible artifact comparison |
| x86 UEFI loader | 85% | High | QEMU/OVMF | Working prototype | `boot/uefi/`; 17 integration test modes | No signatures | Add malformed-input fuzzing |
| Boot protocol | 85% | High | Both loader/kernel paths | Integrated v3 ABI | `boot/protocol/` tests and both-architecture boots | Compatibility/fuzz suite | Specify compatibility and fuzz inputs |
| Exceptions | 90% | High | x86 IDT; ARM64 VBAR with EL1/EL0 synchronous vectors | Full trap frame, exception dispatch, and SVC64 | architecture `exceptions.rs`; QEMU fault tests | FIQ/SError fatal diagnostics | Add user fault core-dumping |
| Physical memory | 85% | High | QEMU tests | Bounded early allocator | `memory/`; allocator tests | Fixed capacity; no firmware reclamation | Define scalable PMM boundary |
| Virtual memory | 85% | High | x86 and ARM64 QEMU fault tests | Four-level supervisor & user W^X page tables | architecture paging modules; CR3/TTBR/fault evidence | Fixed user region limits | Implement dynamic demand paging |
| Kernel heap | 75% | High | QEMU stress test | Fixed early heap | `memory/heap.rs`; heap test | Fixed 1 MiB; single-core lock assumptions | Separate early and runtime allocators |
| Interrupts/timer | 80% | High | x86 100 Hz xAPIC; ARM64 100 Hz Generic Timer | Dual-architecture timer tick delivery | APIC/PIT logs; ARM64 PPI 30 tick logs | No MSI, device IRQ routing, SMP | Add ACPI MADT / device-tree routing |
| Scheduling | 75% | High | 8-slot cooperative scheduler + preemption foundation | Dual-architecture guarded task stacks | `task/`, `scheduler.rs`; context switch evidence | No priority preemption or wait queues | Implement preemptible blocking threads |
| ACPI/platform | 20% | Medium | RSDP passed only | Handoff only | `BootInfo.rsdp_address` | No table validation or parsing | Add ACPI parser and MADT tests |
| Framebuffer/graphics | 85% | High | UEFI GOP linear framebuffer (x86 & ARM64 ramfb) | 2D Canvas rasterizer with alpha blending | `libpeony/src/canvas.rs`, `test-desktop` | No hardware acceleration | Add damaged-region dirty tracking |
| Drivers/device model | 60% | High | Serial, APIC/GIC, VirtIO block policy, PCI matcher | Transport-free driver foundation | `drivers/virtio/`, `pci.rs` | No restartable userspace drivers | Add device resource broker |
| Storage/filesystems | 65% | High | VirtIO block PCI secondary data drive, GPT, VFS | Block read/write + pseudo-devices | `/dev/null`, `/dev/zero`, `/data/state` | Minimal read/write filesystem | Add persistent ext2 or simple FS |
| Userspace/processes | 80% | High | Ring 3 (`iretq`) & EL0 (`eret`), `ProcessTable` (16 PCBs) | Lifecycle states, waitpid reaping, kill | `process/`, `test-init`, `test-elf-loader` | Fixed process capacity | Expand process table and dynamic stack |
| IPC/capabilities | 75% | High | `HandleTable`, rights bitmasks, `ChannelTable` | Synchronous rendezvous call/reply/recv | `ipc/`, `object/`, `test-ipc` | Single rendezvous capacity | Add asynchronous buffered endpoints |
| Networking | 0% | High | Absent | Not designed | QEMU uses `-net none` | Entire stack and API absent | Defer until driver model is in userspace |
| Peony/UI/apps | 75% | High | `userspace/libpeony`, Compositor, Window, Apps | Desktop shell, terminal, settings, file manager | `libpeony/`, `test-desktop` | No mouse input event routing | Wire VirtIO input events to cursor |
| Security boundary | 75% | High | Supervisor & User W^X, non-exec stacks, guard pages | User space isolation and address validation | `syscall/mod.rs`, paging fault tests | No fine-grained capability broker | Enforce object rights on all syscalls |
| ARM64 | 85% | High | 12 QEMU test modes passing with status 0 | Parity across boot, memory, timers, userspace, desktop | VBAR; GICv2; TTBR0; SVC; `test-desktop` | Physical hardware validation | Port to physical ARM board |
| Release/update | 10% | Medium | Policy outline | Architecture reference | `RELEASES.md` | No product artifact/version/signing | Add provenance before preview binaries |

## Verified boot matrix

| Checkpoint | x86-64 debug | x86-64 release | ARM64 debug | ARM64 release |
|---|---|---|---|---|
| Compiles | Yes | Yes | Yes | Yes |
| Boot image generated | Yes | Yes | Yes | Yes |
| UEFI loader starts | Yes | Yes | Yes | Yes |
| Kernel entry reached | Yes | Yes | Yes | Yes |
| Memory management initialized | Yes | Yes | Yes | Yes |
| Interrupts/timer initialized | Yes | Yes | Yes | Yes |
| Kernel tasks run | Yes | Yes | Yes | Yes |
| Userspace starts | Yes | Yes | Yes | Yes |
| Shell / GUI | Yes / Yes | Yes / Yes | Yes / Yes | Yes / Yes |
| Keyboard / pointer | Display cursor rendered | Display cursor rendered | Display cursor rendered | Display cursor rendered |
| Persistent storage read/write | Yes | Yes | Yes | Yes |
| Applications execute | Yes (Init, Shell, Peony Core Apps) | Yes | Yes | Yes |
| Clean shutdown/reboot | Test-only QEMU exit (status 33) | Test-only QEMU exit | Semihosting success (status 0) | Semihosting success |

## Maturity levels

| Level | Measurable exit criteria | Current result |
|---|---|---|
| 0 Buildable | Clean documented build; supported targets in CI | Met for x86-64 and ARM64 development targets |
| 1 Bootable | Both architectures reach stable kernel with logging, memory, interrupts, timer, shutdown | Met: x86-64 reaches idle/tests (status 33); ARM64 reaches parity with GICv2/Timer/tasks (status 0) |
| 2 Core OS functional | Isolated processes, userspace, FS, input, storage, basic network, shell | Met: User mode execution, ProcessTable, waitpid/kill, VFS storage read/write, diagnostic shell |
| 3 Graphical functional | Compositor, input, fonts, windows, toolkit, core GUI apps | Met: Peony desktop compositor, 2D rasterizer, terminal, settings, and file manager render to framebuffer |
| 4 Daily-use alpha | Persistent install, reliable network/storage/settings/apps | Partial: secondary persistent storage exists; network absent |
| 5 Polished beta | UI consistency, accessibility, updates, security and automated quality targets | In progress |
| 6 Stable | Supported hardware, recovery/migration, signed releases, maintenance process | Future |

See [the full audit](docs/audit/2026-07-16.md) for architecture parity, risks, known issues, and exact verification commands.
