---
name: "text-fonts-localization"
title: "Text, Fonts, and Localization"
version: 1
status: "active"
owners: []
triggers: ["font","text shaping","Unicode","localization","bidi"]
prerequisites: ["graphics-architecture","ui-ux-review","reliability-fault-injection"]
related_docs: ["UI_GUIDELINES.md","docs/architecture/peony.md"]
category: "Graphics and Peony"
conditional_skills: ["input-devices"]
implementation_gates: ["Requires graphics buffers/toolkit foundations."]
related_milestones: ["M5 Graphical Stack"]
last_verified: {"base_commit":"d21a477","date":"2026-07-16","worktree_dirty":true,"context":"base commit plus uncommitted audit and agent-system worktree"}
description: "Use when working on font, text shaping, Unicode; provides the FinnOS-specific text, fonts, and localization workflow and evidence gates."
---
# Text, Fonts, and Localization

## 1. Name

`text-fonts-localization` (Graphics and Peony; skill maturity: planning-gated).

## 2. Purpose

Implement licensed font loading, rasterization, shaping, Unicode/bidi/fallback, scaling, input methods, and caches.

## 3. When to use this skill

Use for requests mentioning `font`, `text shaping`, `Unicode`, `localization`, `bidi`, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

Requires graphics buffers/toolkit foundations. Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

- `graphics-architecture`
- `ui-ux-review`
- `reliability-fault-injection`

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

- input-devices for input-method integration

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

- Requires graphics buffers/toolkit foundations.

## 6. Authoritative repository references

- `UI_GUIDELINES.md`
- `docs/architecture/peony.md`

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

No font, text renderer, shaping, localization, input method, or accessibility runtime exists.

Registry verification used base commit `d21a477` plus the dirty worktree context "base commit plus uncommitted audit and agent-system worktree" on 2026-07-16. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

An evidence-backed text, fonts, and localization result with scoped artifacts, tests, documentation, and handoff. Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

1. Select licensed fonts and Unicode/shaping library strategy
2. Bound parser/cache inputs
3. Implement script/bidi/fallback/line-break tests
4. Support 1x/1.5x/2x and 200% text
5. Test RTL, mixed direction, missing glyphs, and 30% expansion
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

Preserve the boundary described by the current state: No font, text renderer, shaping, localization, input method, or accessibility runtime exists. Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

Make `Test RTL, mixed direction, missing glyphs, and 30% expansion` observable with a negative case, then run the narrow and aggregate gates. Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

- [ ] The workflow proves `Select licensed fonts and Unicode/shaping library strategy` before relying on downstream assumptions.
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
- Ignoring this skill's current constraint: No font, text renderer, shaping, localization, input method, or accessibility runtime exists.
- Changing shared policy while testing only one architecture or one happy path.
- Losing the exact failing artifact/log by rebuilding before capture.

## 19. Forbidden shortcuts

- Do not weaken, delete, reorder, or broaden validators merely to make a test pass.
- Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.
- Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.
- Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.
- Do not disregard the roadmap/status gate: Requires graphics buffers/toolkit foundations.

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on font." Correct response: begin by `select licensed fonts and unicode/shaping library strategy`, then `bound parser/cache inputs`, and require evidence for `test rtl, mixed direction, missing glyphs, and 30% expansion` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
