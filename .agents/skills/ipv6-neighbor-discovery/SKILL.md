---
name: "ipv6-neighbor-discovery"
title: "IPv6 and Neighbor Discovery"
version: 1
status: "active"
owners: []
triggers: ["IPv6","NDP","SLAAC","ICMPv6"]
prerequisites: ["ethernet-arp-ipv4-icmp","network-security","timer-timekeeping"]
related_docs: ["ROADMAP.md","SECURITY.md"]
category: "Networking"
conditional_skills: []
implementation_gates: ["Defer until bounded IPv4/UDP networking is reliable and threat-reviewed."]
related_milestones: ["M7 Beta"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on IPv6, NDP, SLAAC; provides the FinnOS-specific ipv6 and neighbor discovery workflow and evidence gates."
---
# IPv6 and Neighbor Discovery

## 1. Name

`ipv6-neighbor-discovery` (Networking; skill maturity: planning).

## 2. Purpose

Add bounded IPv6, extension-header, ICMPv6, NDP, SLAAC, and route handling after IPv4 baseline.

## 3. When to use this skill

Use for requests mentioning `IPv6`, `NDP`, `SLAAC`, `ICMPv6`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

Defer until bounded IPv4/UDP networking is reliable and threat-reviewed. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `ethernet-arp-ipv4-icmp`
- `network-security`
- `timer-timekeeping`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- None.

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- Defer until bounded IPv4/UDP networking is reliable and threat-reviewed.

## 6. Authoritative repository references

- `ROADMAP.md`
- `SECURITY.md`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

No network stack exists; IPv6 is beyond the first basic-network acceptance scope.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed ipv6 and neighbor discovery result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Bound extension-header chain and payload lengths
2. Implement address/scope/route rules
3. Validate ICMPv6 and NDP options
4. Cap neighbor/SLAAC state and timers
5. Test interoperability, spoofing, fragments, and malformed chains
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

Preserve the boundary described by the current state: No network stack exists; IPv6 is beyond the first basic-network acceptance scope. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Test interoperability, spoofing, fragments, and malformed chains` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Bound extension-header chain and payload lengths` before relying on downstream assumptions.
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
- Ignoring this skill's current constraint: No network stack exists; IPv6 is beyond the first basic-network acceptance scope.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: Defer until bounded IPv4/UDP networking is reliable and threat-reviewed.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on IPv6." Correct response: begin by `bound extension-header chain and payload lengths`, then `implement address/scope/route rules`, and require evidence for `test interoperability, spoofing, fragments, and malformed chains` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
