#!/usr/bin/env python3
"""Validate FinnOS agent skills, dependencies, references, and generated state."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

from skill_registry import SKILLS

ROOT = Path(__file__).resolve().parents[2]
SKILLS_DIR = ROOT / ".agents" / "skills"
REQUIRED_SECTIONS = [
    "Name", "Purpose", "When to use this skill", "When not to use this skill",
    "Prerequisite skills", "Authoritative repository references", "Current FinnOS context",
    "Required inputs", "Expected outputs", "Step-by-step workflow", "Repository-specific commands",
    "Architecture considerations", "Safety constraints", "Testing requirements",
    "Documentation requirements", "Review checklist", "Completion criteria",
    "Common failure modes", "Forbidden shortcuts", "Handoff requirements", "Examples",
    "Skill maintenance notes",
]
REQUIRED_SKILLS = {
    "finnos-operating-rules", "repository-orientation", "task-planning", "roadmap-execution",
    "evidence-status-reporting", "build-environment-management", "build-orchestration",
    "qemu-boot-testing", "test-strategy", "debugging-investigation", "code-review",
    "git-commit-hygiene", "github-project-management", "uefi-bootloader-development",
    "boot-protocol-evolution", "x86-64-platform-development", "arm64-platform-development",
    "cross-architecture-design", "scheduler-thread-development", "userspace-isolation",
    "driver-architecture", "network-stack-architecture", "graphics-architecture",
    "peony-design-system", "threat-modelling", "ci-maintenance", "release-engineering",
    "documentation-maintenance", "implementing-roadmap-issue", "agent-handoff",
}
NAME_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
ALLOWED_CATEGORIES = {
    "Foundations", "Boot and kernel", "Drivers and hardware", "Userspace and platform",
    "Networking", "Graphics and Peony", "Security", "Quality, release, and docs",
    "Execution workflows",
}
ALLOWED_MILESTONES = {
    "M0 Reproducible Build", "M1 Dual-Architecture Boot", "M2 Core Kernel",
    "M3 Userspace Foundation", "M4 Devices and Storage", "M5 Graphical Stack",
    "M6 Developer Preview", "M7 Beta", "M8 Stable 1.0", "post-1.0",
}


def parse_frontmatter(path: Path) -> tuple[dict, str]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if not lines or lines[0] != "---":
        raise ValueError("missing opening front matter delimiter")
    try:
        end = lines.index("---", 1)
    except ValueError as error:
        raise ValueError("missing closing front matter delimiter") from error
    data = {}
    for line in lines[1:end]:
        if not line.strip():
            continue
        key, separator, raw = line.partition(":")
        if not separator:
            raise ValueError(f"invalid front matter line: {line}")
        try:
            data[key] = json.loads(raw.strip())
        except json.JSONDecodeError as error:
            raise ValueError(f"front matter value for {key} is not JSON-compatible YAML") from error
    return data, "\n".join(lines[end + 1:])


def dependency_cycles(graph: dict[str, list[str]]) -> list[list[str]]:
    cycles = []
    state = {}
    stack = []

    def visit(node: str) -> None:
        state[node] = 1
        stack.append(node)
        for dependency in graph[node]:
            if state.get(dependency, 0) == 0:
                visit(dependency)
            elif state.get(dependency) == 1:
                start = stack.index(dependency)
                cycles.append(stack[start:] + [dependency])
        stack.pop()
        state[node] = 2

    for name in graph:
        if state.get(name, 0) == 0:
            visit(name)
    return cycles


def validate_skills() -> list[str]:
    errors = []
    registry_names = [spec["name"] for spec in SKILLS]
    if len(registry_names) != len(set(registry_names)):
        errors.append("registry contains duplicate skill names")
    if len(registry_names) < 87:
        errors.append(f"expected at least 87 skills (86 requested + root policy), found {len(registry_names)}")
    missing_required = REQUIRED_SKILLS - set(registry_names)
    if missing_required:
        errors.append(f"missing required skills: {', '.join(sorted(missing_required))}")

    graph = {spec["name"]: spec["prerequisites"] for spec in SKILLS}
    for spec in SKILLS:
        if spec["category"] not in ALLOWED_CATEGORIES:
            errors.append(f"{spec['name']}: invalid category {spec['category']}")
        if spec["status"] not in {"active", "deprecated"}:
            errors.append(f"{spec['name']}: invalid registry status {spec['status']}")
        if not isinstance(spec["version"], int) or spec["version"] < 1:
            errors.append(f"{spec['name']}: invalid registry version")
        if spec["owners"]:
            errors.append(f"{spec['name']}: owners must remain empty until MAINTAINERS.md assigns ownership")
        unknown_milestones = set(spec["milestones"]) - ALLOWED_MILESTONES
        if unknown_milestones:
            errors.append(f"{spec['name']}: unknown milestones {sorted(unknown_milestones)}")
        for conditional in spec["conditional_skills"]:
            if conditional not in registry_names:
                errors.append(f"{spec['name']}: unknown conditional skill {conditional}")
        if not isinstance(spec["verified_dirty"], bool):
            errors.append(f"{spec['name']}: verified_dirty must be boolean")
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", spec["verified_date"]):
            errors.append(f"{spec['name']}: invalid verified date {spec['verified_date']}")
    for name, prerequisites in graph.items():
        if not NAME_RE.fullmatch(name):
            errors.append(f"invalid skill name: {name}")
        for prerequisite in prerequisites:
            if prerequisite not in graph:
                errors.append(f"{name}: unknown prerequisite {prerequisite}")
            if prerequisite == name:
                errors.append(f"{name}: self dependency")
    for cycle in dependency_cycles(graph):
        errors.append(f"dependency cycle: {' -> '.join(cycle)}")
    for name in graph:
        closure = set()

        def collect(node: str) -> None:
            for dependency in graph[node]:
                if dependency not in closure:
                    closure.add(dependency)
                    collect(dependency)

        collect(name)
        if len(closure) > 20:
            errors.append(f"{name}: prerequisite closure has {len(closure)} skills; use conditional skills or implementation gates")

    actual_paths = list(SKILLS_DIR.glob("*/SKILL.md"))
    actual_names = {path.parent.name for path in actual_paths}
    for name in sorted(set(registry_names) - actual_names):
        errors.append(f"manifest skill missing file: {name}")
    for name in sorted(actual_names - set(registry_names)):
        errors.append(f"unregistered skill file: {name}")

    seen_frontmatter_names = set()
    for path in actual_paths:
        relative = path.relative_to(ROOT)
        try:
            metadata, body = parse_frontmatter(path)
        except ValueError as error:
            errors.append(f"{relative}: {error}")
            continue
        required_keys = {
            "name", "title", "version", "status", "owners", "triggers", "prerequisites",
            "related_docs", "related_milestones", "last_verified", "description", "category",
            "conditional_skills", "implementation_gates",
        }
        missing = required_keys - metadata.keys()
        if missing:
            errors.append(f"{relative}: missing metadata {', '.join(sorted(missing))}")
            continue
        name = metadata["name"]
        if name != path.parent.name:
            errors.append(f"{relative}: metadata name {name!r} does not match directory")
        if name in seen_frontmatter_names:
            errors.append(f"{relative}: duplicate metadata name {name}")
        seen_frontmatter_names.add(name)
        if metadata["status"] not in {"active", "deprecated"}:
            errors.append(f"{relative}: unsupported status {metadata['status']}")
        if not isinstance(metadata["version"], int) or metadata["version"] < 1:
            errors.append(f"{relative}: version must be a positive integer")
        if not metadata["description"].startswith("Use when"):
            errors.append(f"{relative}: description must begin with 'Use when'")
        if len(name) > 64:
            errors.append(f"{relative}: OpenCode skill name exceeds 64 characters")
        if not 1 <= len(metadata["description"]) <= 1024:
            errors.append(f"{relative}: OpenCode description length is invalid")
        verified = metadata["last_verified"]
        required_verification = {"base_commit", "date", "worktree_dirty", "context"}
        if not isinstance(verified, dict) or not required_verification <= verified.keys():
            errors.append(f"{relative}: last_verified lacks explicit base/worktree provenance")
        registry = next((spec for spec in SKILLS if spec["name"] == name), None)
        if registry:
            expected_metadata = {
                "title": registry["title"], "version": registry["version"],
                "status": registry["status"], "owners": registry["owners"],
                "triggers": registry["triggers"], "prerequisites": registry["prerequisites"],
                "related_docs": registry["docs"], "related_milestones": registry["milestones"],
                "category": registry["category"],
                "conditional_skills": registry["conditional_skills"],
                "implementation_gates": registry["implementation_gates"],
            }
            for key, expected in expected_metadata.items():
                if metadata.get(key) != expected:
                    errors.append(f"{relative}: front matter {key} differs from registry")
        for doc in metadata["related_docs"]:
            if not (ROOT / doc).exists():
                errors.append(f"{relative}: nonexistent related doc {doc}")
        headings = re.findall(r"^##\s+\d+\.\s+(.+)$", body, flags=re.MULTILINE)
        if headings != REQUIRED_SECTIONS:
            errors.append(f"{relative}: required sections differ or are out of order")
        sections = re.split(r"^##\s+\d+\.\s+.+$", body, flags=re.MULTILINE)[1:]
        if len(sections) == len(REQUIRED_SECTIONS):
            for heading, content in zip(REQUIRED_SECTIONS, sections):
                if len(content.strip()) < 20:
                    errors.append(f"{relative}: section {heading!r} is not substantive")
        for command in re.findall(r"```bash\n(.*?)\n```", body, flags=re.DOTALL):
            for line in command.splitlines():
                try:
                    tokens = shlex.split(line)
                except ValueError as error:
                    errors.append(f"{relative}: invalid shell syntax {line!r}: {error}")
                    continue
                while tokens and "=" in tokens[0] and not tokens[0].startswith(("./", "/")):
                    tokens.pop(0)
                token = tokens[0] if tokens else ""
                if token.startswith("./") and not (ROOT / token[2:]).exists():
                    errors.append(f"{relative}: command references missing path {token}")
                if token in {"python", "python3"} and len(tokens) > 1 and tokens[1].endswith(".py"):
                    if not (ROOT / tokens[1]).exists():
                        errors.append(f"{relative}: Python command references missing path {tokens[1]}")
                if line.strip().startswith("cd "):
                    errors.append(f"{relative}: commands must run from root, not use cd")
        if metadata["status"] == "deprecated" and "supersed" not in body.lower():
            errors.append(f"{relative}: deprecated skill does not name supersession")

    manifest = ROOT / ".agents" / "MANIFEST.md"
    if not manifest.is_file():
        errors.append("missing .agents/MANIFEST.md")
    else:
        manifest_text = manifest.read_text(encoding="utf-8")
        for name in registry_names:
            expected = f"skills/{name}/SKILL.md"
            if expected not in manifest_text:
                errors.append(f"manifest missing skill link {name}")
    config = ROOT / "opencode.json"
    try:
        opencode = json.loads(config.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"opencode.json is invalid: {error}")
    else:
        if opencode.get("$schema") != "https://opencode.ai/config.json":
            errors.append("opencode.json lacks the canonical schema URL")
        for path in opencode.get("instructions", []):
            if not (ROOT / path).exists():
                errors.append(f"opencode.json references missing instruction {path}")
        for path in opencode.get("skills", {}).get("paths", []):
            if not (ROOT / path).is_dir():
                errors.append(f"opencode.json references missing skill path {path}")
    state = ROOT / ".agents" / "STATE.md"
    state_text = state.read_text(encoding="utf-8") if state.is_file() else ""
    for token in ("verified_base_commit:", "working_tree_dirty:", "maturity:", "executable_architectures:", "current_critical_milestone:"):
        if token not in state_text:
            errors.append(f".agents/STATE.md missing {token}")
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{SKILLS[0]['verified_commit']}^{{commit}}"],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )
    if result.returncode != 0:
        shallow = subprocess.run(
            ["git", "rev-parse", "--is-shallow-repository"], cwd=ROOT,
            text=True, capture_output=True, check=False,
        )
        if shallow.returncode != 0 or shallow.stdout.strip() != "true":
            errors.append(f"verification base commit does not exist: {SKILLS[0]['verified_commit']}")
    return errors


def run(command: list[str]) -> list[str]:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    if result.returncode == 0:
        return []
    output = (result.stdout + result.stderr).strip()
    return [f"{' '.join(command)} failed:\n{output}"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true", help="also check generated drift, links, and planning YAML")
    args = parser.parse_args()
    errors = validate_skills()
    if args.all:
        python = sys.executable
        errors.extend(run([python, ".agents/scripts/render_skills.py", "--check"]))
        errors.extend(run([python, ".agents/scripts/check_links.py"]))
        errors.extend(run([python, ".agents/scripts/validate_yaml.py"]))
    if errors:
        print(f"Agent validation failed with {len(errors)} error(s):")
        print("\n".join(f"- {error}" for error in errors))
        return 1
    print(f"Agent validation passed: {len(SKILLS)} skills, no dependency cycles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
