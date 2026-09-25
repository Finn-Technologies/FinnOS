# Agent Handoff: VirtIO-GPU Peony Damage Session

This handoff supersedes
`virtio-gpu-control-transport-2026-09-24.md` for the current GPU evidence.
The older handoff remains useful as the historical five-command
control/2D transport milestone.

- Objective: Add a resumable bounded 2D resource lifecycle and connect a real
  Peony `DamageRegion` follow-up to the same VirtIO-GPU control queue in both
  QEMU guests. Do not claim continuous compositor presentation, GPU-composited
  rendering, 3D acceleration, IRQ completion, or physical-hardware support.
- Starting commit/worktree:

```text
commit: 468ed41530ef598fe70019bd90edd47327bc4b74
## main...origin/main
 M .agents/STATE.md
 M HARDWARE_SUPPORT.md
 M README.md
 M ROADMAP.md
 M STATUS.md
 M UI_GUIDELINES.md
 M docs/architecture/drivers.md
 M docs/architecture/peony.md
 M kernel/src/bin/aarch64.rs
 M kernel/src/bin/x86_64.rs
 M kernel/src/drivers/virtio/gpu/queue.rs
 M tools/finnlib/qemu.py
 M tools/tests/test_boot_log.py
```

- Task state: Locally Verified worktree change; not committed, pushed, or
  integrated.
- Skills used: finnos-operating-rules, repository-orientation, task-planning,
  evidence-status-reporting, test-strategy, debugging-investigation,
  cross-architecture-design, unsafe-rust-low-level-safety, driver-architecture,
  virtio, graphics-architecture, compositor-window-system,
  documentation-maintenance, agent-handoff.
- Work completed:
  - Added `Gpu2dResource` and `Gpu2dResourceState` with explicit
    `Unbound`, `Created`, `BackingAttached`, `ContentTransferred`,
    `ScanoutBound`, and `Flushed` stages.
  - Added checked backing-size and address-range arithmetic plus
    stride-aware damage source offsets.
  - Made the control session advance through all incomplete lifecycle stages;
    a successful initial frame reports five commands, while a partial resource
    can resume at the first incomplete stage.
  - Connected both desktop paths to Peony’s returned damage from
    `Compositor::update_clock`; the follow-up submits only transfer and flush
    for that rectangle and reports two commands.
  - Hardened desktop validators to require the exact x86-64 and ARM64 Peony
    damage rectangles, initial/follow-up counts, and total command count, and
    to reject stale packet-only GPU claims and altered damage dimensions.
  - Captured independent final QEMU traces showing the follow-up
    `res_xfer_toh_2d` and `res_flush` for the taskbar damage rectangle.
  - Updated status, hardware support, roadmap, UI guidance, and architecture
    documents to distinguish the bounded session from continuous GPU
    compositing or acceleration.
- Files changed:
  - `kernel/src/drivers/virtio/gpu/queue.rs`
  - `kernel/src/bin/x86_64.rs`
  - `kernel/src/bin/aarch64.rs`
  - `tools/finnlib/qemu.py`
  - `tools/tests/test_boot_log.py`
  - `README.md`
  - `STATUS.md`
  - `HARDWARE_SUPPORT.md`
  - `ROADMAP.md`
  - `UI_GUIDELINES.md`
  - `docs/architecture/drivers.md`
  - `docs/architecture/peony.md`
  - `.agents/STATE.md`
  - this handoff
