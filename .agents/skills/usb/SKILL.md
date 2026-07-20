---
name: "usb"
title: "USB"
version: 1
status: "active"
owners: []
triggers: ["USB","xHCI","HID","hotplug"]
prerequisites: ["task-planning","threat-modelling","unsafe-rust-low-level-safety"]
related_docs: ["HARDWARE_SUPPORT.md","ROADMAP.md"]
category: "Drivers and hardware"
conditional_skills: ["driver-architecture","input-devices","pci-pcie","storage-block-devices"]
implementation_gates: ["Defer until stable virtual input/storage and selected physical hardware make USB necessary."]
related_milestones: ["post-1.0"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on USB, xHCI, HID; provides the FinnOS-specific usb workflow and evidence gates."
---
# USB

## 1. Name

`usb` (Drivers and hardware; skill maturity: planning).

## 2. Purpose

Control post-critical-path host-controller, enumeration, transfer, HID/storage, hotplug, power, and recovery scope.

## 3. When to use this skill

Use for requests mentioning `USB`, `xHCI`, `HID`, `hotplug`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

Defer until stable virtual input/storage and selected physical hardware make USB necessary. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `task-planning`
- `threat-modelling`
- `unsafe-rust-low-level-safety`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- pci-pcie and driver-architecture for a selected controller
- storage-block-devices or input-devices for a selected class

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- Defer until stable virtual input/storage and selected physical hardware make USB necessary.

## 6. Authoritative repository references

- `HARDWARE_SUPPORT.md`
- `ROADMAP.md`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

No USB stack or controller driver exists; USB is explicitly not an early critical-path dependency.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed usb result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Require an approved hardware use case
2. Choose one controller family after PCI/DMA works
3. Model device/config/interface/endpoint lifetime
4. Bound transfer descriptors and timeouts
5. Test disconnect, stall, malformed descriptors, and reset
6. Run the narrow regression, then all required subsystem/repository checks.
7. Update canonical docs/status and finish the agent-handoff template.

## 11. Repository-specific commands

```bash
./tools/finn check
```

Run commands from the repository root. A command listed here is a baseline/gate, not evidence that absent future functionality has a runnable target.

## 12. Architecture considerations

State shared semantics explicitly; isolate x86-64 and ARM64 mechanisms.

State guest architecture separately from host architecture and emulator model. Maintain a parity row for changed semantics and document intentional differences.

## 13. Safety constraints

Preserve the boundary described by the current state: No USB stack or controller driver exists; USB is explicitly not an early critical-path dependency. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Test disconnect, stall, malformed descriptors, and reset` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Require an approved hardware use case` before relying on downstream assumptions.
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
- Ignoring this skill's current constraint: No USB stack or controller driver exists; USB is explicitly not an early critical-path dependency.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: Defer until stable virtual input/storage and selected physical hardware make USB necessary.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on USB." Correct response: begin by `require an approved hardware use case`, then `choose one controller family after pci/dma works`, and require evidence for `test disconnect, stall, malformed descriptors, and reset` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
