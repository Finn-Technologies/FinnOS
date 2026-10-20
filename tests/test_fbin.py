"""The `.fbin` format: the writer and the reader, and the round trip.

The format is specified in `sdk/findsdk/fuic/FORMAT.md`. These tests
are the other half of that document: if the code and the document ever
disagree, one of these fails.
"""

import struct
import os
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

from findsdk.fuic import fbin


def value(kind, payload):
    return {"k": kind, "v": payload}


class FbinTest(unittest.TestCase):

    # ---- the header ----

    def test_the_header_is_twelve_bytes(self):
        data = fbin.write([])
        self.assertEqual(len(data), 12 + 2)
        self.assertEqual(data[0:4], b"FBIN")

    def test_the_version_is_one(self):
        data = fbin.write([])
        self.assertEqual(struct.unpack("<H", data[4:6])[0], fbin.VERSION)

    def test_the_flags_are_reserved_and_zero(self):
        data = fbin.write([])
        self.assertEqual(struct.unpack("<H", data[6:8])[0], 0)

    def test_the_body_length_is_the_length_of_the_body(self):
        data = fbin.write([("a", b"12345")])
        length = struct.unpack("<I", data[8:12])[0]
        self.assertEqual(length, len(data) - 12)

    def test_a_file_that_is_not_a_fbin_is_refused(self):
        with self.assertRaises(fbin.FbinError):
            fbin.read(b"NOPE" + b"\0" * 20)

    def test_an_unknown_version_is_refused(self):
        data = bytearray(fbin.write([]))
        data[4:6] = struct.pack("<H", 99)
        with self.assertRaises(fbin.FbinError):
            fbin.read(bytes(data))

    # ---- sections ----

    def test_sections_keep_their_names_and_payloads(self):
        data = fbin.write([("one", b"12345"), ("two", b"")])
        sections = fbin.read(data)
        self.assertEqual(sections["one"], b"12345")
        self.assertEqual(sections["two"], b"")

    def test_a_section_name_is_one_byte_long_at_most(self):
        with self.assertRaises(fbin.FbinError):
            fbin.write([("x" * 256, b"")])

    # ---- values ----

    def round_trip(self, value):
        # the `screen` section is read back as a node, so a value goes
        # into the `tokens` section, whose payload is read as a value
        data = fbin.write([("tokens", fbin.encode_value(value))])
        payload = fbin.read(data)["tokens"]
        return fbin.decode_value(fbin.Reader(payload))

    def test_a_string_round_trips(self):
        self.assertEqual(self.round_trip(value("str", "Hello")),
                         value("str", "Hello"))

    def test_an_int_round_trips(self):
        self.assertEqual(self.round_trip(value("int", -42)),
                         value("int", -42))

    def test_a_number_round_trips(self):
        self.assertEqual(self.round_trip(value("num", 1.5)),
                         value("num", 1.5))

    def test_a_bool_round_trips(self):
        self.assertEqual(self.round_trip(value("bool", True)),
                         value("bool", True))
        self.assertEqual(self.round_trip(value("bool", False)),
                         value("bool", False))

    def test_a_colour_round_trips_without_its_hash(self):
        got = self.round_trip(value("colour", "#7AB8FF"))
        self.assertEqual(got, value("colour", "#7AB8FF"))

    def test_a_three_digit_colour_is_expanded(self):
        got = self.round_trip(value("colour", "#FFF"))
        self.assertEqual(got, value("colour", "#FFFFFF"))

    def test_an_eight_digit_colour_keeps_its_first_six(self):
        got = self.round_trip(value("colour", "#7AB8FF80"))
        self.assertEqual(got, value("colour", "#7AB8FF"))

    def test_a_themed_colour_round_trips(self):
        themed = {"k": "themed",
                  "light": value("colour", "#F7F9FC"),
                  "dark": value("colour", "#0B0E14")}
        self.assertEqual(self.round_trip(themed), themed)

    def test_a_list_round_trips(self):
        got = self.round_trip(value("list", [value("int", 1), value("str", "two")]))
        self.assertEqual(got, value("list", [value("int", 1), value("str", "two")]))

    def test_a_map_round_trips_and_keeps_its_order(self):
        pairs = [("b", value("int", 2)), ("a", value("int", 1))]
        got = self.round_trip(value("map", pairs))
        self.assertEqual([key for key, _ in got["v"]], ["b", "a"])

    def test_an_arrow_round_trips(self):
        arrow = value("arrow", [value("ident", "a"), value("ident", "b")])
        self.assertEqual(self.round_trip(arrow), arrow)

    def test_a_phrase_round_trips(self):
        phrase = value("phrase", [value("num", 2.0), value("ident", "px")])
        self.assertEqual(self.round_trip(phrase), phrase)

    def test_an_ident_round_trips(self):
        self.assertEqual(self.round_trip(value("ident", "title")),
                         value("ident", "title"))

    def test_a_combine_round_trips(self):
        combine = value("combine", [value("ident", "a"), value("ident", "b")])
        self.assertEqual(self.round_trip(combine), combine)

    def test_a_call_round_trips(self):
        call = value("call", [value("ident", "linear"),
                               value("list", [value("ident", "180deg")])])
        self.assertEqual(self.round_trip(call), call)

    def test_an_unknown_kind_is_refused(self):
        with self.assertRaises(fbin.FbinError):
            fbin.encode_value(value("nonesuch", 1))

    # ---- nodes ----

    def test_a_node_round_trips(self):
        source = """
            screen Notes {
              target: Desktop (primary)
              Column(padding:20, gap:12) {
                Text("Hello", style:title)
              }
            }
        """
        from findsdk.fuic.parser import parse
        node = parse(source)[0]

        data = fbin.write([("screen", fbin.compile_screen(node))])
        got = fbin.read(data)["screen"]

        self.assertEqual(got["kind"], "screen")
        self.assertEqual(got["name"], "Notes")
        self.assertEqual(len(got["children"]), 1)

        column = got["children"][0]
        self.assertEqual(column["kind"], "element")
        self.assertEqual(column["name"], "Column")
        self.assertEqual(column["props"]["padding"], value("int", 20))
        self.assertEqual(len(column["children"]), 1)

        text = column["children"][0]
        self.assertEqual(text["name"], "Text")
        self.assertEqual(text["props"]["style"], value("ident", "title"))
        self.assertEqual(text["args"], [value("str", "Hello")])

    def test_a_truncated_file_is_refused(self):
        data = fbin.write([("one", b"12345")])
        with self.assertRaises(fbin.FbinError):
            fbin.read(data[:-3])


if __name__ == "__main__":
    unittest.main()
