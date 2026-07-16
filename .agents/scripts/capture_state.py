#!/usr/bin/env python3
"""Print a non-mutating FinnOS repository/build environment snapshot."""

from __future__ import annotations

import platform
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def capture(command: list[str]) -> str:
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    except OSError as error:
        return f"status: unavailable\nerror: {error}"
    stdout = result.stdout.strip() or "<empty>"
    stderr = result.stderr.strip() or "<empty>"
    return f"status: {result.returncode}\nstdout:\n{stdout}\nstderr:\n{stderr}"


def main() -> int:
    print("# FinnOS State Capture")
    print(f"host: {platform.platform()}")
    for label, command in [
        ("git status", ["git", "status", "--short", "--branch"]),
        ("recent history", ["git", "log", "--oneline", "-5"]),
        ("rustc", ["rustc", "--version", "--verbose"]),
        ("cargo", ["cargo", "--version"]),
        ("python", ["python3", "--version"]),
        ("qemu x86", ["qemu-system-x86_64", "--version"]),
        ("doctor", ["./tools/finn", "doctor"]),
    ]:
        print(f"\n## {label}\n{capture(command)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
