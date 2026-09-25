# Agent Handoff: VirtIO-GPU Control and Bounded 2D Presentation Verification

> Superseded for current GPU evidence by
> `virtio-gpu-peony-damage-2026-09-24.md`. This file records the historical
> five-command control/2D transport milestone.

- Objective: Close the bounded split-ring transport gap for VirtIO-GPU and
  verify one real `GET_DISPLAY_INFO` request plus a five-command 2D
  resource/backing/transfer/scanout/flush sequence in both QEMU guests,
  without claiming continuous compositor presentation, GPU-composited
  rendering, 3D acceleration, or physical hardware support.
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
 M kernel/arch/x86_64/linker.ld
 M kernel/src/arch/x86_64/mod.rs
 M kernel/src/bin/aarch64.rs
 M kernel/src/bin/x86_64.rs
 M kernel/src/drivers/pci.rs
 M kernel/src/drivers/virtio/gpu.rs
 M kernel/src/drivers/virtio/mod.rs
 M userspace/libpeony/src/canvas.rs
 M userspace/libpeony/src/compositor.rs
 M userspace/libpeony/src/lib.rs
?? .agents/handoffs/peony-damage-presentation-2026-09-24.md
?? kernel/src/arch/x86_64/keyboard.rs
?? kernel/src/drivers/virtio/gpu/
?? kernel/src/drivers/virtio/pci.rs
?? kernel/src/drivers/virtio/split_queue.rs
?? kernel/src/drivers/virtio/transport.rs
?? userspace/libpeony/src/input.rs
```
- Task state: Locally Verified worktree change; not committed, pushed, or integrated.
- Skills used: finnos-operating-rules, repository-orientation, task-planning, evidence-status-reporting, test-strategy, debugging-investigation, cross-architecture-design, unsafe-rust-low-level-safety, driver-architecture, virtio, qemu-boot-testing, build-environment-management, build-orchestration, documentation-maintenance, agent-handoff.
- Work completed:
  - Corrected modern split-ring geometry so negotiated `VIRTIO_F_EVENT_IDX` adds the required event field without overlapping the used ring or request buffer.
  - Made event-index selection explicit in `SplitVirtqueueLayout`; legacy rings remain available for non-event-index callers.
  - Added the driver-owned available-ring event-field publication and preserved the device-owned used-ring event field.
  - Kept direct-descriptor-only negotiation, corrected VirtIO-GPU cursor command IDs, and modeled the fixed 16-scanout display response.
  - Added host regressions for event-ring sizes, event-field offsets, GPU smoke storage disjointness, and response/header serialization.
  - Integrated `submit_2d_presentation` after the enabled-scanout query in
    both architecture-specific desktop smoke paths. The presenter uses the
    rendered Peony framebuffer address and submits resource creation,
    backing attachment, host transfer, scanout binding, and flush.
  - Added guest completion markers with `commands=5`; hardened desktop log
    validators to require GPU query/presentation evidence and reject the old
    packet-only hardware-cursor, double-buffer, and page-flip claims.
  - Removed the interactive x86 hardware-cursor claim until a real cursor
    resource and scanout are submitted; the software cursor remains active.
  - Captured independent QEMU `virtio_gpu_cmd_*` traces for both guests,
    showing `res_create_2d`, `res_back_attach`, `res_xfer_toh_2d`,
    `set_scanout`, and `res_flush` after the guest query.
  - Reconciled `STATUS.md`, `.agents/STATE.md`, `README.md`,
    `HARDWARE_SUPPORT.md`, `UI_GUIDELINES.md`, and architecture docs with the
    bounded evidence boundary.
- Files changed:
  - `kernel/src/drivers/virtio/split_queue.rs`
  - `kernel/src/drivers/virtio/transport.rs`
  - `kernel/src/drivers/virtio/gpu.rs`
  - `kernel/src/drivers/virtio/gpu/queue.rs`
  - `kernel/src/drivers/virtio/pci.rs`
  - `kernel/src/bin/x86_64.rs`
  - `kernel/src/bin/aarch64.rs`
  - `kernel/src/drivers/pci.rs`
  - `tools/finnlib/qemu.py`
  - `tools/tests/test_boot_log.py`
  - `STATUS.md`
  - `.agents/STATE.md`
  - `README.md`
  - `HARDWARE_SUPPORT.md`
  - `UI_GUIDELINES.md`
  - `docs/architecture/drivers.md`
  - `docs/architecture/peony.md`
  - `.agents/handoffs/virtio-gpu-control-transport-2026-09-24.md`
- Tests/commands run:
  - `./tools/finn check` -> pass; workspace host tests: 212 kernel, 40
    libpeony, 15 libsys, 8 protocol, and 10 UEFI tests.
  - `cargo test -p finn-kernel drivers::virtio --lib` -> 41 focused
    VirtIO tests pass.
  - `cargo test -p finn-libpeony --lib` -> 40 tests pass.
  - `python3 -m unittest tools.tests.test_boot_log` -> 54 tests pass.
  - Target-aware x86-64 and ARM64 desktop kernels both build for their
    bare-metal targets with `kernel-bin,qemu-test-exit,qemu-test-desktop`.
  - `git diff --check` and `cargo fmt --all -- --check` pass.
  - `python3 .agents/scripts/validate.py --all` -> 87 skills, no dependency
    cycles.
  - `python3 .agents/scripts/check_links.py` -> 245 local references valid.
  - `python3 -m unittest discover -s tools/tests -p 'test_*.py'` -> 86/86
    pass.
  - `git diff --check` -> pass.
  - `./tools/finn check-all` -> pass (exit 0), including all host gates and
    the strict x86-64 desktop mode.
  - `./tools/finn test-desktop` -> pass, QEMU status 33; log includes
    `FINNOS:GPU:VIRTIO_CONTROL_QUERY_COMPLETED scanouts=1` and
    `FINNOS:GPU:VIRTIO_2D_PRESENTATION_COMPLETED resource=1 commands=5`.
  - `./tools/finn test-desktop --target arm64-qemu` -> pass, QEMU status 0;
    log includes `FINNOS:GPU:VIRTIO_CONTROL_QUERY_COMPLETED_SCANOUTS=1`,
    `FINNOS:GPU:VIRTIO_2D_PRESENTATION_COMMANDS=5`, and
    `FINNOS:GPU:VIRTIO_2D_PRESENTATION_COMPLETED`.
  - Independent x86 QEMU trace was captured with
    `-trace 'enable=virtio_gpu_cmd_*'` at
    `/tmp/finnos-gpu-trace.MUaYeq/virtio-gpu.trace`.
  - Independent ARM64 QEMU trace was captured with
    `-trace 'enable=virtio_gpu_cmd_*'` at
    `/tmp/finnos-gpu-trace.CY2XBQ/virtio-gpu.trace`.
  - Both traces contain the post-query sequence
    `res_create_2d`, `res_back_attach`, `res_xfer_toh_2d`, `set_scanout`, and
    `res_flush`; the paths are temporary and the excerpts are summarized here
    because generated build/trace artifacts are not committed.
  - Final host: Rust 1.98.1, Python 3.9.6, QEMU 11.1.1; exact images/logs
    are under `build/out/x86_64-qemu-desktop/` and
    `build/out/arm64-qemu-desktop/`.
- Results and evidence classification:
  - Verified: modern VirtIO-PCI capability discovery, bounded BAR mapping,
    feature negotiation, queue 0 setup, direct request/response descriptor
    chains, a real `GET_DISPLAY_INFO` response, and the bounded five-command
    2D resource/backing/transfer/scanout/flush sequence in QEMU on both
    architectures.
  - Verified independently by QEMU device traces: all five 2D command
    classes are received after the control query.
  - Verified: software-rendered Peony desktop still passes both guest smoke
    modes and remains the fallback.
  - Implemented-unverified: continuous compositor-to-GPU presentation,
    hardware cursor, VirGL/3D execution, IRQ-driven completion, latency,
    60 Hz behavior, and physical-device behavior.
  - Unsupported/unverified: physical GPU or physical OS hardware, broad driver breadth, networking, USB/input, filesystems beyond the existing VFS/block slice, packages, updates, and release qualification.
- Documentation/status changes: Replaced stale “GPU transport not implemented” language with the exact narrow claim and documented remaining gaps. No roadmap item was promoted to complete and no physical-hardware or release claim was made.
- Unverified assumptions: QEMU's modern VirtIO-GPU behavior is representative
  only for these bounded control/2D commands; event-index notification
  suppression and completion are polled in this smoke path; the static smoke
  buffer remains kernel-owned and single-threaded; the framebuffer is not yet
  an owned GPU resource managed by a lifecycle/revocation service.
- Remaining work:
  1. Replace the static smoke buffer with owned GPU resource allocation and
     connect Peony damage regions to transfer/flush/scanout presentation.
  2. Add device IRQ/MSI routing and replace bounded polling with a
     lifecycle-safe completion path.
  3. Connect VirtIO input and PS/2 keyboard polling to compositor/terminal
     routing.
  4. Add persistent filesystem, package/runtime services, networking, and
     reference hardware qualification in roadmap order.
- Blockers: No physical hardware is connected; no general resource broker or restartable userspace driver model exists; no IRQ routing exists; the requested full desktop-class OS scope is substantially beyond this bounded slice.
- Risks/regressions to watch: Event-index ring geometry, descriptor direction/order, physical-address translation, queue reset/reuse, completion polling bounds, and accidental promotion of packet-level GPU work into acceleration claims. The software desktop fallback must remain functional if the GPU transport is absent or fails.
- Current Git state: Dirty worktree on `main` at `468ed41530ef598fe70019bd90edd47327bc4b74`; no commit, push, PR, or issue update was made. Existing user changes in the worktree were preserved.
- Suggested next action: Implement the owned GPU resource broker/display
  service, connect Peony damage to it, and add framebuffer hash plus
  input-to-present latency evidence on both QEMU guests.
- Skills next agent must load: finnos-operating-rules, repository-orientation, task-planning, test-strategy, qemu-boot-testing, driver-architecture, virtio, graphics-architecture, compositor-window-system, documentation-maintenance, agent-handoff.
