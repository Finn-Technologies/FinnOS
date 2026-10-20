"""
The parser for .fui.

The grammar, in full:

    file     := item*
    item     := 'screen' IDENT '{' body '}'
              | 'theme'  IDENT '{' decl* '}'
              | 'target' IDENT '{' decl* '}'
    body     := stmt*
    stmt     := property | element
    property := IDENT ['(' args ')'] ':' value
    element  := IDENT ['(' args ')'] ['{' body '}']
    args     := arg (',' arg)*
    arg      := IDENT ':' value | value
    value    := atom+

An atom is a string, a number, a colour, an identifier, a bracketed
list, a map, a call or an operator. An arrow joins two atoms into a
mapping, a `+` joins two into one value, and several atoms in a row are
a phrase, which is what a property like `focus_ring: 2px accent when
keyboard_nav` is.

A theme's declaration is a list of items — `surface_light #F7F9FC` — and
a target's is a list of `key: value` declarations.

A statement ends at the end of the line, at a `;`, or at a comma that
begins the next property. In an argument list a comma ends the value,
because the comma belongs to the list.
"""

from .lexer import (
    ARROW, COLOUR, EOF, IDENT, NEWLINE, NUMBER, PUNCT, STRING, tokenize,
)


class ParseError(Exception):
    def __init__(self, message, line):
        super().__init__("line %d: %s" % (line, message))
        self.line = line


class Node:
    """One node of the tree: a screen, a theme, a target or an element."""

    __slots__ = ("kind", "name", "props", "args", "children", "line")

    def __init__(self, kind, name, line):
        self.kind = kind
        self.name = name
        self.props = {}
        self.args = []
        self.children = []
        self.line = line

    def get(self, key, default=None):
        return self.props.get(key, default)

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def __repr__(self):
        return "Node(%s, %r, %d children)" % (
            self.kind, self.name, len(self.children))


# ---- values ----

def v_str(text):
    return {"k": "str", "v": text}


def v_int(number):
    return {"k": "int", "v": int(number)}


def v_num(number):
    return {"k": "num", "v": float(number)}


def v_bool(flag):
    return {"k": "bool", "v": bool(flag)}


def v_ident(name):
    return {"k": "ident", "v": name}


def v_colour(text):
    return {"k": "colour", "v": text.upper()}


def v_list(items):
    return {"k": "list", "v": items}


def v_map(pairs):
    return {"k": "map", "v": pairs}


def v_arrow(left, right):
    return {"k": "arrow", "v": [left, right]}


def v_combine(left, right):
    """Two values that make one: `token.a + token.b`."""
    return {"k": "combine", "v": [left, right]}


def v_call(name, args):
    """A named call: `linear(180deg, ...)`, `surface(bind: ...)`."""
    return {"k": "call", "v": [name, args]}


def v_phrase(atoms):
    if len(atoms) == 1:
        return atoms[0]
    return {"k": "phrase", "v": atoms}


def _name_of(value):
    """The name of an argument, when it has one."""
    if value.get("k") == "ident":
        return value["v"]
    if value.get("k") == "str":
        return value["v"]
    return "@"


def _scoped(positional, named, value):
    """A property that has an argument list: `adapt(Mobile): value`.

    The argument says what the property applies to, so the value becomes
    a map from that scope to the value. A named argument keeps its own
    value, and the property's value is kept beside it.
    """
    if named:
        pairs = list(named)
        pairs.append(("value", value))
        return v_map(pairs)
    if positional:
        return v_map([(_name_of(arg), value) for arg in positional])
    return value


def _set_property(parent, name, value):
    """Set a property, keeping both when it is written twice.

    A screen says `adapt(Mobile): ...` and then `adapt(TV): ...`, and
    both are true. A dict would keep the last one and lose the first,
    so a repeated property becomes a list of its values.
    """
    existing = parent.props.get(name)
    if existing is None:
        parent.props[name] = value
        return
    if existing.get("k") == "list" and existing.get("repeat"):
        existing["v"].append(value)
        return
    parent.props[name] = {"k": "list", "v": [existing, value], "repeat": True}


