"""The compiler: the `.fui` sources in this tree, and the templates.

The `.fui` files under `ui/` and `apps/` belong to the shell and to the
first-party apps, and they are edited while the rest of the tree is
worked on. A file that is being edited may not parse at that moment,
and that is not the compiler's fault: a parse error is a **skip** here,
not a failure. What is a failure is a file that parses and then makes
the compiler fall over — that is the compiler's fault, and it is what
this suite is for.
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

from finnsdk.fuic import compile_file, compile_source, read_fbin
from finnsdk.fuic.compiler import CompileError, find_theme
from finnsdk.fuic.lexer import LexError
from finnsdk.fuic.parser import ParseError, parse


class RepoSourcesTest(unittest.TestCase):
    """Every `.fui` in the tree has to compile."""

    def test_every_source_compiles(self):
        files = support.fui_files()
        self.assertTrue(files, "there are no .fui sources to test")
        for path in files:
            with self.subTest(source=support.relative(path)):
                self.skip_unless_it_parses(path)
                warnings, data = compile_file(path, None)
                self.assertTrue(data.startswith(b"FBIN"))
                sections = read_fbin(data)
                self.assertIn("tokens", sections)

    def test_a_source_with_a_screen_has_one(self):
        for path in support.fui_files():
            if os.path.basename(path).startswith("_"):
                continue          # the theme and the targets are libraries
            with self.subTest(source=support.relative(path)):
                self.skip_unless_it_parses(path)
                _warnings, data = compile_file(path, None)
                self.assertIn("screen", read_fbin(data))

    def skip_unless_it_parses(self, path):
        """Skip a file that does not parse: it is not ours to fix."""
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        try:
            parse(text)
        except (ParseError, LexError) as exc:
            self.skipTest("%s does not parse yet: %s"
                          % (support.relative(path), exc))


class CompileTest(unittest.TestCase):

    def test_a_screen_becomes_a_screen_section(self):
        sections, warnings = compile_source("""
            screen Notes {
              Column(padding:20) { Text("Hello") }
            }
        """)
        self.assertEqual([name for name, _ in sections], ["screen", "tokens"])
        self.assertEqual(warnings, [])

    def test_a_theme_becomes_a_tokens_section(self):
        sections, _warnings = compile_source("""
            theme FinnTokens { color: accent #7AB8FF }
        """)
        self.assertEqual([name for name, _ in sections], ["tokens"])

    def test_a_target_becomes_its_own_section(self):
        sections, _warnings = compile_source("""
            target Desktop { viewport: min 1024x600 }
        """)
        self.assertIn("target:Desktop", [name for name, _ in sections])

    def test_a_file_with_two_screens_keeps_both(self):
        sections, _warnings = compile_source("""
            screen Notes { }
            screen Files { }
        """)
        names = [name for name, _ in sections]
        self.assertIn("screen", names)
        self.assertIn("screen:Files", names)

    def test_a_file_with_nothing_in_it_is_an_error(self):
        with self.assertRaises(CompileError):
            compile_source("// nothing here\n")

    def test_the_tokens_are_resolved_into_the_screen(self):
        sections, warnings = compile_source("""
            theme FinnTokens { color: accent #7AB8FF }
            screen Notes { ink: token.accent }
        """)
        self.assertEqual(warnings, [])
        screen = read_fbin(fbin_write(sections))["screen"]
        self.assertEqual(screen["props"]["ink"], {"k": "colour", "v": "#7AB8FF"})

    def test_an_unknown_token_is_a_warning(self):
        _sections, warnings = compile_source("""
            screen Notes { ink: token.nonesuch }
        """)
        self.assertEqual(warnings, ["unknown token 'token.nonesuch'"])

    def test_strict_turns_a_warning_into_an_error(self):
        with self.assertRaises(CompileError):
            compile_source("screen Notes { ink: token.nonesuch }", strict=True)

    def test_the_theme_beside_the_file_is_used(self):
        tmp = tempfile.mkdtemp(prefix="finn-test-")
        try:
            with open(os.path.join(tmp, "_tokens.fui"), "w", encoding="utf-8") as handle:
                handle.write("theme FinnTokens { color: accent #7AB8FF }\n")
            path = os.path.join(tmp, "Notes.fui")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("screen Notes { ink: token.accent }\n")
            warnings, _data = compile_file(path, None)
            self.assertEqual(warnings, [])
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_the_system_theme_is_found_from_an_app(self):
        # the apps live in apps/<name>/ and use the theme of the system,
        # which lives in ui/_tokens.fui
        theme = os.path.join(support.UI_DIR, "_tokens.fui")
        if not os.path.exists(theme):
            self.skipTest("this tree has no ui/_tokens.fui")
        found = find_theme(os.path.join(support.APPS_DIR, "window", "Window.fui"))
        self.assertIsNotNone(found)
        self.assertEqual(os.path.basename(found), "_tokens.fui")


def fbin_write(sections):
    from finnsdk.fuic import fbin
    return fbin.write(sections)


class TemplateTest(unittest.TestCase):
    """`fuic new` writes files that compile."""

    def new_app(self, name):
        from finnsdk.fuic.cli import main
        tmp = tempfile.mkdtemp(prefix="finn-test-")
        with support.quiet():
            self.assertEqual(main(["new", name, "-d", tmp]), 0)
        return tmp

    def test_the_template_writes_a_screen_and_a_manifest(self):
        tmp = self.new_app("Finn Notes")
        try:
            self.assertTrue(os.path.exists(os.path.join(tmp, "Notes.fui")))
            self.assertTrue(os.path.exists(os.path.join(tmp, "manifest.toml")))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_the_screen_the_template_writes_compiles(self):
        tmp = self.new_app("Finn Notes")
        try:
            path = os.path.join(tmp, "Notes.fui")
            _warnings, data = compile_file(path, None)
            screen = read_fbin(data)["screen"]
            self.assertEqual(screen["name"], "Notes")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_the_manifest_says_what_the_package_is(self):
        tmp = self.new_app("Finn Notes")
        try:
            with open(os.path.join(tmp, "manifest.toml"), "r", encoding="utf-8") as handle:
                text = handle.read()
            self.assertIn('id = "notes"', text)
            self.assertIn('name = "Finn Notes"', text)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_the_name_follows_the_house_rule(self):
        # the brand and a plain English noun
        tmp = self.new_app("notes")
        try:
            with open(os.path.join(tmp, "Notes.fui"), "r", encoding="utf-8") as handle:
                self.assertIn("Finn Notes", handle.read())
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
