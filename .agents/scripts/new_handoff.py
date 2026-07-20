#!/usr/bin/env python3
"""Print or create a FinnOS handoff skeleton with current Git state."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / ".agents" / "templates" / "handoff-template.md"
HANDOFF_DIR = ROOT / ".agents" / "handoffs"


def git(command: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *command], cwd=ROOT, text=True, capture_output=True, check=False,
        )
    except OSError as error:
        return f"unavailable: {error}"
    output = (result.stdout + result.stderr).strip()
    return output or f"<empty; status {result.returncode}>"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, help="explicit output path; default prints to stdout")
    args = parser.parse_args()
    marker = "- Starting commit/worktree:"
    template = TEMPLATE.read_text(encoding="utf-8")
    if template.count(marker) != 1:
        raise RuntimeError(f"expected exactly one handoff marker {marker!r}")
    state = f"commit: {git(['rev-parse', 'HEAD'])}\n{git(['status', '--short', '--branch'])}"
    text = template.replace(marker, f"{marker}\n```text\n{state}\n```")
    if args.output:
        if args.output.is_absolute() or args.output.parent != Path("."):
            parser.error("--output must be one filename under .agents/handoffs/")
        if args.output.suffix != ".md":
            parser.error("--output must end in .md")
        output = HANDOFF_DIR / args.output
        if output.exists() or output.is_symlink():
            parser.error(f"refusing to overwrite {output.relative_to(ROOT)}")
        HANDOFF_DIR.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as handle:
            handle.write(text)
        print(output)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
