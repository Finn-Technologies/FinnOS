#!/usr/bin/env python3
"""Render standalone FinnOS SKILL.md files and MANIFEST.md from the registry."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from skill_registry import SKILLS

ROOT = Path(__file__).resolve().parents[2]
SKILLS_DIR = ROOT / ".agents" / "skills"
MANIFEST = ROOT / ".agents" / "MANIFEST.md"

CATEGORIES = [
    "Foundations", "Boot and kernel", "Drivers and hardware", "Userspace and platform",
    "Networking", "Graphics and Peony", "Security", "Quality, release, and docs",
    "Execution workflows",
]


def category(name: str) -> str:
    for spec in SKILLS:
        if spec["name"] == name:
            return spec["category"]
    raise ValueError(f"unknown skill: {name}")


def topological_order(names: list[str]) -> list[str]:
    graph = {spec["name"]: spec["prerequisites"] for spec in SKILLS}
    selected = set()

    def collect(name: str) -> None:
        if name in selected:
            return
        for dependency in graph[name]:
            collect(dependency)
        selected.add(name)

    for name in names:
        collect(name)
    ordered = []
    visited = set()

    def visit(name: str) -> None:
        if name in visited:
            return
        for dependency in graph[name]:
            if dependency in selected:
                visit(dependency)
        visited.add(name)
        ordered.append(name)

    for spec in SKILLS:
        if spec["name"] in selected:
            visit(spec["name"])
    return ordered


def yaml_value(value) -> str:
    return json.dumps(value, separators=(",", ":"))


def bullets(items, checkbox=False) -> str:
    prefix = "- [ ]" if checkbox else "-"
    return "\n".join(f"{prefix} {item}" for item in items)


def render_skill(spec: dict) -> str:
    description = (
        f"Use when working on {', '.join(spec['triggers'][:3])}; "
        f"provides the FinnOS-specific {spec['title'].lower()} workflow and evidence gates."
    )
    front = ["---"]
    metadata = {
        "name": spec["name"], "title": spec["title"], "version": spec["version"],
        "status": spec["status"], "owners": spec["owners"], "triggers": spec["triggers"],
        "prerequisites": spec["prerequisites"], "related_docs": spec["docs"],
        "category": spec["category"], "conditional_skills": spec["conditional_skills"],
        "implementation_gates": spec["implementation_gates"],
        "related_milestones": spec["milestones"],
        "last_verified": {
            "base_commit": spec["verified_commit"], "date": spec["verified_date"],
            "worktree_dirty": spec["verified_dirty"], "context": spec["verified_context"],
        },
        "description": description,
    }
    for key, value in metadata.items():
        front.append(f"{key}: {yaml_value(value)}")
    front.append("---")

    prereqs = bullets([f"`{item}`" for item in spec["prerequisites"]]) or "- None; this is the root policy skill."
    conditional = bullets(spec["conditional_notes"]) or "- None."
    gates = bullets(spec["implementation_gates"]) or "- No additional gate beyond the selected roadmap acceptance criteria."
    refs = bullets([f"`{item}`" for item in spec["docs"]])
    command_block = "\n".join(spec["commands"])
    block_text = spec["blocked"] or (
        "Do not use this skill as a substitute for its adjacent subsystem skills or for evidence that the subsystem works."
    )
    review = [
        f"The workflow proves `{spec['actions'][0]}` before relying on downstream assumptions.",
        "The observed baseline and first failing/divergent point are recorded.",
        "Every prerequisite and roadmap dependency is satisfied or explicitly marked blocked.",
        "Bounds, ownership, rollback, error paths, and resource limits were reviewed.",
        "x86-64 and ARM64 semantics are mapped without unsupported parity claims.",
        "Negative/failure tests exist at the layer that owns the behavior.",
        "Commands, logs, artifact/profile/architecture, and results are preserved.",
        "Canonical documentation and affected agent skills agree with behavior.",
    ]
    completion = [
        "All stated outputs exist and remain within the task's scope/non-goals.",
        "The narrow regression and required repository/subsystem gates pass.",
        "Acceptance criteria are tied to source, test, runtime, or hardware evidence as appropriate.",
        "Unknowns and unsupported environments remain explicit; no planned feature is promoted.",
        "The final diff is reviewed and a structured handoff is complete.",
    ]
    failures = [
        "Starting from an audit statement instead of re-reading changed source and tests.",
        "Using a successful compile or marker as evidence for a broader subsystem claim.",
        f"Ignoring this skill's current constraint: {spec['current']}",
        "Changing shared policy while testing only one architecture or one happy path.",
        "Losing the exact failing artifact/log by rebuilding before capture.",
    ]
    forbidden = [
        "Do not weaken, delete, reorder, or broaden validators merely to make a test pass.",
        "Do not claim ARM64, physical hardware, userspace, Peony, security, or release support without its own acceptance evidence.",
        "Do not bypass a named prerequisite by embedding a temporary incompatible abstraction.",
        "Do not commit target/, build/out/, firmware, credentials, local paths, or unreviewed generated output.",
        f"Do not disregard the roadmap/status gate: {block_text}",
    ]
    action_lines = "\n".join(f"{i}. {action}" for i, action in enumerate(spec["actions"], 1))
    example_trigger = spec["triggers"][0]
    verification_basis = (
        f'base commit `{spec["verified_commit"]}` plus the dirty worktree context '
        f'"{spec["verified_context"]}"'
        if spec["verified_dirty"]
        else f'clean revision `{spec["verified_commit"]}` with context "{spec["verified_context"]}"'
    )

    body = f"""
