#!/usr/bin/env python3
"""Validate the repository's constrained planning and GitHub-template YAML shape.

This intentionally validates only FinnOS-owned schema and indentation, not arbitrary YAML.
It avoids a PyYAML dependency on clean contributor hosts.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = {
    "docs/github-planning/labels.yml": ("labels:", ["name:", "color:", "description:"]),
    "docs/github-planning/milestones.yml": ("milestones:", ["title:", "objective:", "exit:"]),
    "docs/github-planning/issues.yml": ("issues:", ["id:", "title:", "milestone:", "labels:", "body_source:"]),
    ".github/ISSUE_TEMPLATE/driver.yml": ("name:", ["description:", "body:", "id:", "attributes:"]),
    ".github/ISSUE_TEMPLATE/architecture.yml": ("name:", ["description:", "body:", "id:", "attributes:"]),
}


def main() -> int:
    errors = []
    for relative, (first_key, required_tokens) in FILES.items():
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"missing {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        if not lines or not lines[0].startswith(first_key):
            errors.append(f"{relative}: expected first key {first_key}")
        if "\t" in text:
            errors.append(f"{relative}: tabs are forbidden")
        if any(line.rstrip() != line for line in lines):
            errors.append(f"{relative}: trailing whitespace")
        for number, line in enumerate(lines, 1):
            if line and (len(line) - len(line.lstrip(" "))) % 2 != 0:
                errors.append(f"{relative}:{number}: indentation must use two-space levels")
            stripped = line.strip()
            if stripped and not stripped.startswith(("#", "-")) and ":" not in stripped:
                errors.append(f"{relative}:{number}: unsupported scalar-only line")
        for token in required_tokens:
            if token not in text:
                errors.append(f"{relative}: missing required token {token}")
        if re.search(r"(?:token|password|secret|api_key)\s*:\s*[^\s\[\]{]", text, re.I):
            errors.append(f"{relative}: possible embedded credential")

    labels_text = (ROOT / "docs/github-planning/labels.yml").read_text(encoding="utf-8")
    labels = set(re.findall(r'name:\s*"([^"]+)"', labels_text))
    if len(labels) != len(re.findall(r'name:\s*"([^"]+)"', labels_text)):
        errors.append("docs/github-planning/labels.yml: duplicate label name")
    milestones_text = (ROOT / "docs/github-planning/milestones.yml").read_text(encoding="utf-8")
    milestones = set(re.findall(r'^\s+- title:\s*"([^"]+)"', milestones_text, re.MULTILINE))

    issues_text = (ROOT / "docs/github-planning/issues.yml").read_text(encoding="utf-8")
    issue_blocks = re.split(r"^\s+- id:\s*", issues_text, flags=re.MULTILINE)[1:]
    issue_ids = []
    roadmap = (ROOT / "ROADMAP.md").read_text(encoding="utf-8")
    for block in issue_blocks:
        issue_id = block.splitlines()[0].strip()
        issue_ids.append(issue_id)
        milestone = re.search(r'^\s+milestone:\s*"([^"]+)"', block, re.MULTILINE)
        if not milestone or milestone.group(1) not in milestones:
            errors.append(f"issues.yml {issue_id}: unknown or missing milestone")
        label_line = re.search(r"^\s+labels:\s*\[(.*)\]", block, re.MULTILINE)
        issue_labels = re.findall(r'"([^"]+)"', label_line.group(1)) if label_line else []
        for label in issue_labels:
            if label not in labels:
                errors.append(f"issues.yml {issue_id}: unknown label {label}")
        body = re.search(r'^\s+body_source:\s*"ROADMAP.md#([^"]+)"', block, re.MULTILINE)
        if not body:
            errors.append(f"issues.yml {issue_id}: missing ROADMAP body_source")
        else:
            anchor_words = body.group(1).replace("-", ".*")
            if not re.search(rf"^###\s+{re.escape(issue_id)}\b.*", roadmap, re.MULTILINE | re.IGNORECASE):
                errors.append(f"issues.yml {issue_id}: roadmap heading missing for {body.group(1)}")
    if len(issue_ids) != len(set(issue_ids)):
        errors.append("docs/github-planning/issues.yml: duplicate issue id")
    if issue_ids != [f"R{index}" for index in range(1, 11)]:
        errors.append("docs/github-planning/issues.yml: expected intentional R1-R10 initial issue set in order")

    for relative in (".github/ISSUE_TEMPLATE/driver.yml", ".github/ISSUE_TEMPLATE/architecture.yml"):
        text = (ROOT / relative).read_text(encoding="utf-8")
        ids = re.findall(r"^\s+id:\s*([a-zA-Z0-9_-]+)\s*$", text, re.MULTILINE)
        if not ids or len(ids) != len(set(ids)):
            errors.append(f"{relative}: body IDs must be present and unique")
        if "validations:" not in text or "required: true" not in text:
            errors.append(f"{relative}: at least one required validation is expected")
    if errors:
        print("Planning YAML validation failed:")
        print("\n".join(f"- {item}" for item in errors))
        return 1
    print(f"Planning YAML structurally valid: {len(FILES)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
