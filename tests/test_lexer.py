"""The lexer: the tokens a `.fui` source is made of."""

import os
import sys
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

from findsdk.fuic.lexer import (
    ARROW, COLOUR, EOF, IDENT, NEWLINE, NUMBER, PUNCT, STRING, LexError,
    tokenize,
)


def tokens(text):
    """The tokens, without the end-of-file marker."""
    out = tokenize(text)
    if out and out[-1].kind == EOF:
        out = out[:-1]
    return out


def kinds(text):
    return [token.kind for token in tokens(text)]


def values(text):
    return [token.value for token in tokens(text)]


class LexerTest(unittest.TestCase):

    def test_names_and_punctuation(self):
        self.assertEqual(kinds("Column(padding:20)"),
                         [IDENT, PUNCT, IDENT, PUNCT, NUMBER, PUNCT])
        self.assertEqual(values("Column(padding:20)"),
                         ["Column", "(", "padding", ":", 20, ")"])

    def test_a_line_comment_is_dropped(self):
        self.assertEqual(kinds("// nothing here\nx"), [NEWLINE, IDENT])

    def test_a_block_comment_is_dropped(self):
        self.assertEqual(kinds("/* nothing\nhere */x"), [IDENT])

    def test_a_comment_marker_inside_a_word_is_not_a_comment(self):
        # a font path: `FixelDisplay/*.otf` is a path, not a comment
        self.assertNotIn("/*", [t.value for t in tokens("assets/FixelDisplay/*.otf")])

    def test_strings_keep_their_text(self):
        self.assertEqual(tokens('"Hello, world"')[0].kind, STRING)
        self.assertEqual(tokens('"Hello, world"')[0].value, "Hello, world")

    def test_a_string_may_contain_a_quote_escape(self):
        self.assertEqual(tokens('"a\\"b"')[0].value, 'a"b')

    def test_an_unterminated_string_is_an_error(self):
        with self.assertRaises(LexError):
            tokenize('"open')

    def test_an_unterminated_block_comment_is_an_error(self):
        with self.assertRaises(LexError):
            tokenize("/* open")

    def test_numbers(self):
        self.assertEqual(kinds("1 2.5 -3"), [NUMBER] * 3)
        self.assertEqual(values("1 2.5 -3"), [1, 2.5, -3])

    def test_a_number_may_carry_a_unit(self):
        found = tokens("96px 180ms 50%")
        self.assertEqual([t.extra for t in found], ["px", "ms", "%"])
        self.assertEqual([t.value for t in found], [96, 180, 50])

    def test_a_hyphen_between_numbers_is_a_range_not_a_sign(self):
        # `600-1366w` is a viewport, not a viewport of -1366
        found = tokens("600-1366w")
        self.assertEqual([t.kind for t in found], [NUMBER, PUNCT, NUMBER])
        self.assertEqual(found[2].extra, "w")

    def test_a_minus_on_its_own_is_a_sign(self):
        self.assertEqual(tokens("-1")[0].value, -1)

    def test_colours(self):
        found = tokens("#F7F9FC")
        self.assertEqual(found[0].kind, COLOUR)
        self.assertEqual(found[0].value, "#F7F9FC")

    def test_an_arrow(self):
        self.assertEqual(kinds("windows->fullscreen"), [IDENT, ARROW, IDENT])

    def test_a_name_may_carry_a_hyphen(self):
        self.assertEqual(tokens("en-US")[0].value, "en-US")

    def test_newlines_end_the_lines(self):
        self.assertEqual(kinds("a\nb"), [IDENT, NEWLINE, IDENT])

    def test_an_unexpected_character_is_an_error(self):
        with self.assertRaises(LexError):
            tokenize("a ? b")

    def test_the_end_of_the_file_is_a_token(self):
        self.assertEqual(tokenize("")[0].kind, EOF)


if __name__ == "__main__":
    unittest.main()