# {spec['title']}

## 1. Name

`{spec['name']}` ({category(spec['name'])}; skill maturity: {spec['maturity']}).

## 2. Purpose

{spec['purpose']}

## 3. When to use this skill

Use for requests mentioning {', '.join(f'`{item}`' for item in spec['triggers'])}, or when a dependency points to this skill. Load only after repository entry and before design or implementation.

## 4. When not to use this skill

{block_text} Do not load it only because a future FinnOS document mentions the subsystem.

## 5. Prerequisite skills

{prereqs}

Read the full prerequisite closure in topological dependency order. If one cannot be satisfied, move the task to `Blocked` or `Deferred`; do not omit the dependency.

Conditional skills:

{conditional}

Implementation gates are roadmap/runtime conditions, not additional documents to load automatically:

{gates}

## 6. Authoritative repository references

{refs}

Re-read implementation and tests referenced by those documents. The documents establish intent/status boundaries, not runtime proof.

## 7. Current FinnOS context

{spec['current']}

Registry verification used {verification_basis} on {spec['verified_date']}. This is not an integrated-revision claim. Reverify after HEAD, active PRs, or relevant source changes.

## 8. Required inputs

- User request or issue with desired outcome and architecture/profile scope.
- Current `git status`, recent history, active related issue/PR, and selected roadmap item.
- Relevant implementation, tests, invariants, ADRs, and exact baseline output.
- Toolchain/firmware/hardware versions when behavior crosses those boundaries.

## 9. Expected outputs

{spec['outputs']} Include acceptance evidence, residual limitations, and next dependency rather than only code.

## 10. Step-by-step workflow

{action_lines}
{len(spec['actions']) + 1}. Run the narrow regression, then all required subsystem/repository checks.
{len(spec['actions']) + 2}. Update canonical docs/status and finish the agent-handoff template.

## 11. Repository-specific commands

```bash
{command_block}
```

Run commands from the repository root. A command listed here is a baseline/gate, not evidence that absent future functionality has a runnable target.

## 12. Architecture considerations

{spec['architecture']}

State guest architecture separately from host architecture and emulator model. Maintain a parity row for changed semantics and document intentional differences.

## 13. Safety constraints

{spec['safety']} Apply `.agents/checklists/pre-change.md`; for kernel, driver, security, architecture, or UI work also apply the matching checklist.

## 14. Testing requirements

{spec['tests']} Do not permanently hard-code test counts; counts belong to dated evidence reports.

## 15. Documentation requirements

Update the canonical behavior/status document, relevant architecture/reference material, test/build instructions, limitations, and this skill registry if any command, gate, or current-context statement changes.

## 16. Review checklist

{bullets(review, checkbox=True)}

## 17. Completion criteria

{bullets(completion, checkbox=True)}

## 18. Common failure modes

{bullets(failures)}

## 19. Forbidden shortcuts

{bullets(forbidden)}

## 20. Handoff requirements

Use `.agents/templates/handoff-template.md`. Include objective, starting/final Git state, task state, skills used, files, exact commands/results, evidence classification, docs/status changes, unknowns, blockers, risks, next action, and next skills. Distinguish locally verified worktree changes from integrated behavior.

## 21. Examples

Request: "Work on {example_trigger}." Correct response: begin by `{spec['actions'][0].lower()}`, then `{spec['actions'][1].lower()}`, and require evidence for `{spec['actions'][-1].lower()}` before changing status. Incorrect response: create a plausible subsystem scaffold and mark the roadmap item complete because it compiles.

## 22. Skill maintenance notes

