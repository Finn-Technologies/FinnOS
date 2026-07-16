#!/usr/bin/env python3
"""Check repository-local Markdown links without network access."""

from __future__ import annotations

import re
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
IGNORED_ROOTS = {".git", "target", "build"}


def main() -> int:
    errors = []
    checked = 0
    for path in ROOT.rglob("*.md"):
        if any(part in IGNORED_ROOTS for part in path.relative_to(ROOT).parts):
            continue
        for raw in LINK_RE.findall(path.read_text(encoding="utf-8")):
            target = raw.strip().split(maxsplit=1)[0].strip("<>")
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            checked += 1
            resolved = (path.parent / target).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                errors.append(f"{path.relative_to(ROOT)}: link escapes repository: {raw}")
                continue
            if not resolved.exists():
                errors.append(f"{path.relative_to(ROOT)}: missing link target: {raw}")
    if errors:
        print("Markdown link validation failed:")
        print("\n".join(f"- {item}" for item in errors))
        return 1
    print(f"Markdown links valid: {checked} local references")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
