"""The two command lines: `fuic` and `finn-sdk`.

They speak to the same device and the same compiler as the daemons, and
what is tested here is that they say what they mean and report the
device's own refusal.
"""

import os
import shutil
import sys
import tempfile
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

import fakeserver  # noqa: E402

from finnsdk.cli import main as sdk_main
from finnsdk.fuic.cli import main as fuic_main


class SdkCliTest(unittest.TestCase):

    def setUp(self):
        self.server = fakeserver.FakeServer()
        self.addCleanup(self.server.close)

    def run_cli(self, *args):
        """Run `finn-sdk` and say what it printed."""
        out = []
        with support.quiet() as quiet:
            code = sdk_main(["--socket", self.server.path] + list(args))
        out.append(quiet.getvalue())
        return code, "".join(out)

    def test_state_prints_the_state(self):
        code, text = self.run_cli("state")
        self.assertEqual(code, 0)
        self.assertIn("battery", text)
        self.assertIn("87", text)

    def test_apps_prints_the_registry(self):
        code, text = self.run_cli("apps")
        self.assertEqual(code, 0)
        self.assertIn("notes", text)
        self.assertIn("Finn Notes", text)

    def test_launch_starts_an_app(self):
        code, text = self.run_cli("launch", "notes")
        self.assertEqual(code, 0)
        self.assertIn("launched notes", text)

    def test_ping_says_the_protocol(self):
        code, text = self.run_cli("ping")
        self.assertEqual(code, 0)
        self.assertIn("FINNCTL/1", text)

    def test_a_refusal_is_reported_and_fails(self):
        code, text = self.run_cli("launch", "nonesuch")
        self.assertEqual(code, 1)

    def test_notifications_are_listed(self):
        code, text = self.run_cli("notify")
        self.assertEqual(code, 0)
        self.assertIn("Hello", text)


class FuicCliTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="finn-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def write(self, name, text):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_check_reports_what_the_file_holds(self):
        path = self.write("Notes.fui", "screen Notes { }\n")
        with support.quiet() as quiet:
            code = fuic_main(["check", path])
        self.assertEqual(code, 0)
        self.assertIn("1 screen(s)", quiet.getvalue())

    def test_check_fails_on_a_file_that_does_not_parse(self):
        path = self.write("Bad.fui", "screen Notes {\n")
        with support.quiet():
            code = fuic_main(["check", path])
        self.assertEqual(code, 1)

    def test_compile_writes_a_binary(self):
        path = self.write("Notes.fui", 'screen Notes { Text("Hello") }\n')
        out = os.path.join(self.tmp, "Notes.fbin")
        with support.quiet():
            code = fuic_main(["compile", path, "-o", out])
        self.assertEqual(code, 0)
        with open(out, "rb") as handle:
            self.assertTrue(handle.read(4) == b"FBIN")

    def test_dump_prints_the_tree(self):
        path = self.write("Notes.fui", 'screen Notes { Text("Hello") }\n')
        with support.quiet() as quiet:
            code = fuic_main(["dump", path])
        self.assertEqual(code, 0)
        self.assertIn("Notes", quiet.getvalue())

    def test_new_writes_an_app(self):
        with support.quiet():
            code = fuic_main(["new", "Finn Notes", "-d", self.tmp])
        self.assertEqual(code, 0)
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "Notes.fui")))
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "manifest.toml")))

    def test_without_a_command_it_prints_the_help(self):
        with support.quiet() as quiet:
            code = fuic_main([])
        self.assertEqual(code, 2)
        self.assertIn("usage", quiet.getvalue())


if __name__ == "__main__":
    unittest.main()
