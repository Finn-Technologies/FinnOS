---
name: "lowercase-hyphen-name"
title: "Human Title"
version: 1
status: "active"
owners: []
triggers: ["literal trigger"]
prerequisites: ["finnos-operating-rules"]
related_docs: ["STATUS.md"]
related_milestones: ["M0 Reproducible Build"]
last_verified: {"base_commit":"<integrated-or-base-commit>","date":"YYYY-MM-DD","worktree_dirty":false,"context":"integrated revision or explicit worktree context"}
description: "Use when <literal trigger and workflow>."
---

# Human Title

## 1. Name

State name, category, and skill maturity.

## 2. Purpose

State the concrete FinnOS engineering outcome.

## 3. When to use this skill

List literal triggers and task boundaries.

## 4. When not to use this skill

Name adjacent skills and premature roadmap work.

## 5. Prerequisite skills

Separate skills that must be read from conditional skills and implementation gates.

## 6. Authoritative repository references

List existing canonical paths only.

## 7. Current FinnOS context

State implemented/absent status and verification provenance.

## 8. Required inputs

List issue, baseline, source, architecture, environment, and evidence inputs.

## 9. Expected outputs

Name specific source/test/document/report artifacts.

## 10. Step-by-step workflow

Provide ordered, executable investigation and implementation actions.

## 11. Repository-specific commands

```bash
<existing command or explicit statement that no command exists>
```

## 12. Architecture considerations

Map shared semantics to x86-64 and ARM64 or document intentional differences.

## 13. Safety constraints

Name concrete bounds, ownership, unsafe, privilege, concurrency, and rollback rules.

## 14. Testing requirements

Name negative, host, QEMU, architecture, fault, and aggregate evidence as applicable.

## 15. Documentation requirements

Name canonical documents and status language to update.

## 16. Review checklist

- [ ] Add skill-specific review gates.

## 17. Completion criteria

- [ ] Tie completion to explicit acceptance evidence.

## 18. Common failure modes

- List likely FinnOS-specific errors.

## 19. Forbidden shortcuts

- List unsafe or premature actions.

## 20. Handoff requirements

Name evidence, unknowns, blockers, Git state, and next skills.

## 21. Examples

Provide one correct and one incorrect task response.

## 22. Skill maintenance notes

Update `.agents/scripts/skill_registry.py` and regenerate; do not hand-edit generated skills.

This is an authoring guide. The registry renderer produces canonical skill files and fills repeated policy sections. Operational skills must override generic output, safety, test, command, and example text whenever the default would not let a clean agent execute the task.
