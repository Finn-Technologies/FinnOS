---
name: "fixing-boot-failure"
title: "Fixing a Boot Failure"
version: 1
status: "active"
owners: []
triggers: ["boot failure","QEMU timeout","missing marker"]
prerequisites: ["debugging-investigation","qemu-boot-testing","unsafe-rust-low-level-safety","agent-handoff"]
related_docs: ["docs/development/debugging.md","tools/finnlib/qemu.py","docs/architecture/boot.md"]
category: "Execution workflows"
conditional_skills: ["interrupt-exception-handling","uefi-bootloader-development","virtual-memory"]
implementation_gates: []
related_milestones: ["M0 Reproducible Build","M1 Dual-Architecture Boot"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on boot failure, QEMU timeout, missing marker; provides the FinnOS-specific fixing a boot failure workflow and evidence gates."
---
# Fixing a Boot Failure

## 1. Name

`fixing-boot-failure` (Execution workflows; skill maturity: operational).

## 2. Purpose

Use a staged decision tree to localize image, firmware, loader, handoff, entry, paging, exception, emulator, or harness failures.

## 3. When to use this skill

Use for requests mentioning `boot failure`, `QEMU timeout`, `missing marker`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

Do not use this skill as a substitute for its adjacent subsystem skills or for evidence that the subsystem works. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `debugging-investigation`
- `qemu-boot-testing`
- `unsafe-rust-low-level-safety`
- `agent-handoff`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- uefi-bootloader-development for loader/handoff failures
- virtual-memory for paging failures
- interrupt-exception-handling for trap failures

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- No additional gate beyond the selected roadmap acceptance criteria.

## 6. Authoritative repository references

- `docs/development/debugging.md`
- `tools/finnlib/qemu.py`
- `docs/architecture/boot.md`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

Current x86 marker sequence is validated; ARM boot does not exist.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed fixing a boot failure result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Verify image files/manifest
2. Verify firmware/QEMU command
3. Classify last loader marker
4. Validate BootInfo and kernel entry
5. Inspect CR3/mappings/exceptions
6. Check harness status/ordering/timeout
7. Add regression at owning layer
8. Run the narrow regression, then all required subsystem/repository checks.
9. Update canonical docs/status and finish the agent-handoff template.

## 11. Repository-specific commands

```bash
./tools/finn check
```

Run commands from the repository root. A command listed here is a baseline/gate, not evidence that absent future functionality has a runnable target.

## 12. Architecture considerations

State shared semantics explicitly; isolate x86-64 and ARM64 mechanisms.

State guest architecture separately from host architecture and emulator model. Maintain a parity row for changed semantics and document intentional differences.

## 13. Safety constraints

Preserve the boundary described by the current state: Current x86 marker sequence is validated; ARM boot does not exist. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Add regression at owning layer` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Verify image files/manifest` before relying on downstream assumptions.
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
- Ignoring this skill's current constraint: Current x86 marker sequence is validated; ARM boot does not exist.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: Do not use this skill as a substitute for its adjacent subsystem skills or for evidence that the subsystem works.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on boot failure." Correct response: begin by `verify image files/manifest`, then `verify firmware/qemu command`, and require evidence for `add regression at owning layer` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
