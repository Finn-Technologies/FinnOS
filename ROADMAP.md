# FinnOS Roadmap

This roadmap is ordered by dependency, not calendar time. Complexity assumes one experienced contributor: XS hours, S days, M one to several weeks, L several weeks, XL a multi-milestone program. No team velocity data exists, so dates would be misleading.

Current state: x86-64 and ARM64 QEMU dual-architecture verified through Level 0, Level 1, Level 2, and Level 3 Graphical Desktop (Peony compositor, terminal, settings, file manager). See [STATUS.md](STATUS.md) and [.agents/STATE.md](.agents/STATE.md).

## Critical path

1. **M0 Reproducible Build [COMPLETE]:** target/profile build orchestration, ELF loader validation, both architecture targets.
2. **M1 Dual-Architecture Boot [COMPLETE]:** full boot parity across x86-64 (q35/OVMF) and ARM64 (virt/AAVMF/GICv2) with W^X, 100 Hz timers, and guarded task stacks.
3. **M2 Core Kernel [COMPLETE]:** cooperative scheduler, preemption context, user address spaces, syscall ABI (syscall / svc #0), capability handles, and synchronous IPC.
4. **M3 Userspace Foundation [COMPLETE]:** ELF binary image loader, PID 1 init process, diagnostic recovery shell, and process lifecycle management (waitpid, kill).
5. **M4 Devices and Storage [COMPLETE]:** VirtIO block PCI secondary data drive, GPT disk formatting, and VFS block read/write.
6. **M5 Graphical Stack [COMPLETE]:** `finn-libpeony` UI toolkit, 2D rasterizer with alpha blending, window compositor, desktop shell, terminal emulator, settings viewer, and file manager.
7. **M6 Developer Preview [IN PROGRESS]:** install/persist, SDK/packages, diagnostics, signed artifacts and recovery.
8. **M7 Beta [FUTURE]:** updates/rollback, security review, accessibility, reliability/performance gates and physical hardware qualification.
9. **M8 Stable 1.0 [FUTURE]:** supported configurations, migration/recovery, signed maintenance and long-term policy.

## Actionable roadmap

Each item includes the fields needed to become a GitHub issue. P0 blocks build/boot, P1 core OS, P2 graphics, P3 alpha/beta, and P4 broader polish.

### R1 [P0] Unify target/profile build orchestration

**Description/current:** the R1 implementation uses `Finnfile.toml` to select one authoritative target file and profile, while Python consumes the validated result for build/image/QEMU paths. **Desired/reason:** one validated target model must produce debug/release artifacts and prevent ARM/config drift. **Dependencies:** none. **Architecture:** shared tooling, x86 and ARM. **Complexity:** M. **Acceptance:** CLI accepts target/profile; normal x86 tests remain green; release image is generated and boot-tested; invalid combinations fail clearly. **Verification:** Python unit tests, both x86 profiles, CI matrix. **Files:** `tools/finnlib/`, `Finnfile.toml`, `build/targets/`. **Milestone:** M0. **Labels:** `priority:P0`, `type:maintenance`, `area:tooling`, `arch:all`, `complexity:M`.

**Implementation status:** locally verified on the publication branch based on `cc828ec`; development and release first boot pass, negative configuration tests pass, and CI matrix/evidence changes are present. R1 remains incomplete until the updated workflows pass after integration.

### R2 [P0] Harden the UEFI loader and protocol

**Description/current:** the disjoint-executable-segment entry bug was fixed during the audit, but handoff still trusts pointers and inherited mappings and malformed-input coverage is narrow. **Desired/reason:** malformed ELF/protocol inputs must fail deterministically under documented trust assumptions. **Dependencies:** none. **Architecture:** shared protocol plus architecture-specific ELF machine. **Complexity:** M. **Acceptance:** retain the disjoint-segment regression test; add overflow/overlap/property tests; protocol comments match v2; threat assumptions documented. **Verification:** host tests, fuzz corpus, all QEMU tests. **Files:** `boot/uefi/`, `boot/protocol/`, `docs/reference/`. **Milestone:** M0. **Labels:** `priority:P0`, `type:maintenance`, `area:boot`, `security`, `complexity:M`.

### R3 [P0] Establish ARM64 serial first boot

**Description/current:** the integrated R3 path produces `BOOTAA64.EFI` and an AArch64 kernel, then reaches an ordered serial-ready marker in QEMU `virt`/AAVMF. **Desired/reason:** AArch64 UEFI loader and kernel entry reach a deterministic serial marker in QEMU `virt`; early porting prevents x86 interfaces from hardening accidentally. **Dependencies:** R1. **Architecture:** ARM64. **Complexity:** L. **Acceptance:** `BOOTAA64.EFI`, linker/entry/panic/UART, image/QEMU command, bounded smoke test and CI. **Verification:** AAVMF QEMU status/ordered markers on clean CI. **Files:** new ARM boot/kernel arch code, tooling, workflows. **Milestone:** M1. **Labels:** `priority:P0`, `type:feature`, `area:boot`, `arch:arm64`, `complexity:L`.

**Implementation status:** integrated at `f74fc49`; the R3 handoff records local QEMU status 0 and green integration CI for the required loader/kernel serial markers. R4 memory, MMU, GIC, timer, context, and shutdown parity remain incomplete.

### R4 [P0] Complete architecture boot parity

**Description/current:** x86 has MMU/exception/timer/tasks; ARM has locally verified R4.1 exceptions, R4.2 handoff/early allocation, R4.3 owned MMU, and R4.4 pinned BSP GICv2 self-SGI delivery, but no generic timer or tasks; x86 lacks shutdown. **Desired/reason:** both ports meet Level 1 with shared contracts. **Dependencies:** R3. R5 device discovery/routing follows in M2 and is not required for the emulator-only Level 1 baseline. **Architecture:** both. **Complexity:** XL. **Acceptance:** ARM memory map/MMU/guards/GIC/timer/context tests match architecture-neutral semantics; both ports provide emulator shutdown/reboot and fatal diagnostics. **Verification:** parity CI and fault tests. **Files:** `kernel/src/arch/`, QEMU validators. **Milestone:** M1. **Labels:** `priority:P0`, `type:feature`, `area:kernel`, `arch:all`, `complexity:XL`.

**Implementation status:** R4.1-R4.4 are integrated and locally reverified on 2026-07-20. AAVMF/QEMU enters at EL1, owns synchronous vectors and bounded fatal diagnostics, copies a strictly validated page-owned v3 handoff, classifies memory, constructs the shared allocator, activates a four-level supervisor W^X TTBR0 regime with four precise fault probes, and drives one real SGI 1 through the pinned BSP GICv2 acknowledge/EOI lifecycle. This is not full parity and does not implement the generic timer, contexts/tasks, external routing/discovery, or shutdown.

### R5 [P1] Add platform discovery and interrupt routing

**Description/current:** ACPI RSDP is unparsed; only BSP timer vectors exist. **Desired/reason:** validated ACPI MADT/PCI discovery and IOAPIC/MSI resource ownership are prerequisites for devices. ARM uses an equivalent device-tree/ACPI and GIC interface. **Dependencies:** stable trap API. **Architecture:** x86 ACPI/IOAPIC; ARM DT/ACPI/GIC. **Complexity:** L. **Acceptance:** enumerate topology; route/mask/ack one synthetic device IRQ; teardown and malformed-table tests. **Verification:** QEMU interrupt integration and parser unit tests. **Files:** platform/arch/driver foundations. **Milestone:** M2. **Labels:** `priority:P1`, `type:feature`, `area:drivers`, `arch:all`, `complexity:L`.

### R6 [P1] Implement preemptible blocking threads

**Description/current:** cooperative tasks cannot sleep/block and timer never schedules. **Desired/reason:** normalized trap contexts, deferred rescheduling, wait queues, deadlines, and safe allocator/lock rules. **Dependencies:** current #16/#17 decision, timer API. **Architecture:** context-entry glue per port, shared policy. **Complexity:** XL. **Acceptance:** forced preemption under allocation/timer load, sleep/wakeup/cancel tests, no ISR allocation or lock inversion, starvation bounds documented. **Verification:** deterministic QEMU stress on both ports. **Files:** task/scheduler/context/timer/synchronization. **Milestone:** M2. **Labels:** `priority:P1`, `type:feature`, `area:kernel`, `arch:all`, `complexity:XL`.

**Implementation status:** the x86-64 foundation now validates complete ring-0 interrupt-return frames, dispatcher-selected return pointers, stack-derived task attribution, bounded nested preemption-disable guards, and deferred timer reschedule requests. It deliberately performs no switch in the timer ISR and does not yet implement blocking, wait queues, user-mode frames, SMP, or ARM64 context parity; R6 therefore remains in progress.

### R7 [P1] Create user address spaces and syscall ABI

**Description/current:** all mappings/tasks are supervisor ring 0. **Desired/reason:** isolated processes need user mappings, copy/validation primitives, syscall entry/return, and fault termination. **Dependencies:** R4, R6. **Architecture:** x86 `syscall/sysret` or safe equivalent; ARM `svc`; shared semantics. **Complexity:** XL. **Acceptance:** user code cannot read/write kernel or another process; invalid pointers cannot crash kernel; W^X and guard invariants enforced; syscall ABI versioned. **Verification:** adversarial QEMU isolation suite. **Files:** VM, process, syscall, exception code. **Milestone:** M2. **Labels:** `priority:P1`, `type:feature`, `area:kernel`, `security`, `complexity:XL`.

### R8 [P1] Implement kernel objects, handles, and minimal IPC

**Description/current:** capability/typed IPC designs have no code. **Desired/reason:** endpoints, bounded messages, rights-bearing generation-safe handles, transfer, cancellation, and backpressure validate the microkernel direction. **Dependencies:** R6, R7. **Architecture:** shared. **Complexity:** XL. **Acceptance:** two isolated processes exchange messages/handles; stale/forged rights fail; quotas prevent unbounded kernel memory; peer death wakes callers. **Verification:** host state-machine tests plus QEMU adversarial integration. **Files:** new object/capability/IPC modules and ABI docs. **Milestone:** M2. **Labels:** `priority:P1`, `type:feature`, `area:ipc`, `security`, `complexity:XL`.

### R9 [P1] Boot init, runtime, and diagnostic shell

**Description/current:** no userspace or executable loader. **Desired/reason:** load an ELF process, start init, logging, service supervision, and a serial shell with minimal utilities. **Dependencies:** R7, R8. **Architecture:** shared userspace. **Complexity:** XL. **Acceptance:** init starts/restarts a service; shell lists processes/memory and runs a second executable; crashes produce bounded diagnostics. **Verification:** QEMU end-to-end shell script on both ports. **Files:** new `userspace/`, runtime, loader, service protocols. **Milestone:** M3. **Labels:** `priority:P1`, `type:feature`, `area:userspace`, `arch:all`, `complexity:XL`.

### R10 [P1] Define driver resource broker and VirtIO baseline

**Description/current:** no driver model, DMA, bus, or restart path. **Desired/reason:** a device manager grants bounded MMIO/IRQ/DMA capabilities to restartable drivers; VirtIO proves the model. **Dependencies:** R5, R8, R9. **Architecture:** transport-specific discovery, shared protocol. **Complexity:** XL. **Acceptance:** enumerate VirtIO, start/restart driver, revoke resources, handle malformed descriptors, prove DMA bounds. **Verification:** QEMU reset/fault/conformance tests. **Files:** device manager, driver SDK, PCI/MMIO transport. **Milestone:** M4. **Labels:** `priority:P1`, `type:feature`, `area:drivers`, `complexity:XL`.

### R11 [P1] Add block storage, VFS, and persistent root

**Description/current:** runtime storage is absent. **Desired/reason:** VirtIO block, partitions, a small documented filesystem/VFS, file handles, permissions, cache, mount, and crash policy enable a real OS. **Dependencies:** R10, IPC, service runtime. **Architecture:** shared, driver transport differs. **Complexity:** XL. **Acceptance:** persistent create/read/write/rename across reboot; bounds and permission checks; forced-power-loss recovery policy tested; root image reproducible. **Verification:** block fault injection and filesystem model/integration tests. **Files:** storage driver/services, VFS, image tools. **Milestone:** M4. **Labels:** `priority:P1`, `type:feature`, `area:storage`, `complexity:XL`.

### R12 [P1] Add input and basic networking services

**Description/current:** both are absent. **Desired/reason:** VirtIO input provides keyboard/pointer events; VirtIO net plus Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS and a bounded service API supports alpha use. TCP/IPv6 follow only with explicit tests. **Dependencies:** R8-R10. **Architecture:** shared protocols/drivers. **Complexity:** XL. **Acceptance:** shell receives key events; pointer coordinates are normalized; DHCP and DNS work; ping/UDP loopback and malformed packets do not crash services. **Verification:** QEMU packet/input automation and fuzzing. **Files:** input/network drivers and services. **Milestone:** M4. **Labels:** `priority:P1`, `type:feature`, `area:input`/`area:network`, `complexity:XL`.

### R13 [P2] Implement Peony display, compositor, and input routing

**Description/current:** only raw framebuffer fill exists. **Desired/reason:** software-rendered surfaces, damage, composition, focus, input routing, and deterministic window behavior establish Level 3 without GPU scope. **Dependencies:** R8-R12. **Architecture:** shared userspace; display backend differs. **Complexity:** XL. **Acceptance:** three isolated clients render/move/resize; occlusion/damage correct; focus/grab policy tested; compositor restart has defined recovery; 60 Hz target at 1280x800 under reference QEMU workload. **Verification:** screenshot, protocol, latency, leak, crash tests. **Files:** new `peony/`, display/input protocols. **Milestone:** M5. **Labels:** `priority:P2`, `type:feature`, `area:graphics`, `area:ui`, `complexity:XL`.

### R14 [P2] Add text, toolkit, accessibility, and core shell

**Description/current:** all absent. **Desired/reason:** fonts/shaping, DPI, controls, keyboard navigation, accessibility tree, shell/panel/launcher/notifications, terminal, settings, and file manager make graphics usable. **Dependencies:** R13, storage. **Architecture:** shared. **Complexity:** XL. **Acceptance:** UI tokens in `UI_GUIDELINES.md`; complete keyboard operation; contrast/focus targets; scale at 1x/1.5x/2x; core apps handle loading/empty/error states; localization-safe layouts. **Verification:** visual/accessibility/performance tests. **Files:** Peony framework/shell/apps/design tokens. **Milestone:** M5. **Labels:** `priority:P2`, `type:feature`, `area:ui`, `accessibility`, `complexity:XL`.

### R15 [P3] Deliver SDK, packages, and developer preview image

**Description/current:** no app SDK/package/install model. **Desired/reason:** versioned APIs, manifests, capability declarations, signed packages, examples, and persistent image allow external development. **Dependencies:** R9, R11, R14. **Architecture:** shared. **Complexity:** XL. **Acceptance:** clean SDK builds/installs/runs sample CLI and GUI apps; package rights visible before install; compatibility/version policy documented. **Verification:** clean-environment tutorial and package tamper tests. **Files:** SDK, package service, app docs. **Milestone:** M6. **Labels:** `priority:P3`, `type:feature`, `area:sdk`, `area:packaging`, `complexity:XL`.

### R16 [P3] Add secure update, recovery, and release provenance

**Description/current:** no updater, rollback, signatures, SBOM, or provenance. **Desired/reason:** atomic signed system updates and a recovery path are mandatory before persistent user data is trusted. **Dependencies:** R11, signing/version policy. **Architecture:** shared. **Complexity:** XL. **Acceptance:** interrupted update boots old or new valid state; rollback/recovery documented; artifacts signed with checksums/SBOM/provenance; keys and incident rotation defined. **Verification:** power-failure matrix, signature/tamper/reproducibility tests. **Files:** update/recovery services, release workflows. **Milestone:** M6/M7. **Labels:** `priority:P3`, `type:feature`, `area:release`, `security`, `complexity:XL`.

### R17 [P3] Security and reliability beta gate

**Description/current:** no threat model, fuzz program, uptime or performance gate. **Desired/reason:** define assets/trust boundaries; audit unsafe code/syscalls/IPC/drivers; add entropy/crypto/secrets/auth policy and measurable reliability. **Dependencies:** implemented product surface. **Architecture:** all. **Complexity:** XL. **Acceptance:** threat model reviewed; critical fuzz targets stable; no known critical vulnerabilities; 24-hour supported-VM stress without kernel panic/leak beyond threshold; crash disclosure bounded. **Verification:** independent review, fuzz/stress reports. **Files:** security docs/tests/CI. **Milestone:** M7. **Labels:** `priority:P3`, `type:security`, `area:security`, `complexity:XL`.

### R18 [P3] Define and qualify supported hardware

**Description/current:** no physical hardware is supported. **Desired/reason:** select at most one x86 computer and one ARM board only after VM stability; publish exact device/firmware constraints. **Dependencies:** M6 stability, driver strategy. **Architecture:** both. **Complexity:** XL per device. **Acceptance:** installation, storage, input, display, network, suspend/reboot, recovery and soak matrix passes; unsupported variants explicit. **Verification:** repeatable hardware-lab report. **Files:** hardware matrix/drivers/lab automation. **Milestone:** M7/M8. **Labels:** `priority:P3`, `type:feature`, `area:hardware`, `complexity:XL`.

### R19 [P3] Stable 1.0 release gate

**Description/current:** no OS release exists. **Desired/reason:** freeze supported APIs/data formats, migration/recovery, signing, versioning, security response, support window, and maintenance ownership. **Dependencies:** all M7 gates. **Architecture:** supported targets. **Complexity:** L. **Acceptance:** [release checklist](RELEASING.md) complete; zero open P0/P1 release blockers; signed reproducible artifacts; clean install/update/rollback; published support/EOL and known issues. **Verification:** release candidate qualification by someone other than implementer where possible. **Files:** release docs/workflows/artifacts. **Milestone:** M8. **Labels:** `priority:P3`, `type:release`, `complexity:L`.

### R20 [P4] Post-1.0 expansion

**Description/current:** GPU, USB breadth, Wi-Fi/Bluetooth, audio, multi-monitor, mobile/tablet and compatibility layers are absent. **Desired/reason:** expand only after core contracts and maintenance are proven. **Dependencies:** stable 1.0. **Architecture:** device-specific. **Complexity:** XL each. **Acceptance:** separate RFC, support target, conformance/performance/security tests for each feature. **Verification:** feature-specific qualification. **Files:** future. **Milestone:** post-1.0. **Labels:** `priority:P4`, `type:feature`, `status:needs-rfc`, `complexity:XL`.

## Roadmap views

| View | Ordered items |
|---|---|
| x86-64 | R1, R2, R5-R19 |
| ARM64 | R1, R3, R4, then architecture parity for R6-R19 |
| Kernel | R2, R4-R8, R17 |
| Drivers/hardware | R5, R10-R12, R18, R20 |
| Userspace | R7-R12, R15 |
| Graphics/UI | R12-R15, R17 |
| Networking | R8-R10, R12, R17 |
| Security | R2, R7-R8, R10-R12, R15-R17, R19 |
| Testing/CI | Verification criteria in every item; R1, R3-R4, R17 |
| Documentation | Architecture/ABI docs in every milestone; tutorials in R9/R15; support/release in R18-R19 |
| Release | R15-R19 |

## Next 10 engineering tasks

1. Connect VirtIO input devices (keyboard/pointer) to route physical key/mouse events to the Peony compositor and terminal app.
2. Implement damaged-region dirty rectangles in `libpeony::Compositor` to optimize rendering frame times.
3. Add persistent filesystem mounting (e.g. ext2 or SimpleFS) on the secondary `virtio-blk-pci` data partition.
4. Implement preemptible blocking threads with wait queues and priority scheduling.
5. Parse and validate ACPI MADT / device-tree platform tables for dynamic interrupt resource discovery.
6. Provide an interactive GUI package manager / installer interface in Peony Desktop.
7. Support multi-process desktop clients connecting to the compositor via IPC shared VMOs.
8. Expand `userspace/libsys` with standard C runtime compatibility / POSIX-like shim library.
9. Implement basic TCP/IP networking service over VirtIO net.
10. Define and qualify first physical hardware target for dual-architecture native installation.