Canonical source: `.agents/scripts/skill_registry.py`. Increment `version` for material policy/workflow/gate changes, update `last_verified` only after reinspection, run `python3 .agents/scripts/render_skills.py`, review generated diffs, then run `python3 .agents/scripts/validate.py --all`. Follow `.agents/GOVERNANCE.md`; never hand-edit generated skills.
"""
    return "\n".join(front) + body


def render_manifest() -> str:
    lines = [
        "# FinnOS Agent Skill Manifest", "",
        "This is the progressive-disclosure index. Select the smallest skill set that covers the task, then load every prerequisite. All implementation work also uses `finnos-operating-rules`, `repository-orientation`, `task-planning`, `test-strategy`, `documentation-maintenance`, and `agent-handoff` unless the task is purely informational.", "",
        "Generated from `.agents/scripts/skill_registry.py`; do not hand-edit. Skill maturity describes instruction readiness, not subsystem implementation.", "",
    ]
    for group in CATEGORIES:
        lines.extend([
            f"## {group}", "",
            "| Skill | Purpose / triggers | Skill prerequisites | Conditional skills / implementation gates | Outputs | Milestones | Skill maturity |", "|---|---|---|---|---|---|---|",
        ])
        for spec in SKILLS:
            if category(spec["name"]) != group:
                continue
            link = f"[ `{spec['name']}` ](skills/{spec['name']}/SKILL.md)"
            triggers = ", ".join(spec["triggers"][:3])
            prereqs = ", ".join(spec["prerequisites"]) or "none"
            dependencies = "; ".join(spec["conditional_skills"] + spec["implementation_gates"]) or "none"
            milestones = ", ".join(spec["milestones"])
            lines.append(
                f"| {link} | {spec['purpose']} Triggers: {triggers}. | {prereqs} | {dependencies} | {spec['outputs']} | {milestones} | {spec['maturity']} |"
            )
        lines.append("")
    lines.extend([
        "## Common Dependency Bundles", "",
        f"- ARM64 first boot (topological): {' -> '.join(f'`{name}`' for name in topological_order(['roadmap-execution', 'arm64-platform-development', 'documentation-maintenance', 'agent-handoff', 'ci-maintenance']))}.",
        f"- Scheduler core (topological): {' -> '.join(f'`{name}`' for name in topological_order(['scheduler-thread-development', 'reliability-fault-injection', 'documentation-maintenance', 'agent-handoff']))}; load the affected platform skill conditionally.",
        f"- Driver workflow (topological): {' -> '.join(f'`{name}`' for name in topological_order(['adding-driver', 'documentation-maintenance']))}; load the concrete discovery/bus/transport skill conditionally.",
        f"- Peony component (topological, currently blocked): {' -> '.join(f'`{name}`' for name in topological_order(['adding-peony-component', 'documentation-maintenance']))}.",
        f"- Pull request (topological): {' -> '.join(f'`{name}`' for name in topological_order(['preparing-pull-request']))}.", "",
        "## Selection Rule", "",
        "If a task spans more than one bundle, begin with investigation and split it. Loading a skill does not authorize bypassing its prerequisites. Report a named blocker when a prerequisite is absent.", "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if generated output differs")
    args = parser.parse_args()

    for spec in SKILLS:
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", spec["name"]):
            raise ValueError(f"unsafe skill path name: {spec['name']!r}")
    expected = {spec["name"]: render_skill(spec) for spec in SKILLS}
    if args.check:
        errors = []
        for name, content in expected.items():
            path = SKILLS_DIR / name / "SKILL.md"
            if not path.is_file() or path.read_text(encoding="utf-8") != content:
                errors.append(str(path.relative_to(ROOT)))
        actual = {p.parent.name for p in SKILLS_DIR.glob("*/SKILL.md")} if SKILLS_DIR.exists() else set()
        errors.extend(f"unexpected skill {name}" for name in sorted(actual - set(expected)))
        if not MANIFEST.is_file() or MANIFEST.read_text(encoding="utf-8") != render_manifest():
            errors.append(str(MANIFEST.relative_to(ROOT)))
        if errors:
            print("generated agent files are stale:")
            print("\n".join(f"- {item}" for item in errors))
            return 1
        print(f"Generated files current: {len(expected)} skills and manifest")
        return 0

    SKILLS_DIR.mkdir(parents=True, exist_ok=True)
    expected_names = set(expected)
    unexpected = [old.parent for old in SKILLS_DIR.glob("*/SKILL.md") if old.parent.name not in expected_names]
    if unexpected:
        formatted = ", ".join(str(path.relative_to(ROOT)) for path in unexpected)
        raise RuntimeError(f"refusing to prune unregistered skill directories: {formatted}")
    for name, content in expected.items():
        directory = SKILLS_DIR / name
        if directory.is_symlink():
            raise RuntimeError(f"refusing to write through skill-directory symlink: {directory}")
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "SKILL.md").write_text(content, encoding="utf-8")
    MANIFEST.write_text(render_manifest(), encoding="utf-8")
    print(f"Rendered {len(expected)} skills and {MANIFEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