class Parser:
    def __init__(self, text):
        self.tokens = tokenize(text)
        self.pos = 0

    # ---- token helpers ----

    def peek(self, offset=0):
        index = self.pos + offset
        if index >= len(self.tokens):
            return self.tokens[-1]
        return self.tokens[index]

    def next(self):
        token = self.peek()
        self.pos += 1
        return token

    def at_punct(self, ch):
        token = self.peek()
        return token.kind == PUNCT and token.value == ch

    def expect_punct(self, ch):
        token = self.peek()
        if not (token.kind == PUNCT and token.value == ch):
            raise ParseError("expected %r, found %r" % (ch, token.value), token.line)
        self.pos += 1
        return token

    def skip_newlines(self):
        while self.peek().kind == NEWLINE:
            self.pos += 1

    # ---- the grammar ----

    def parse_file(self):
        items = []
        while True:
            self.skip_newlines()
            token = self.peek()
            if token.kind == EOF:
                break
            if token.kind != IDENT:
                raise ParseError("expected a screen, theme or target", token.line)
            if token.value in ("screen", "theme", "target"):
                items.append(self.parse_item())
            else:
                raise ParseError("unknown top-level item %r" % token.value, token.line)
        return items

    def parse_item(self):
        kind = self.next().value
        name_token = self.next()
        if name_token.kind != IDENT:
            raise ParseError("expected a name after %r" % kind, name_token.line)
        node = Node(kind, name_token.value, name_token.line)
        self.expect_punct("{")
        while True:
            self.skip_newlines()
            token = self.peek()
            if token.kind == EOF:
                raise ParseError("unterminated %s" % kind, token.line)
            if token.kind == PUNCT and token.value == "}":
                self.pos += 1
                break
            if kind == "screen":
                self.parse_stmt(node)
            else:
                self.parse_decl(node, kind)
        return node

    def parse_stmt(self, parent):
        """A property or an element, inside a screen or an element body."""
        token = self.peek()
        if token.kind != IDENT:
            raise ParseError("expected a property or an element", token.line)

        # A property is `name: value`. An element is `Name(args) { ... }`.
        # The colon after the name settles it.
        save = self.pos
        name = self.next().value

        if self.at_punct("("):
            # The arguments may belong to a property — `adapt(Mobile): ...` —
            # or to an element — `Column(padding:20) { ... }`. The colon
            # after the closing bracket settles that.
            self.skip_balanced("(", ")")
            if self.at_punct(":"):
                # `adapt(Mobile): ...` — a property with an argument
                self.pos = save
                self.next()
                positional, named = self.parse_args()
                self.expect_punct(":")
                value = self.parse_value(stop_at_comma=False)
                self.end_of_statement()
                _set_property(parent, name, _scoped(positional, named, value))
                return None
            self.pos = save
            return self.parse_element(parent, save)

        if self.at_punct(":"):
            self.pos += 1
            value = self.parse_value(stop_at_comma=False)
            self.end_of_statement()
            _set_property(parent, name, value)
            return None

        self.pos = save
        return self.parse_element(parent, save)

    def parse_element(self, parent, start):
        # The caller has already decided this is an element, but the
        # decision was made by looking ahead; the element itself starts
        # at the name token, not where the look-ahead left off.
        self.pos = start
        token = self.next()
        name = token.value
        node = Node("element", name, token.line)

        if self.at_punct("("):
            positional, named = self.parse_args()
            node.args = positional
            node.props.update(named)

        if self.at_punct("{"):
            self.pos += 1
            while True:
                self.skip_newlines()
                inner = self.peek()
                if inner.kind == EOF:
                    raise ParseError("unterminated %s" % name, inner.line)
                if inner.kind == PUNCT and inner.value == "}":
                    self.pos += 1
                    break
                self.parse_stmt(node)
            self.end_of_statement(element=True)
        else:
            self.end_of_statement(element=True)

        parent.children.append(node)
        return node

    def parse_args(self):
        """The arguments of an element or a call: (positional, named)."""
        self.expect_punct("(")
        positional = []
        named = []
        if self.at_punct(")"):
            self.pos += 1
            return positional, named
        while True:
            self.skip_newlines()
            token = self.peek()
            if token.kind == EOF:
                raise ParseError("unterminated argument list", token.line)

            # `key: value`
            if token.kind == IDENT and self.peek(1).kind == PUNCT and self.peek(1).value == ":":
                key = self.next().value
                self.pos += 1
                named.append((key, self.parse_value()))
            else:
                positional.append(self.parse_value())

            self.skip_newlines()
            if self.at_punct(","):
                self.pos += 1
                continue
            self.expect_punct(")")
            return positional, named

    def parse_decl(self, parent, item_kind):
        """
        A declaration inside a theme or a target.

        A target is a list of `key: value` declarations. A theme is a list
        of items, and an item is a name and its value (`surface_light
        #F7F9FC`), a key and its value (`files: assets/fonts/...`), or a
        value on its own.
        """
        token = self.next()
        if token.kind != IDENT:
            raise ParseError("expected a declaration", token.line)
        kind = token.value
        self.expect_punct(":")

        if item_kind == "target":
            value = self.parse_value(stop_at_comma=False)
            self.end_of_statement()
            parent.props[kind] = value
            return

        pairs = []
        while True:
            pairs.append(self.parse_theme_item(kind))
            if self.at_punct(","):
                self.pos += 1
                continue
            break
        self.end_of_statement()
        parent.props.setdefault(kind, []).extend(pairs)

    def parse_theme_item(self, kind):
        """One item of a theme declaration."""
        token = self.peek()
        if token.kind == IDENT and self.peek(1).kind == PUNCT and self.peek(1).value == ":":
            # `files: assets/fonts/FixelDisplay/*.otf`
            name = self.next().value
            self.pos += 1
            return (name, v_phrase(self.parse_atoms()))

        atoms = self.parse_atoms()
        if len(atoms) >= 2 and atoms[0].get("k") == "ident":
            # `surface_light #F7F9FC` — a name and its value
            return (atoms[0]["v"], v_phrase(atoms[1:]))
        # a value on its own: the declaration's own name is the token
        return (kind, v_phrase(atoms))

    def parse_value(self, stop_at_comma=True):
        """
        The value of a property or an argument.

        A value is a list of atoms. In an argument list a comma ends the
        value, because the comma belongs to the list; in a property a comma
        joins the items of the value, unless what follows it is the next
        property.
        """
        return v_phrase(self.parse_atoms(stop_at_comma))

    def parse_atoms(self, stop_at_comma=True):
        """The atoms of a value."""
        atoms = []
        while True:
            token = self.peek()
            if token.kind == NEWLINE or token.kind == EOF:
                break
            if token.kind == PUNCT and token.value in (")", "}", "]", ";"):
                break
            if token.kind == PUNCT and token.value == ",":
                if stop_at_comma:
                    break
                if self.next_is_property():
                    break
                self.pos += 1
                continue
            atoms.append(self.parse_atom())
            if self.peek().kind == ARROW:
                self.pos += 1
                right = self.parse_atom()
                atoms[-1] = v_arrow(atoms[-1], right)
            if self.at_punct("+"):
                if self.starts_atom(1):
                    self.pos += 1
                    right = self.parse_atom()
                    atoms[-1] = v_combine(atoms[-1], right)
                else:
                    # a `+` with nothing after it: an open range, `800w+`
                    self.pos += 1
                    atoms.append(v_str("+"))

        if not atoms:
            raise ParseError("expected a value", self.peek().line)
        return atoms

    def next_is_property(self):
        """Does a property start at the next token?"""
        return (self.peek(1).kind == IDENT
                and self.peek(2).kind == PUNCT
                and self.peek(2).value == ":")

    def starts_atom(self, offset=0):
        """Can the token at this offset begin a value?"""
        token = self.peek(offset)
        if token.kind in (NEWLINE, EOF):
            return False
        if token.kind == PUNCT and token.value in (")", "}", "]", ";", ","):
            return False
        return True

    def parse_atom(self):
        token = self.next()

        if token.kind == STRING:
            return v_str(token.value)

        if token.kind == COLOUR:
            return v_colour(token.value)

        if token.kind == NUMBER:
            if token.extra:
                # a number with a unit: 96px, 180ms, 50%
                return {"k": "phrase", "v": [v_num(token.value), v_ident(token.extra)]}
            return v_int(token.value) if isinstance(token.value, int) else v_num(token.value)

        if token.kind == IDENT:
            if token.value == "true":
                return v_bool(True)
            if token.value == "false":
                return v_bool(False)
            if self.at_punct("("):
                # a call: `linear(180deg, ...)`, `surface(bind: ...)`
                positional, named = self.parse_args()
                if named:
                    args = v_map(named)
                else:
                    args = v_list(positional)
                return v_call(v_ident(token.value), args)
            # a dotted path: system.theme
            name = token.value
            while self.at_punct(".") and self.peek(1).kind == IDENT:
                self.pos += 1
                name += "." + self.next().value
            return v_ident(name)

        if token.kind == PUNCT and token.value == "[":
            items = []
            while True:
                self.skip_newlines()
                if self.at_punct("]"):
                    self.pos += 1
                    break
                items.append(self.parse_value())
                self.skip_newlines()
                if self.at_punct(","):
                    self.pos += 1
                    continue
            return v_list(items)

        if token.kind == PUNCT and token.value == "(":
            # a group: linear(180deg, wp_edge 0%, ...)
            items = []
            while True:
                self.skip_newlines()
                if self.at_punct(")"):
                    self.pos += 1
                    break
                items.append(self.parse_value())
                self.skip_newlines()
                if self.at_punct(","):
                    self.pos += 1
                    continue
            return v_list(items)

        if token.kind == PUNCT and token.value == "{":
            pairs = []
            while True:
                self.skip_newlines()
                if self.at_punct("}"):
                    self.pos += 1
                    break
                key_token = self.next()
                if key_token.kind not in (IDENT, STRING):
                    raise ParseError("expected a key", key_token.line)
                self.expect_punct(":")
                pairs.append((key_token.value, self.parse_value()))
                self.skip_newlines()
                if self.at_punct(","):
                    self.pos += 1
            return v_map(pairs)

        if token.kind == PUNCT and token.value in ("/", "<", ">", "*", "-", ":", ".", "@", "%"):
            # An operator with no meaning of its own yet: the type scale
            # writes `14/20`, a target writes `reflow <150ms`, a range
            # writes `600-1366w`, an opacity writes `token.ink @ 0.10`.
            # It is kept as text rather than dropped, so the renderer
            # can read it.
            return v_str(token.value)

        raise ParseError("unexpected %r" % (token.value,), token.line)

    def end_of_statement(self, element=False):
        """A statement ends at the end of the line, at a `;`, or at a
        comma that begins the next property."""
        while True:
            token = self.peek()
            if token.kind == NEWLINE:
                self.pos += 1
                return
            if token.kind == EOF:
                return
            if token.kind == PUNCT and token.value in ("}", ")", "]"):
                return
            if token.kind == PUNCT and token.value == ";":
                self.pos += 1
                return
            if token.kind == PUNCT and token.value == ",":
                # A comma ends the statement when the next property
                # starts here; otherwise it belongs to the value, and
                # the value parser has already taken it.
                self.pos += 1
                return
            if element and token.kind == IDENT:
                # `Item("Home") Item("Documents")` — an element ends
                # where the next one begins.
                return
            raise ParseError("unexpected %r at the end of a statement" % (token.value,),
                             token.line)

    def skip_balanced(self, open_ch, close_ch):
        depth = 0
        while True:
            token = self.peek()
            if token.kind == EOF:
                raise ParseError("unterminated %r" % open_ch, token.line)
            if token.kind == PUNCT and token.value == open_ch:
                depth += 1
            elif token.kind == PUNCT and token.value == close_ch:
                depth -= 1
                if depth == 0:
                    self.pos += 1
                    return
            self.pos += 1


def parse(text):
    """Parse a .fui source into a list of top-level items."""
    return Parser(text).parse_file()
