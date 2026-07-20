# FinnOS Agent State

```yaml
verified_date: 2026-07-20
verified_base_commit: 93ddf9d
working_tree_dirty: true
working_tree_context: PR #17 conflict resolution merges current main and locally verifies the x86 preemption-context foundation plus all current ARM64 QEMU modes
maturity: "Level 0 met for x86-64; Level 1 partial"
executable_architectures: [x86_64, arm64_serial_entry]
current_critical_milestone: "M1 Dual-Architecture Boot"
active_github_work: ["PR #17 kernel/preemption-context pending updated branch CI"]
```

- x86-64 QEMU `q35` with OVMF builds and boots through memory, paging, heap, BSP xAPIC timer, cooperative ring-0 tasks, framebuffer diagnostic, and idle.
- Target/profile configuration drives x86-64 development/release and ARM64 R3 artifacts; x86 profiles and ARM64 development serial entry are locally boot-verified.
- ARM64 R3 and R4.1-R4.4 are integrated. Local regression verifies EL1 exceptions, strict v3 handoff copying, protected memory classification, early allocation, a guarded FinnOS-owned TTBR0 regime with four exact hardware abort probes, and a pinned single-BSP GICv2 self-SGI lifecycle; generic-timer/task/external-routing parity is absent.
- PR #17 adds a locally verified x86-64 preemption-ready interrupt boundary: complete ring-0 return frames, stack-derived task attribution, bounded nested guards, and deferred requests. It deliberately does not switch from the timer ISR and is not actual preemption.
- No userspace, isolated process, syscall system, implemented IPC/capability model, general driver stack, persistent storage, network stack, or graphical environment exists.
- GOP fill is not Peony. Peony remains planned. Physical hardware support is unverified and unsupported.
- Major blockers: ARM64 timer/task parity, unparsed ACPI/device IRQ path, cooperative-only scheduling, and no user isolation.
- Immediate priorities are merging PR #17 after CI, then implementing actual preemptible blocking semantics and ARM64 generic-timer/task-context parity without broadening the ISR boundary.

Authority: [`STATUS.md`](../STATUS.md), [`ROADMAP.md`](../ROADMAP.md), [`ARCHITECTURE.md`](../ARCHITECTURE.md), and [`docs/audit/2026-07-16.md`](../docs/audit/2026-07-16.md). Reverify before relying on this summary after `HEAD` changes.
