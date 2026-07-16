# GitHub Planning Proposal

No GitHub metadata was changed during the audit. The authenticated token can administer the repository, but Projects V2 could not be inspected because it lacks `read:project`; active issue #16 and PR #17 were intentionally left untouched.

## Current cleanup

- Attach issue #16 directly to milestone `FinnOS Kernel Core`; remove duplicate `milestone:kernel-core`.
- Remove stale `status:planned`/`status:in-progress` labels from closed issues and use issue state/project Status instead.
- Do not merge, close, or relabel PR #17 until its preemption-context scope is reviewed.
- Close the Kernel Core milestone only when its stated scheduler exit criteria are reconciled with #16.
- Fix the repository documentation URL from `master/docs` to `main/docs`.
- Keep the verified strict `foundation`/`smoke`, admin enforcement, conversation resolution, and force-push/deletion protections; raise the current zero required approvals when a second reviewer is available.

## Proposed hierarchy

Use milestones for maturity delivery, tracking issues for subsystem outcomes, and child issues only for independently testable work. The 20 detailed issue-ready work items are in [ROADMAP.md](../../ROADMAP.md). Start with the first ten; do not create hundreds of speculative issues.

- Epic: Reproducible Build and Loader Hardening (R1-R2)
- Epic: Dual-Architecture Boot Baseline (R3-R5)
- Epic: Core Kernel and Isolation (R6-R8)
- Epic: Userspace Foundation (R9)
- Epic: Virtual Devices and Persistent Storage (R10-R12)
- Epic: Peony Graphical Foundation (R13-R14)
- Epic: Developer Preview and Release Security (R15-R17)
- Epic: Supported Hardware and Stable 1.0 (R18-R19)

Every issue body should copy the roadmap fields: problem/current behavior, desired behavior, reason, scope, non-goals, dependencies, architecture, implementation notes, acceptance, verification, documentation, files, complexity, milestone, and labels.

Files in this directory provide machine-readable proposals:

- [`labels.yml`](labels.yml)
- [`milestones.yml`](milestones.yml)
- [`issues.yml`](issues.yml)
- [`project.md`](project.md)
