"""
The lexer for .fui.

The language is line-oriented: a statement ends at the end of the line,
and a newline inside brackets or parentheses is only whitespace. That is
what lets a property be a phrase — `focus_ring: 2px accent when
keyboard_nav` — without the grammar having to know about every kind of
phrase there is.
"""

import re

# The token kinds.
IDENT = "ident"
STRING = "string"
NUMBER = "number"
COLOUR = "colour"
ARROW = "arrow"
PUNCT = "punct"
NEWLINE = "newline"
EOF = "eof"

_PUNCT = set("{}():,[]#=<>./+;*-@%")

_NUMBER_RE = re.compile(r"\d+(\.\d+)?")
# A name may carry a hyphen: `en-US`, `repeating-x`. The hyphen has to
# be between two words, or `windows->fullscreen` would read as one name.
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:-[A-Za-z0-9_]+)*")
_COLOUR_RE = re.compile(r"#[0-9A-Fa-f]{3,8}")


def _starts_word(text, index):
    """Does a marker at this index begin a word rather than sit inside one?"""
    if index == 0:
        return True
    return text[index - 1] in " \t\r\n"


def _inside_word(text, index):
    """Is this index inside a word, so a `-` is a range and not a sign?"""
    if index == 0:
        return False
    return text[index - 1].isalnum() or text[index - 1] in "_-"


class LexError(Exception):
    def __init__(self, message, line):
        super().__init__("line %d: %s" % (line, message))
        self.line = line


class Token:
    __slots__ = ("kind", "value", "line", "extra")

    def __init__(self, kind, value, line, extra=None):
        self.kind = kind
        self.value = value
        self.line = line
        self.extra = extra

    def __repr__(self):
        return "Token(%s, %r, line=%d)" % (self.kind, self.value, self.line)


def tokenize(text):
    """Turn source into a list of tokens."""
    tokens = []
    line = 1
    i = 0
    n = len(text)

    while i < n:
        ch = text[i]

        if ch == "\n":
            tokens.append(Token(NEWLINE, "\n", line))
            line += 1
            i += 1
            continue

        if ch in " \t\r":
            i += 1
            continue

        # comments. A comment marker has to start a word: a path like
        # `assets/fonts/*.otf` is not a comment.
        if text.startswith("//", i) and _starts_word(text, i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i) and _starts_word(text, i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise LexError("unterminated comment", line)
            line += text.count("\n", i, end)
            i = end + 2
            continue

        # strings
        if ch == '"':
            j = i + 1
            out = []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    out.append(text[j + 1])
                    j += 2
                    continue
                if text[j] == "\n":
                    raise LexError("unterminated string", line)
                out.append(text[j])
                j += 1
            if j >= n:
                raise LexError("unterminated string", line)
            tokens.append(Token(STRING, "".join(out), line))
            i = j + 1
            continue

        # arrows: a -> b
        if text.startswith("->", i):
            tokens.append(Token(ARROW, "->", line))
            i += 2
            continue

        # colours
        m = _COLOUR_RE.match(text, i)
        if m:
            tokens.append(Token(COLOUR, m.group(0), line))
            i = m.end()
            continue

        # numbers, with an optional unit right after them: 96px, 180ms, 50%
        m = _NUMBER_RE.match(text, i)
        if m:
            raw = m.group(0)
            i = m.end()
            unit = None
            um = _IDENT_RE.match(text, i)
            if um:
                unit = um.group(0)
                i = um.end()
            elif text.startswith("%", i):
                # a percentage: the sign is part of the unit
                unit = "%"
                i += 1
            value = float(raw) if "." in raw else int(raw)
            tokens.append(Token(NUMBER, value, line, unit))
            continue

        # a negative number. A `-` between two numbers is a range, not a
        # sign: `600-1366w` is a viewport, not a viewport of -1366.
        if ch == "-" and i + 1 < n and text[i + 1].isdigit() and not _inside_word(text, i):
            m = _NUMBER_RE.match(text, i + 1)
            raw = m.group(0)
            i = m.end()
            unit = None
            um = _IDENT_RE.match(text, i)
            if um:
                unit = um.group(0)
                i = um.end()
            value = float(raw) if "." in raw else int(raw)
            tokens.append(Token(NUMBER, -value, line, unit))
            continue

        # identifiers
        m = _IDENT_RE.match(text, i)
        if m:
            tokens.append(Token(IDENT, m.group(0), line))
            i = m.end()
            continue

        if ch in _PUNCT:
            tokens.append(Token(PUNCT, ch, line))
            i += 1
            continue

        raise LexError("unexpected character %r" % ch, line)

    tokens.append(Token(EOF, "", line))
    return tokens
