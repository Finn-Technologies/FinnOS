# FinnOS Agent Operating System

This directory is the repository-local engineering memory and operating procedure for coding agents. It explains how to work; FinnOS source, tests, and authoritative project documents establish what exists and what is intended.

The latest clean-entry usability exercise and resolved ambiguities are recorded in [`SIMULATION.md`](SIMULATION.md).

## Mandatory Entry Protocol

Every substantial task starts in this order:

1. Read this file and [`STATE.md`](STATE.md).
2. Read the relevant rows in [`STATUS.md`](../STATUS.md) and critical path in [`ROADMAP.md`](../ROADMAP.md).
3. Inspect `git status --short --branch`, `git log --oneline -10`, and relevant open issues/PRs when GitHub is available.
4. Select the minimum relevant skills from [`MANIFEST.md`](MANIFEST.md), always including `finnos-operating-rules`, `repository-orientation`, `task-planning`, `test-strategy`, `documentation-maintenance`, and `agent-handoff` for implementation work.
5. Read every listed prerequisite skill. Do not load unrelated future-subsystem skills.
6. Inspect the implementation and reproduce the relevant baseline build/test before designing changes.
7. Record scope, non-goals, dependencies, acceptance evidence, and verification commands.
8. Implement only after the baseline and invariants are understood.
9. Run the narrow regression first, then required subsystem and repository gates.
10. Update behavior, status, architecture, test, and limitation documentation together.
11. Inspect the complete diff and leave a structured handoff using [`templates/handoff-template.md`](templates/handoff-template.md).

Fast start:

```bash
python3 .agents/scripts/capture_state.py
python3 .agents/scripts/validate.py --all
./tools/finn doctor
./tools/finn check
```

## Universal Rules

- Repository code and verified execution are the source of truth. Documentation, comments, TODOs, types, names, and filenames are not implementation evidence.
- Compilation does not prove boot. Boot does not prove a subsystem. QEMU support is not physical-hardware support. x86-64 support is not ARM64 support.
- A framebuffer fill is not a graphical environment. Planned Peony behavior is not implemented UI. Cooperative ring-0 tasks are not userspace processes or a production scheduler.
- Never mark roadmap work complete or alter percentages without acceptance evidence. Never invent dates without contributor-capacity evidence.
- Never expose credentials, commit generated build artifacts, weaken tests to pass, or remove safety checks without a tested replacement.
- Never introduce an architecture abstraction without mapping its semantics to x86-64 and ARM64. Intentional differences are allowed and must be documented.
- Never begin broad rewrites before identifying current invariants. UI work cannot bypass userspace, IPC, storage, input, and display dependencies.
- Preserve exact commands, versions, logs, artifacts, failures, unverified assumptions, and reproduction evidence.
- Update relevant documentation whenever behavior changes. Every substantial session ends with a structured handoff.

## Authority Order

When instructions conflict, resolve in this order:

1. Executed tests and current source code.
2. [`STATUS.md`](../STATUS.md) for verified maturity and [`ROADMAP.md`](../ROADMAP.md) for dependency order.
3. [`ARCHITECTURE.md`](../ARCHITECTURE.md), canonical `docs/architecture/`, accepted ADRs, and protocol references.
4. Build, testing, porting, security, hardware, UI, and release documents.
5. `.agents/` operating instructions.
6. Historical audits and issue descriptions.

Stop and report a contradiction rather than choosing the more convenient claim. Skills must be corrected when their workflow or current-state summary diverges from higher-authority evidence.

## Skill Selection

`MANIFEST.md` lists purpose, triggers, prerequisites, outputs, subsystem, milestone, dependencies, and maturity for every skill. Skills use YAML front matter documented in [`GOVERNANCE.md`](GOVERNANCE.md). A skill's `When not to use` and prerequisite gates are mandatory scope controls.

Examples:

- ARM64 first boot: orientation, planning, build environment/orchestration, ARM64 platform, cross-architecture design, UEFI, QEMU, tests, docs, handoff.
- Scheduler work: scheduler, synchronization, interrupts, timer, architecture platform/context, unsafe Rust, reliability, tests, docs, handoff.
- Peony widget: graphics architecture, Peony design system/toolkit, text/localization, UI review, tests, docs, handoff. It is currently blocked by roadmap prerequisites.

## Templates, Checklists, and Runbooks

- Planning/investigation: [`task-plan-template.md`](templates/task-plan-template.md), [`investigation-template.md`](templates/investigation-template.md), and [`architecture-decision-template.md`](templates/architecture-decision-template.md).
- Delivery: [`implementation-report-template.md`](templates/implementation-report-template.md), [`issue-template.md`](templates/issue-template.md), and [`handoff-template.md`](templates/handoff-template.md).
- Every task: [`repository-entry.md`](checklists/repository-entry.md), [`pre-change.md`](checklists/pre-change.md), and [`pre-commit.md`](checklists/pre-commit.md).
- Conditional review: [`kernel-change.md`](checklists/kernel-change.md), [`architecture-change.md`](checklists/architecture-change.md), [`driver-change.md`](checklists/driver-change.md), [`security-review.md`](checklists/security-review.md), [`ui-review.md`](checklists/ui-review.md), [`documentation-review.md`](checklists/documentation-review.md), and [`release-readiness.md`](checklists/release-readiness.md).
- Current executable runbook: [`x86_64-debug-boot.md`](runbooks/x86_64-debug-boot.md).

## Task State Model

| State | Required evidence |
|---|---|
| Untriaged | Request exists; scope and owner are unknown |
| Investigating | Repository state, implementation, issue history, and baseline are being collected |
| Planned | Dependencies, non-goals, acceptance criteria, risks, and commands are written |
| Implementing | Approved scoped source/test/document changes are in progress |
| Locally Verified | Required local commands passed with logs/results recorded |
| Review Ready | Diff is scoped, docs are current, handoff/PR evidence is complete |
| Integrated | Change is merged into the protected branch; not automatically runtime-verified |
| Fully Verified | Integrated revision passes required CI/runtime/hardware evidence for its claim |
| Blocked | A named dependency or unavailable resource prevents an acceptance criterion |
| Rejected | Investigation found the approach unsafe, incorrect, or out of scope |
| Deferred | Valid work intentionally moved behind a dependency or later milestone |
| Superseded | A newer issue, ADR, or implementation replaces this work |

Code written is still `Implementing`. A merged QEMU-only driver is not `Fully Verified` for physical hardware.

## Updating the System

Follow [`GOVERNANCE.md`](GOVERNANCE.md). Modify the registry in `.agents/scripts/skill_registry.py`, render skills and the manifest, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Report stale instructions as documentation issues or fix them with the code change that invalidated them.

## Handoff

A handoff names the objective, baseline, work, files, tests/results, unverified assumptions, remaining work, blockers, risks, Git state, next action, and skills to load. Generate a skeleton with:

```bash
python3 .agents/scripts/new_handoff.py --output <task>-YYYY-MM-DD.md
```

This writes only under `.agents/handoffs/` and refuses overwrites. Use stdout mode for a final response that already contains the complete handoff. Do not claim a commit, push, issue update, or integration unless it actually occurred.
