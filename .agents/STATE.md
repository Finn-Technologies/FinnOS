# FinnOS Agent State

```yaml
verified_date: 2026-07-16
verified_base_commit: d21a477
working_tree_dirty: true
working_tree_context: audit documentation and agent-system changes are uncommitted
maturity: "Level 0 met for x86-64; Level 1 partial"
executable_architectures: [x86_64]
current_critical_milestone: "M0 Reproducible Build"
active_github_work: ["issue #16", "PR #17"]
```

- x86-64 QEMU `q35` with OVMF builds and boots through memory, paging, heap, BSP xAPIC timer, cooperative ring-0 tasks, framebuffer diagnostic, and idle.
- Debug and release components compile, but the image wrapper is debug-only.
- ARM64 is planning metadata only: no executable loader, kernel, image, QEMU path, or CI.
- No userspace, isolated process, syscall system, implemented IPC/capability model, general driver stack, persistent storage, network stack, or graphical environment exists.
- GOP fill is not Peony. Peony remains planned. Physical hardware support is unverified and unsupported.
- Major blockers: target/profile build drift, ARM64 absence, unparsed ACPI/device IRQ path, cooperative-only scheduling, and no user isolation.
- Immediate priorities are roadmap R1/R2, resolution of #16/#17 without overstating preemption, CI evidence retention, cross-architecture contracts, and ARM64 serial first boot.

Authority: [`STATUS.md`](../STATUS.md), [`ROADMAP.md`](../ROADMAP.md), [`ARCHITECTURE.md`](../ARCHITECTURE.md), and [`docs/audit/2026-07-16.md`](../docs/audit/2026-07-16.md). Reverify before relying on this summary after `HEAD` changes.
