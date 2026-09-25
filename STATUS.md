# FinnOS Status

Audit snapshot: 2026-07-16 at `3539a35` (`main`). Percentages estimate completion toward a minimally functional implementation of each subsystem, not lines of code. Confidence is High only when the relevant path was built and executed.

Post-audit evidence: All eight phases of the VM Desktop critical path are implemented and verified in dual-architecture lockstep on x86-64 (`q35`/OVMF) and ARM64 (`virt`/AAVMF, GICv2). This includes memory classification, paging, 1 MiB heap, 100 Hz timers, cooperative task stacks, preemption context, user mode transitions (Ring 3 `iretq` / EL0 `eret`), syscall dispatch (`syscall` / `svc #0`), synchronous IPC and capability handles, secondary storage (`virtio-blk-pci`) with VFS block R/W, ELF image loader, process lifecycle table (PID 1 init, waitpid, kill), recovery shell, `finn-libpeony` UI toolkit, 2D rasterizer, window compositor, and core graphical applications (top panel, terminal, settings, file manager). The current worktree adds bounded damage-region tracking, clipped Peony recomposition, and a BAR-backed modern VirtIO-GPU control-virtqueue `GET_DISPLAY_INFO` query plus a five-command initial 2D resource/backing/transfer/scanout/flush setup and a two-command Peony damage follow-up frame verified in both QEMU guests. QEMU device traces independently show the initial command classes and the follow-up taskbar transfer/flush. This is bounded emulator evidence, not proof of continuous compositor presentation, GPU-composited rendering, 3D acceleration, physical hardware, or IRQ-driven presentation.

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
| Interrupts/timer | 85% | High | x86 100 Hz xAPIC; ARM64 100 Hz Generic Timer; ARM64 GICv2 SPI routing with a bounded device-handler table; GICv2m MSI doorbell; PCI MSI/MSI-X discovery; a fully programmed ARM64 MSI-X vector on the GPU control queue; group 0 and group 1 enabled on both the GIC distributor and CPU interface | Dual-architecture timer tick delivery, SPI group-1/priority routing, single-ownership handler registration, device-SPI acknowledgement and dispatch to a registered handler, and an ARM64 control-queue vector that is discovered, routed, and programmed end to end | APIC/PIT logs; ARM64 PPI 30 tick logs; GIC routing, handler-table, device-SPI classification, v2m TYPER/frame-base decode and MSI-X decode host tests; ARM64 desktop log showing `MSI_VECTOR_BOUND`, `MSI_ROUTE_ENABLED=1`, `GICD_CTLR=0x3`, `GICC_CTLR=0x3`, and `QUEUE_INTERRUPT_ENABLED` | The programmed vector still reports zero deliveries (`MSI_DELIVERIES=0`). Guest-side A/B tests show the frame latches on no line and in no group, and a control probe on a plain machine-wired INTx line shows the distributor reflecting every routing write (enabled, group 1, priority) yet latching no pending or active bit even via `GICD_ISPENDR`; a QEMU `-d int` trace of the same run shows 834 timer IRQs actually delivered. Device handling is correct, so the gap is the emulator's GICv2 model. x86 still has no IOAPIC or MSI-X table; no SMP | Confirm against a second QEMU version or a GICv3 port of the ARM64 interrupt path, then add ACPI MADT / device-tree routing and an x86 IOAPIC path |
| Scheduling | 75% | High | 8-slot cooperative scheduler + preemption foundation | Dual-architecture guarded task stacks | `task/`, `scheduler.rs`; context switch evidence | No priority preemption or wait queues | Implement preemptible blocking threads |
| ACPI/platform | 20% | Medium | RSDP passed only | Handoff only | `BootInfo.rsdp_address` | No table validation or parsing | Add ACPI parser and MADT tests |
| Framebuffer/graphics | 85% | High | UEFI GOP linear framebuffer (x86 & ARM64 ramfb); bounded VirtIO-GPU 2D smoke with owned display-buffer mapping/presentation and teardown | 2D Canvas rasterizer with alpha blending and bounded damage tracking; five-command initial resource/scanout/flush setup plus one damage-driven follow-up; a resumable three-command teardown (`SET_SCANOUT` disable, `RESOURCE_DETACH_BACKING`, `RESOURCE_UNREF`) that gates release; the ISR region is resolved and mapped, per-queue MSI-X vector programming and a bounded vector allocator exist, and a completion policy keeps the used ring authoritative over any interrupt; `GpuDisplayBuffer` validates geometry, owns a bounded `PageRange`, maps through supervisor pages, and falls back to GOP | `libpeony/src/canvas.rs`, `libpeony/src/compositor.rs`, `drivers/virtio/gpu/display.rs`, `drivers/virtio/gpu/queue.rs`, `drivers/virtio/transport.rs`, `test-desktop` | Backing is released only after `RESOURCE_UNREF` completes, but a failed teardown retains it for recovery; the driver half of interrupt completion exists and the ISR region is confirmed present in both emulators, but the emulator does not latch SPI pending for any interrupt, so completion still completes by polling in the live desktop runs; no continuous compositor presentation, GPU-composited frame, or 60 Hz measurement | Deliver device interrupts end to end (a second QEMU version or a GICv3 port first, then x86 IOAPIC/MSI-X), add a general resource broker, and measure latency |
| Drivers/device model | 67% | High | Serial, APIC/GIC, VirtIO block policy, PCI matcher, modern VirtIO-GPU control and bounded 2D command transport, and PCI MSI/MSI-X capability discovery | Kernel bootstrap transport with bounded BAR mapping and polled completions; MSI/MSI-X capability and MSI-X table-entry policy with read-back-verified per-queue vector programming; no restartable userspace driver model | `drivers/virtio/`, `pci.rs`, dual-architecture `test-desktop` logs and QEMU traces | No restartable userspace drivers, no platform MSI-X table programming, no delivered device interrupt, no full resource broker, no physical-device support | Add resource ownership and deliver interrupts end to end |
| Storage/filesystems | 65% | High | VirtIO block PCI secondary data drive, GPT, VFS | Block read/write + pseudo-devices | `/dev/null`, `/dev/zero`, `/data/state` | Minimal read/write filesystem | Add persistent ext2 or simple FS |
| Userspace/processes | 80% | High | Ring 3 (`iretq`) & EL0 (`eret`), `ProcessTable` (16 PCBs) | Lifecycle states, waitpid reaping, kill | `process/`, `test-init`, `test-elf-loader` | Fixed process capacity | Expand process table and dynamic stack |
| IPC/capabilities | 75% | High | `HandleTable`, rights bitmasks, `ChannelTable` | Synchronous rendezvous call/reply/recv | `ipc/`, `object/`, `test-ipc` | Single rendezvous capacity | Add asynchronous buffered endpoints |
| Networking | 0% | High | Absent | Not designed | QEMU uses `-net none` | Entire stack and API absent | Defer until driver model is in userspace |
| Peony/UI/apps | 75% | High | `userspace/libpeony`, Compositor, Window, Apps | Desktop shell, terminal, settings, file manager, clipped damage recomposition, semantic shortcut routing | `libpeony/`, `test-desktop` | No VirtIO input event routing, hardware keyboard polling, or accessibility tree | Wire VirtIO input events to cursor |
| Security boundary | 75% | High | Supervisor & User W^X, non-exec stacks, guard pages | User space isolation and address validation | `syscall/mod.rs`, paging fault tests | No fine-grained capability broker | Enforce object rights on all syscalls |
| ARM64 | 85% | High | 12 QEMU test modes passing with status 0; VirtIO-GPU control query, five-command setup, and Peony damage follow-up completed in desktop mode | Parity across boot, memory, timers, userspace, desktop, and bounded modern VirtIO-GPU control/2D transport | VBAR; GICv2; TTBR0; SVC; `test-desktop`; QEMU VirtIO-GPU trace | External IRQ, physical hardware, and full driver lifecycle validation | Qualify an ARM64 reference board |
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
| 3 Graphical functional | Compositor, input, fonts, windows, toolkit, core GUI apps | Met for the software-rendered VM desktop: Peony compositor, 2D rasterizer, terminal, settings, and file manager render to framebuffer; input/accessibility remain partial |
| 4 Daily-use alpha | Persistent install, reliable network/storage/settings/apps | Partial: secondary persistent storage exists; network absent |
| 5 Polished beta | UI consistency, accessibility, updates, security and automated quality targets | In progress |
| 6 Stable | Supported hardware, recovery/migration, signed releases, maintenance process | Future |

See [the full audit](docs/audit/2026-07-16.md) for architecture parity, risks, known issues, and exact verification commands.
