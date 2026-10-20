"""The parser: the tree a `.fui` source becomes."""

import os
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

from findsdk.fuic.parser import ParseError, parse


class ParserTest(unittest.TestCase):

    def parse(self, text):
        return parse(text)

    # ---- the shape of a file ----

    def test_a_file_holds_screens_themes_and_targets(self):
        items = self.parse("""
            screen Notes { }
            theme FinnTokens { color: ink #111318 }
            target Desktop { viewport: min 1024x600 }
        """)
        self.assertEqual([item.kind for item in items],
                         ["screen", "theme", "target"])
        self.assertEqual([item.name for item in items],
                         ["Notes", "FinnTokens", "Desktop"])

    def test_an_unknown_top_level_item_is_an_error(self):
        with self.assertRaises(ParseError):
            self.parse("widget Notes { }")

    def test_an_unterminated_screen_is_an_error(self):
        with self.assertRaises(ParseError):
            self.parse("screen Notes {")

    # ---- properties ----

    def test_a_property_holds_one_value(self):
        screen = self.parse('screen Notes { title: "Notes" }')[0]
        self.assertEqual(screen.get("title"), {"k": "str", "v": "Notes"})

    def test_a_property_may_hold_a_number(self):
        screen = self.parse("screen Notes { count: 3 }")[0]
        self.assertEqual(screen.get("count"), {"k": "int", "v": 3})

    def test_a_property_may_hold_a_colour(self):
        screen = self.parse("screen Notes { ink: #111318 }")[0]
        self.assertEqual(screen.get("ink"), {"k": "colour", "v": "#111318"})

    def test_a_property_may_hold_a_list(self):
        screen = self.parse("screen Notes { options: [Light, Dark] }")[0]
        value = screen.get("options")
        self.assertEqual(value["k"], "list")
        self.assertEqual([item["v"] for item in value["v"]], ["Light", "Dark"])

    def test_a_property_may_hold_a_map(self):
        screen = self.parse("screen Notes { shell: { dock: bottom, menu: top } }")[0]
        value = screen.get("shell")
        self.assertEqual(value["k"], "map")
        self.assertEqual([key for key, _ in value["v"]], ["dock", "menu"])

    def test_a_property_may_hold_an_arrow(self):
        screen = self.parse("screen Notes { layout: windows->fullscreen }")[0]
        value = screen.get("layout")
        self.assertEqual(value["k"], "arrow")
        self.assertEqual(value["v"][0]["v"], "windows")
        self.assertEqual(value["v"][1]["v"], "fullscreen")

    def test_a_property_may_hold_a_phrase(self):
        screen = self.parse("screen Notes { focus_ring: 2px accent when keyboard_nav }")[0]
        value = screen.get("focus_ring")
        self.assertEqual(value["k"], "phrase")
        self.assertEqual(len(value["v"]), 4)

    def test_a_dotted_path_is_one_name(self):
        screen = self.parse("screen Notes { bind: system.theme }")[0]
        self.assertEqual(screen.get("bind"), {"k": "ident", "v": "system.theme"})

    def test_a_property_written_twice_keeps_both(self):
        screen = self.parse("""
            screen Notes {
              adapt(Mobile): windows->fullscreen
              adapt(TV): windows->fullscreen_single
            }
        """)[0]
        value = screen.get("adapt")
        self.assertEqual(value["k"], "list")
        self.assertEqual(len(value["v"]), 2)

    def test_a_property_with_an_argument_is_scoped(self):
        screen = self.parse("screen Notes { adapt(Mobile): windows->fullscreen }")[0]
        value = screen.get("adapt")
        self.assertEqual(value["k"], "map")
        self.assertEqual(value["v"][0][0], "Mobile")

    # ---- elements ----

    def test_an_element_has_a_name_and_children(self):
        screen = self.parse("""
            screen Notes {
              Column(padding:20, gap:12) {
                Text("Hello", style:title)
              }
            }
        """)[0]
        column = screen.child("Column")
        self.assertIsNotNone(column)
        self.assertEqual(column.kind, "element")
        self.assertEqual(column.get("padding"), {"k": "int", "v": 20})
        self.assertEqual(column.get("gap"), {"k": "int", "v": 12})
        self.assertEqual(len(column.children), 1)
        self.assertEqual(column.children[0].name, "Text")

    def test_an_element_may_hold_positional_arguments(self):
        screen = self.parse('screen Notes { Item("Home") }')[0]
        item = screen.child("Item")
        self.assertEqual(item.args[0], {"k": "str", "v": "Home"})

    def test_two_elements_may_share_a_line(self):
        screen = self.parse('screen Notes { Item("Home") Item("Documents") }')[0]
        self.assertEqual([child.name for child in screen.children],
                         ["Item", "Item"])

    def test_a_semicolon_ends_a_statement(self):
        screen = self.parse('screen Notes { Item("Home"); Item("Documents") }')[0]
        self.assertEqual([child.name for child in screen.children],
                         ["Item", "Item"])

    def test_a_call_is_a_name_and_its_arguments(self):
        screen = self.parse("screen Notes { fill: linear(180deg, red) }")[0]
        value = screen.get("fill")
        self.assertEqual(value["k"], "call")
        self.assertEqual(value["v"][0]["v"], "linear")

    def test_a_call_may_have_named_arguments(self):
        screen = self.parse("screen Notes { bg: surface(bind: system.theme) }")[0]
        value = screen.get("bg")
        self.assertEqual(value["k"], "call")
        self.assertEqual(value["v"][1]["k"], "map")

    def test_a_plus_makes_one_value_of_two(self):
        screen = self.parse("screen Notes { fill: token.a + token.b }")[0]
        value = screen.get("fill")
        self.assertEqual(value["k"], "combine")
        self.assertEqual(len(value["v"]), 2)

    def test_a_boolean_is_a_boolean(self):
        screen = self.parse("screen Notes { default: false }")[0]
        self.assertEqual(screen.get("default"), {"k": "bool", "v": False})

    # ---- themes and targets ----

    def test_a_theme_declaration_is_a_list_of_pairs(self):
        theme = self.parse("theme FinnTokens { color: ink_light #111318, ink_dark #E8ECF1 }")[0]
        pairs = theme.props["color"]
        self.assertEqual(len(pairs), 2)
        self.assertEqual(pairs[0][0], "ink_light")
        self.assertEqual(pairs[0][1]["v"], "#111318")

    def test_a_target_declaration_is_a_property(self):
        target = self.parse("target Desktop { viewport: min 1024x600 }")[0]
        self.assertEqual(target.get("viewport")["k"], "phrase")

    def test_a_target_may_hold_several_declarations(self):
        target = self.parse("""
            target Desktop {
              density: 1.0x
              type_scale: 1.0
            }
        """)[0]
        self.assertIn("density", target.props)
        self.assertIn("type_scale", target.props)


if __name__ == "__main__":
    unittest.main()
