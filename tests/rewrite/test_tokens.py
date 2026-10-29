#!/usr/bin/env python3
"""The generated token header must match ui/_tokens.fui exactly."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    with tempfile.NamedTemporaryFile("r", suffix=".hpp") as generated:
        subprocess.run(
            [sys.executable, str(ROOT / "tools/tokens.py"),
             str(ROOT / "ui/_tokens.fui"), generated.name],
            check=True,
        )
        expected = (ROOT / "src/ui/generated_tokens.hpp").read_text()
        actual = Path(generated.name).read_text()
    if expected != actual:
        print("test_tokens: generated_tokens.hpp is stale", file=sys.stderr)
        return 1
    print("test_tokens: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
