---
name: "runtime-standard-library"
title: "Runtime and Standard Library"
version: 1
status: "active"
owners: []
triggers: ["runtime","standard library","startup code","userspace ABI"]
prerequisites: ["task-planning","cross-architecture-design","test-strategy"]
related_docs: ["docs/development/application-development.md","ROADMAP.md"]
category: "Userspace and platform"
conditional_skills: ["executable-loader","ipc-capabilities","syscall-development"]
implementation_gates: ["Requires a running isolated process and minimal syscall/IPC ABI."]
related_milestones: ["M3 Userspace Foundation","M6 Developer Preview"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on runtime, standard library, startup code; provides the FinnOS-specific runtime and standard library workflow and evidence gates."
---
# Runtime and Standard Library

## 1. Name

`runtime-standard-library` (Userspace and platform; skill maturity: planning-gated).

## 2. Purpose

Build architecture-neutral userspace startup, allocation, threads, files, time, IPC, and errors on FinnOS ABI.

## 3. When to use this skill

Use for requests mentioning `runtime`, `standard library`, `startup code`, `userspace ABI`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

Requires a running isolated process and minimal syscall/IPC ABI. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `task-planning`
- `cross-architecture-design`
- `test-strategy`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- syscall-development, ipc-capabilities, and executable-loader during implementation

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- Requires a running isolated process and minimal syscall/IPC ABI.

## 6. Authoritative repository references

- `docs/development/application-development.md`
- `ROADMAP.md`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

No userspace ABI, runtime, standard library, headers/bindings, or SDK exists.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed runtime and standard library result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Freeze only the minimal proven syscall/IPC surface
2. Add per-architecture startup stubs
3. Define stable error/result conventions
4. Wrap allocation/time/files/threads without bypassing rights
5. Run runtime tests in isolated processes on both ports
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

Preserve the boundary described by the current state: No userspace ABI, runtime, standard library, headers/bindings, or SDK exists. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Run runtime tests in isolated processes on both ports` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Freeze only the minimal proven syscall/IPC surface` before relying on downstream assumptions.
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
- Ignoring this skill's current constraint: No userspace ABI, runtime, standard library, headers/bindings, or SDK exists.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: Requires a running isolated process and minimal syscall/IPC ABI.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on runtime." Correct response: begin by `freeze only the minimal proven syscall/ipc surface`, then `add per-architecture startup stubs`, and require evidence for `run runtime tests in isolated processes on both ports` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
