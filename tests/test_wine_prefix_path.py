"""The default Wine prefix is one fact shared by C++ and the Python runner.

src/common/paths.hpp is the source of truth. tools/finn-win is a standalone
script installed as /usr/local/bin/finn-win, so it cannot include a C++ header;
this test is the guard that stops the two literals drifting apart again.
"""

import importlib.machinery
import importlib.util
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def canonical_prefix_rel() -> str:
    header = (ROOT / "src" / "common" / "paths.hpp").read_text(encoding="utf-8")
    match = re.search(r'kWinePrefixRel\s*=\s*"([^"]+)"', header)
    if not match:
        raise AssertionError("kWinePrefixRel missing from src/common/paths.hpp")
    return match.group(1)


def load_finn_win():
    path = ROOT / "tools" / "finn-win"
    loader = importlib.machinery.SourceFileLoader("finn_win_under_test", str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class WinePrefixPathTest(unittest.TestCase):
    def test_canonical_prefix_is_the_initialised_one(self):
        # The VM (and every installed machine) initialises drive_c directly in
        # this directory. A "default" child is not created by finn-win init.
        self.assertEqual(canonical_prefix_rel(), ".local/share/finn/wine")

    def test_finn_win_uses_the_same_prefix(self):
        rel = canonical_prefix_rel()
        expected = os.path.join(os.path.expanduser("~"), *rel.split("/"))
        self.assertEqual(load_finn_win().DEFAULT_PREFIX, expected)


if __name__ == "__main__":
    unittest.main()
