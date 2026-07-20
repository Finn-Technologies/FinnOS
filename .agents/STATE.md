# FinnOS Agent State

```yaml
verified_date: 2026-07-20
verified_base_commit: f74fc49
working_tree_dirty: true
working_tree_context: R4.1 exceptions, R4.2 early memory, R4.3 owned MMU, and R4.4 pinned GICv2 are locally verified; integration CI is pending
maturity: "Level 0 met for x86-64; Level 1 partial"
executable_architectures: [x86_64, arm64_serial_entry]
current_critical_milestone: "M1 Dual-Architecture Boot"
active_github_work: ["current R4.1-R4.4 worktree pending draft PR"]
```

- x86-64 QEMU `q35` with OVMF builds and boots through memory, paging, heap, BSP xAPIC timer, cooperative ring-0 tasks, framebuffer diagnostic, and idle.
- Target/profile configuration drives x86-64 development/release and ARM64 R3 artifacts; x86 profiles and ARM64 development serial entry are locally boot-verified.
- ARM64 R3 serial entry is integrated. The current R4.1-R4.4 worktree locally verifies EL1 exceptions, strict handoff copying, protected memory classification, early allocation, a guarded FinnOS-owned TTBR0 regime with four exact hardware abort probes, and a pinned single-BSP GICv2 self-SGI lifecycle; worktree CI is pending and generic-timer/task/external-routing parity is absent.
- No userspace, isolated process, syscall system, implemented IPC/capability model, general driver stack, persistent storage, network stack, or graphical environment exists.
- GOP fill is not Peony. Peony remains planned. Physical hardware support is unverified and unsupported.
- Major blockers: ARM64 parity, unparsed ACPI/device IRQ path, cooperative-only scheduling, and no user isolation.
- Immediate priorities are integrating R4.1-R4.4, then building ARM64 generic-timer and task-context parity; preemption remains a separate later milestone.

Authority: [`STATUS.md`](../STATUS.md), [`ROADMAP.md`](../ROADMAP.md), [`ARCHITECTURE.md`](../ARCHITECTURE.md), and [`docs/audit/2026-07-16.md`](../docs/audit/2026-07-16.md). Reverify before relying on this summary after `HEAD` changes.
