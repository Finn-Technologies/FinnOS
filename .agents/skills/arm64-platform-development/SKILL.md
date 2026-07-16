---
name: "arm64-platform-development"
title: "ARM64 Platform Development"
version: 1
status: "active"
owners: []
triggers: ["arm64","aarch64","AAVMF","GIC","exception vectors"]
prerequisites: ["build-orchestration","uefi-bootloader-development","cross-architecture-design","qemu-boot-testing","unsafe-rust-low-level-safety"]
related_docs: ["PORTING.md","build/targets/arm64-qemu.toml","Finnfile.toml"]
category: "Boot and kernel"
conditional_skills: []
implementation_gates: ["R3 serial first boot is blocked by roadmap R1 build target/profile orchestration; no ARM command, qemu-system-aarch64/AAVMF resolver, ARM override, or marker/termination contract exists. R4 parity starts only after R3 serial entry is verified."]
related_milestones: ["M1 Dual-Architecture Boot"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on arm64, aarch64, AAVMF; provides the FinnOS-specific arm64 platform development workflow and evidence gates."
---
# ARM64 Platform Development

## 1. Name

`arm64-platform-development` (Boot and kernel; skill maturity: planning-gated).

## 2. Purpose

Bring up AArch64 UEFI/QEMU mechanisms while maintaining semantic parity with x86.

## 3. When to use this skill

Use for requests mentioning `arm64`, `aarch64`, `AAVMF`, `GIC`, `exception vectors`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

R3 serial first boot is blocked by roadmap R1 build target/profile orchestration; no ARM command, qemu-system-aarch64/AAVMF resolver, ARM override, or marker/termination contract exists. R4 parity starts only after R3 serial entry is verified. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `build-orchestration`
- `uefi-bootloader-development`
- `cross-architecture-design`
- `qemu-boot-testing`
- `unsafe-rust-low-level-safety`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- None.

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- R3 serial first boot is blocked by roadmap R1 build target/profile orchestration; no ARM command, qemu-system-aarch64/AAVMF resolver, ARM override, or marker/termination contract exists. R4 parity starts only after R3 serial entry is verified.

## 6. Authoritative repository references

- `PORTING.md`
- `build/targets/arm64-qemu.toml`
- `Finnfile.toml`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

ARM64 has no executable implementation: no BOOTAA64.EFI, kernel entry, MMU, vectors, GIC, timer, UART, context, image, QEMU command, or CI.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed arm64 platform development result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Clear R1: target/profile data must drive build, image, run/test, artifacts, invalid-combination errors, and local/CI parity
2. For R3 add BOOTAA64, matching kernel/linker/entry/UART, QEMU virt plus AAVMF resolution, and bounded ordered loader/kernel-entry markers
3. End R3 at deterministic serial kernel entry with retained manifest/artifacts/log and existing x86 gates green
4. For R4 only, add exception vectors, MMU/MAIR/TCR/TTBR, GIC, generic timer, and AAPCS64 task context parity
5. Define ARM-specific timeout/termination evidence and environment overrides rather than copying x86 isa-debug-exit or OVMF variables
6. Run the narrow regression, then all required subsystem/repository checks.
7. Update canonical docs/status and finish the agent-handoff template.

## 11. Repository-specific commands

```bash
python3 .agents/scripts/validate.py --all
./tools/finn check
```

Run commands from the repository root. A command listed here is a baseline/gate, not evidence that absent future functionality has a runnable target.

## 12. Architecture considerations

Use AAPCS64, EL0/EL1, VBAR, translation-table, GIC, and barrier semantics; never transliterate x86 registers.

State guest architecture separately from host architecture and emulator model. Maintain a parity row for changed semantics and document intentional differences.

## 13. Safety constraints

Preserve the boundary described by the current state: ARM64 has no executable implementation: no BOOTAA64.EFI, kernel entry, MMU, vectors, GIC, timer, UART, context, image, QEMU command, or CI. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Define ARM-specific timeout/termination evidence and environment overrides rather than copying x86 isa-debug-exit or OVMF variables` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Clear R1: target/profile data must drive build, image, run/test, artifacts, invalid-combination errors, and local/CI parity` before relying on downstream assumptions.
- [ ] The observed baseline and first failing/divergent point are recorded.
- [ ] Every prerequisite and roadmap dependency is satisfied or explicitly marked blocked.
- [ ] Bounds, ownership, rollback, error paths, and resource limits were reviewed.
- [ ] x86-64 and ARM64 semantics are mapped without unsupported parity claims.
- [ ] Negative/failure tests exist at the layer that owns the behavior.
- [ ] Commands, logs, artifact/profile/architecture, and results are preserved.
- [ ] Canonical documentation and affected agent skills agree with behavior.

## 17. Completion criteria

- [ ] All stated outputs exist and remain within the task's scope/non-goals.
- [ ] The narrow regression and required repository/subsystem gates pass.
- [ ] Acceptance criteria are tied to source, test, runtime, or hardware evidence as appropriate.
- [ ] Unknowns and unsupported environments remain explicit; no planned feature is promoted.
- [ ] The final diff is reviewed and a structured handoff is complete.

## 18. Common failure modes

- Starting from an audit statement instead of re-reading changed source and tests.
- Using a successful compile or marker as evidence for a broader subsystem claim.
- Ignoring this skill's current constraint: ARM64 has no executable implementation: no BOOTAA64.EFI, kernel entry, MMU, vectors, GIC, timer, UART, context, image, QEMU command, or CI.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: R3 serial first boot is blocked by roadmap R1 build target/profile orchestration; no ARM command, qemu-system-aarch64/AAVMF resolver, ARM override, or marker/termination contract exists. R4 parity starts only after R3 serial entry is verified.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on arm64." Correct response: begin by `clear r1: target/profile data must drive build, image, run/test, artifacts, invalid-combination errors, and local/ci parity`, then `for r3 add bootaa64, matching kernel/linker/entry/uart, qemu virt plus aavmf resolution, and bounded ordered loader/kernel-entry markers`, and require evidence for `define arm-specific timeout/termination evidence and environment overrides rather than copying x86 isa-debug-exit or ovmf variables` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
