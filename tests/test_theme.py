"""The theme: the tokens a screen refers to, and how they are resolved."""

import os
import re
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

    def test_compositor_palette_and_glass_passes_match_theme_tokens(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        with open(os.path.join(root, "ui", "_tokens.fui"), encoding="utf-8") as handle:
            source = handle.read()
        with open(os.path.join(root, "src", "compositor", "finndraw.c"), encoding="utf-8") as handle:
            palette = handle.read()
        with open(os.path.join(root, "src", "compositor", "finndraw.h"), encoding="utf-8") as handle:
            drawing = handle.read()

        theme = theme_of(source)
        light_block = palette.split("if (!dark) {", 1)[1].split("} else {", 1)[0]
        dark_block = palette.split("} else {", 1)[1].split("\n    }\n}", 1)[0]

        def values(block):
            return {
                name: int(value, 16)
                for name, value in re.findall(r"\b(FINN_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)u?", block)
            }

        names = {
            "FINN_BONE": "bone",
            "FINN_TILE": "tile",
            "FINN_TILE_ON": "tile_on",
            "FINN_HAIR": "hair",
            "FINN_INK": "ink",
            "FINN_MUTE": "mute",
            "FINN_DIM": "dim",
            "FINN_ACCENT": "accent",
            "FINN_TRACK": "track",
            "FINN_KNOB": "knob",
            "FINN_ON": "on",
            "FINN_FILL": "fill",
            "FINN_GLASS": "glass",
        }
        for block, appearance in ((light_block, "light"), (dark_block, "dark")):
            actual = values(block)
            for c_name, token in names.items():
                token_value = theme.get(f"{token}_{appearance}")["v"]
                expected = int(token_value.removeprefix("#"), 16)
                if len(token_value) == 7:
                    expected |= 0xFF000000
                if c_name == "FINN_TILE":
                    self.assertEqual(actual[c_name] & 0xFFFFFF, expected & 0xFFFFFF)
                else:
                    self.assertEqual(actual[c_name], expected, f"{c_name} differs from {token}_{appearance}")

        weights = dict(
            (name, float(value))
            for name, value in re.findall(r"\b(glass_[a-z_]+)\s+([0-9.]+)", source)
        )
        panel = float(re.search(r"FINN_GLASS_PANEL\s+([0-9.]+)f", drawing).group(1))
        tile_pass = float(re.search(r"FINN_GLASS_TILE\s+([0-9.]+)f", drawing).group(1))
        self.assertAlmostEqual(panel, weights["glass_panel"])
        self.assertAlmostEqual(tile_pass, weights["glass_tile_pass"])
        self.assertAlmostEqual(panel + (1.0 - panel) * tile_pass, weights["glass_tile"])

    def test_taskbar_geometry_matches_the_measured_three_item_figma_island(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        with open(os.path.join(root, "src", "compositor", "finndock.c"), encoding="utf-8") as handle:
            source = handle.read()
        values = {
            name: float(value)
            for name, value in re.findall(
                r"^#define\s+(TILE_W|TILE_STEP|TILE_OUTER_INSET|TILE_TOP_INSET|DOCK_H)\s+([0-9.]+)f$",
                source, re.MULTILINE,
            )
        }
        width = 2 * values["TILE_OUTER_INSET"] + values["TILE_W"] + 2 * values["TILE_STEP"]
        self.assertAlmostEqual(width, 115.0)
        self.assertAlmostEqual(values["TILE_STEP"] - values["TILE_W"], 10.0)
        self.assertAlmostEqual(2 * values["TILE_TOP_INSET"] + values["TILE_W"], values["DOCK_H"])

    def test_window_glass_and_shadow_match_figma(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        with open(os.path.join(root, "src", "compositor", "finnwindow.c"), encoding="utf-8") as handle:
            source = handle.read()
        glass = {
            name: float(value)
            for name, value in re.findall(
                r"^#define\s+(WIN_FROST|OPT_FROST)\s+([0-9.]+)f$",
                source, re.MULTILINE,
            )
        }
        header = os.path.join(root, "src", "compositor", "finndraw.h")
        with open(header, encoding="utf-8") as handle:
            drawing = handle.read()
        shadow = {
            name: float(value)
            for name, value in re.findall(
                r"^#define\s+(FINN_SHADOW_BLUR|FINN_SHADOW_OFFSET_Y|FINN_SHADOW_ALPHA)\s+([0-9.]+)f$",
                drawing, re.MULTILINE,
            )
        }
        self.assertEqual(glass, {"WIN_FROST": 0.70, "OPT_FROST": 0.50})
        self.assertEqual(shadow, {
            "FINN_SHADOW_BLUR": 100.0,
            "FINN_SHADOW_OFFSET_Y": 4.0,
            "FINN_SHADOW_ALPHA": 0.03,
        })
        self.assertIn("finn_shadow_rrect(", source)

    def test_home_material_and_shadow_match_figma(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        with open(os.path.join(root, "src", "compositor", "finnmenu.c"), encoding="utf-8") as handle:
            source = handle.read()
        glass = float(re.search(r"^#define\s+MENU_GLASS\s+([0-9.]+)f$", source, re.MULTILINE).group(1))
        self.assertEqual(glass, 0.65)
        self.assertEqual(source.count("finn_shadow_rrect("), 2)


if __name__ == "__main__":
    unittest.main()
