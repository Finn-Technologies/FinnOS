"""
The theme: the tokens a screen refers to.

`ui/_tokens.fui` declares the theme. A token is one name and — for the
colours — two values, because the appearance switch is a property of the
whole theme and never of a single surface. A colour is declared as a pair:

    color: surface_light #F7F9FC, surface_dark #0B0E14

which is one token, `surface`, with a light half and a dark half.

Everything else is declared once:

    radius: sm 8, md 12, lg 24
    type:   body 14/20 "Fixel Display" 400
    motion: theme_xfade 180ms linear
"""

from .parser import v_colour, v_ident, v_int, v_map, v_num, v_str


def _int_of(value, default=0):
    if value.get("k") == "int":
        return value["v"]
    if value.get("k") == "num":
        return int(value["v"])
    if value.get("k") == "phrase":
        for atom in value["v"]:
            if atom.get("k") in ("int", "num"):
                return atom["v"]
    return default


def _str_of(value, default=""):
    if value.get("k") == "str":
        return value["v"]
    if value.get("k") == "ident":
        return value["v"]
    if value.get("k") == "phrase":
        for atom in value["v"]:
            if atom.get("k") == "str":
                return atom["v"]
    return default


def _atoms(value):
    """The atoms of a value, whether it is a phrase or a single thing."""
    if value.get("k") == "phrase":
        return value["v"]
    return [value]


def _flatten(value):
    """The atoms of a value, with a number and its unit pulled apart.

    `180ms` is one thing in the source and two in the theme: the number
    and the unit. The theme is read as data, so the unit is flattened out
    here rather than in the parser.
    """
    out = []
    for atom in _atoms(value):
        inner = atom.get("v") if atom.get("k") == "phrase" else None
        if (inner and len(inner) == 2
                and inner[0].get("k") in ("int", "num")
                and inner[1].get("k") == "ident"):
            out.append(inner[0])
            out.append(inner[1])
            continue
        out.append(atom)
    return out


def _type_token(value):
    """`14/20 "Fixel Display" 400` becomes a type, not a phrase."""
    atoms = _flatten(value)
    size = line = weight = None
    family = None
    i = 0
    while i < len(atoms):
        atom = atoms[i]
        if atom.get("k") in ("int", "num") and i + 2 < len(atoms) \
                and atoms[i + 1].get("k") == "str" and atoms[i + 1]["v"] == "/":
            size = atom["v"]
            line = atoms[i + 2].get("v")
            i += 3
            continue
        if atom.get("k") == "str" and family is None:
            family = atom["v"]
        elif atom.get("k") in ("int", "num") and weight is None:
            weight = atom["v"]
        i += 1
    return v_map([
        ("size", v_int(size if size is not None else 0)),
        ("line", v_int(line if line is not None else 0)),
        ("family", v_str(family or "")),
        ("weight", v_int(weight if weight is not None else 400)),
    ])


def _motion_token(value):
    """`180ms linear` becomes a motion, not a phrase."""
    atoms = _flatten(value)
    duration = 0
    unit = ""
    easing = "linear"
    for atom in atoms:
        if atom.get("k") in ("int", "num") and not duration:
            duration = atom["v"]
        elif atom.get("k") == "ident" and not unit:
            unit = atom["v"]
        elif atom.get("k") == "ident" and unit:
            easing = atom["v"]
    return v_map([
        ("duration", v_int(duration)),
        ("unit", v_str(unit)),
        ("easing", v_str(easing)),
    ])


class Theme:
    """The token table, and the resolution of `token.name` references."""

    def __init__(self):
        self.tokens = {}
        self.warnings = []

    @classmethod
    def from_items(cls, items):
        theme = cls()
        for item in items:
            if item.kind == "theme":
                theme._read(item)
        theme._pair_colours()
        return theme

    def _read(self, node):
        for kind, pairs in node.props.items():
            for name, value in pairs:
                if kind == "color":
                    if value.get("k") == "colour":
                        self.tokens[name] = v_colour(value["v"])
                    else:
                        self.warnings.append(
                            "colour %r is not a colour" % name)
                elif kind == "radius":
                    self.tokens[name] = v_int(_int_of(value))
                elif kind == "type":
                    self.tokens[name] = _type_token(value)
                elif kind == "motion":
                    self.tokens[name] = _motion_token(value)
                elif kind == "font":
                    self.tokens["font." + name] = value
                elif kind in ("gradient", "effect"):
                    self.tokens[name] = value
                else:
                    self.tokens[name] = value

    def _pair_colours(self):
        """
        A colour declared as a light half and a dark half is one token.

        `surface_light` and `surface_dark` become `surface`, a themed
        colour: the renderer picks the half that matches the appearance.
        The halves stay in the table too, so a screen that wants one
        particular half can ask for it by name.
        """
        for name in list(self.tokens):
            if not name.endswith("_light"):
                continue
            base = name[:-len("_light")]
            dark = base + "_dark"
            if dark in self.tokens:
                self.tokens[base] = {
                    "k": "themed",
                    "light": self.tokens[name],
                    "dark": self.tokens[dark],
                }

    def get(self, name):
        return self.tokens.get(name)

    def resolve(self, value):
        """Replace every `token.name` reference in a value."""
        kind = value.get("k")
        if kind == "ident":
            name = value["v"]
            if name.startswith("token."):
                key = name[len("token."):]
                resolved = self.tokens.get(key)
                if resolved is None:
                    self.warnings.append("unknown token %r" % name)
                    return value
                return resolved
            return value
        if kind == "list":
            return {"k": "list", "v": [self.resolve(v) for v in value["v"]]}
        if kind == "map":
            return {"k": "map",
                    "v": [(k, self.resolve(v)) for k, v in value["v"]]}
        if kind == "phrase":
            return {"k": "phrase", "v": [self.resolve(v) for v in value["v"]]}
        if kind == "arrow":
            return {"k": "arrow", "v": [self.resolve(v) for v in value["v"]]}
        if kind == "combine":
            return {"k": "combine", "v": [self.resolve(v) for v in value["v"]]}
        if kind == "call":
            name, args = value["v"]
            return {"k": "call", "v": [name, self.resolve(args)]}
        return value

    def resolve_node(self, node):
        node.props = {k: self.resolve(v) for k, v in node.props.items()}
        node.args = [self.resolve(v) for v in node.args]
        for child in node.children:
            self.resolve_node(child)
