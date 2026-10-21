"""The theme: the tokens a screen refers to, and how they are resolved."""

import os
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

from finnsdk.fuic.parser import parse
from finnsdk.fuic.theme import Theme


def theme_of(text):
    return Theme.from_items(parse(text))


class ThemeTest(unittest.TestCase):

    def test_a_colour_is_a_token(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        self.assertEqual(theme.get("accent"), {"k": "colour", "v": "#7AB8FF"})
        self.assertEqual(theme.warnings, [])

    def test_a_light_and_a_dark_half_are_one_token(self):
        theme = theme_of("""
            theme FinnTokens {
              color: surface_light #F7F9FC, surface_dark #0B0E14
            }
        """)
        token = theme.get("surface")
        self.assertEqual(token["k"], "themed")
        self.assertEqual(token["light"]["v"], "#F7F9FC")
        self.assertEqual(token["dark"]["v"], "#0B0E14")

    def test_the_halves_stay_in_the_table(self):
        # a screen that wants one particular half can ask for it by name
        theme = theme_of("""
            theme FinnTokens {
              color: surface_light #F7F9FC, surface_dark #0B0E14
            }
        """)
        self.assertEqual(theme.get("surface_light")["v"], "#F7F9FC")
        self.assertEqual(theme.get("surface_dark")["v"], "#0B0E14")

    def test_a_radius_is_a_number(self):
        theme = theme_of("theme FinnTokens { radius: md 12 }")
        self.assertEqual(theme.get("md"), {"k": "int", "v": 12})

    def test_a_type_is_a_type(self):
        theme = theme_of('theme FinnTokens { type: body 14/20 "Fixel Display" 400 }')
        token = theme.get("body")
        self.assertEqual(token["k"], "map")
        self.assertEqual(dict(token["v"])["size"], {"k": "int", "v": 14})
        self.assertEqual(dict(token["v"])["line"], {"k": "int", "v": 20})
        self.assertEqual(dict(token["v"])["family"], {"k": "str", "v": "Fixel Display"})
        self.assertEqual(dict(token["v"])["weight"], {"k": "int", "v": 400})

    def test_a_motion_is_a_motion(self):
        theme = theme_of("theme FinnTokens { motion: theme_xfade 180ms linear }")
        token = theme.get("theme_xfade")
        self.assertEqual(token["k"], "map")
        fields = dict(token["v"])
        self.assertEqual(fields["duration"], {"k": "int", "v": 180})
        self.assertEqual(fields["unit"], {"k": "str", "v": "ms"})
        self.assertEqual(fields["easing"], {"k": "str", "v": "linear"})

    def test_a_font_keeps_its_prefix(self):
        theme = theme_of('theme FinnTokens { font: family "Fixel Display" }')
        self.assertEqual(theme.get("font.family"), {"k": "str", "v": "Fixel Display"})

    # ---- resolution ----

    def test_a_token_reference_is_resolved(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        value = theme.resolve({"k": "ident", "v": "token.accent"})
        self.assertEqual(value, {"k": "colour", "v": "#7AB8FF"})
        self.assertEqual(theme.warnings, [])

    def test_a_token_reference_inside_a_phrase_is_resolved(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        value = theme.resolve({"k": "phrase", "v": [
            {"k": "num", "v": 2},
            {"k": "ident", "v": "token.accent"},
        ]})
        self.assertEqual(value["v"][1], {"k": "colour", "v": "#7AB8FF"})

    def test_a_token_reference_inside_a_list_is_resolved(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        value = theme.resolve({"k": "list", "v": [{"k": "ident", "v": "token.accent"}]})
        self.assertEqual(value["v"][0], {"k": "colour", "v": "#7AB8FF"})

    def test_an_unknown_token_is_a_warning(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        value = theme.resolve({"k": "ident", "v": "token.nonesuch"})
        self.assertEqual(value, {"k": "ident", "v": "token.nonesuch"})
        self.assertEqual(theme.warnings, ["unknown token 'token.nonesuch'"])

    def test_a_name_that_is_not_a_token_is_left_alone(self):
        theme = theme_of("theme FinnTokens { color: accent #7AB8FF }")
        value = theme.resolve({"k": "ident", "v": "system.theme"})
        self.assertEqual(value, {"k": "ident", "v": "system.theme"})
        self.assertEqual(theme.warnings, [])

    def test_a_colour_that_is_not_a_colour_is_a_warning(self):
        theme = theme_of('theme FinnTokens { color: accent "blue" }')
        self.assertEqual(theme.warnings, ["colour 'accent' is not a colour"])


if __name__ == "__main__":
    unittest.main()