- Tests/commands run:
  - `cargo test --workspace -- --test-threads=1` -> pass: 213 kernel, 40
    libpeony, 15 libsys, 8 protocol, and 10 UEFI tests.
  - `cargo test -p finn-kernel drivers::virtio --lib` -> 42 focused
    VirtIO tests pass.
  - `cargo clippy --workspace --all-targets -- -D warnings` -> pass.
  - `cargo fmt --all -- --check` -> pass.
  - `python3 -m unittest discover -s tools/tests -p 'test_*.py'` -> 86/86
    pass.
  - `python3 .agents/scripts/validate.py --all` -> 87 skills, no dependency
    cycles.
  - `python3 .agents/scripts/check_links.py` -> 245 local references valid.
  - `git diff --check` -> pass.
  - `./tools/finn check-all` -> pass, exit 0.
  - `./tools/finn test-desktop` -> pass, QEMU status 33; markers include
    `VIRTIO_2D_INITIAL_COMMANDS=5`,
    `PEONY:GPU_DAMAGE regions=1 x=0 y=755 width=1280 height=45`,
    `VIRTIO_2D_FOLLOWUP_COMMANDS=2`, and
    `VIRTIO_2D_PRESENTATION_COMPLETED resource=1 commands=7`.
  - `./tools/finn test-desktop --target arm64-qemu` -> pass, QEMU status 0;
    markers include `VIRTIO_2D_INITIAL_COMMANDS=5`,
    `PEONY:GPU_DAMAGE_REGIONS=1`, `GPU_DAMAGE_X=0`,
    `GPU_DAMAGE_Y=555`, `GPU_DAMAGE_WIDTH=800`,
    `GPU_DAMAGE_HEIGHT=45`, `VIRTIO_2D_FOLLOWUP_COMMANDS=2`, and
    `VIRTIO_2D_PRESENTATION_COMMANDS=7`.
  - Final x86 QEMU trace:
    `/tmp/finnos-gpu-final-trace.IqLaLr/virtio-gpu.trace`.
  - Final ARM64 QEMU trace:
    `/tmp/finnos-gpu-final-trace.r5jPhc/virtio-gpu.trace`.
  - Both traces show the initial 2D setup and a follow-up
    `res_xfer_toh_2d` / `res_flush` pair at the corresponding Peony damage
    rectangle.
- Results and evidence classification:
  - Verified: the bounded resource lifecycle completes the five initial GPU
    commands in both QEMU guests.
  - Verified: the second frame is driven by the actual Peony damage result,
    not a hard-coded full-frame transfer.
  - Verified: QEMU independently receives the follow-up transfer and flush
    for the reported damage rectangle.
  - Implemented-unverified: injected failures during each lifecycle stage
    and restart/retry behavior; a general resource broker and revocation
    policy; continuous compositor presentation; IRQ/MSI completion; hardware
    cursor; VirGL/3D; latency and 60 Hz measurements; physical hardware.
  - Unsupported/unverified: broad driver breadth, VirtIO input, networking,
    production filesystems beyond the current VFS/block slice, packages,
    updates, release qualification, and physical GPU support.
- Documentation/status changes: Current claims now consistently describe the
  five-command setup plus two-command Peony damage follow-up. No roadmap
  item was promoted to complete and no continuous acceleration or physical
  hardware claim was made.
- Unverified assumptions: QEMU’s modern VirtIO-GPU behavior is representative
  only for these bounded commands; the static smoke buffer remains
  kernel-owned and single-threaded; the framebuffer is not yet allocated by a
  general GPU resource broker; failure-injection recovery has not yet been
  exercised end to end.
- Remaining work:
  1. Add a real owned GPU buffer allocator/resource broker and exercise
     create/attach/transfer/scanout/flush failure recovery with injected
     device responses.
  2. Add MSI/IRQ routing and replace bounded polling with lifecycle-safe
     completion handling.
  3. Connect VirtIO input and PS/2 keyboard polling to compositor/terminal
     routing.
  4. Add persistent filesystems, package/runtime services, networking, stock
     application breadth, and reference hardware qualification in roadmap
     order.
- Blockers: No physical hardware is connected; no general driver resource
  broker or restartable userspace driver model exists; no external device IRQ
  routing exists; the requested full desktop-class OS scope remains much larger
  than this bounded slice.
- Risks/regressions to watch: lifecycle state transitions after transport
  errors, backing range ownership, stride-aware damage offsets, queue
  completion bounds, DMA/cache barriers, and accidental promotion of a bounded
  QEMU session into a continuous acceleration claim. The software framebuffer
  must remain functional if the GPU path is absent or fails.
- Current Git state: Dirty worktree on `main` at
  `468ed41530ef598fe70019bd90edd47327bc4b74`; no commit, push, PR, or issue
  update was made. Existing user changes in the worktree were preserved.
- Suggested next action: Build the owned GPU resource/display service and a
  fault-injection harness for the lifecycle transitions, then add IRQ
  completion and framebuffer/input latency evidence.
- Skills next agent must load: finnos-operating-rules, repository-orientation,
  task-planning, evidence-status-reporting, test-strategy,
  debugging-investigation, cross-architecture-design,
  unsafe-rust-low-level-safety, driver-architecture, virtio,
  graphics-architecture, compositor-window-system, documentation-maintenance,
  agent-handoff.
