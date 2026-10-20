"""
The .fbin format: what `fuic` writes and what a renderer reads.

The format is documented in `FORMAT.md`. In short: a twelve-byte header, a
list of named sections, and inside the `screen` section a tree of elements
whose properties are typed values.

It is binary rather than JSON on purpose. A `.fbin` is a compiled artefact
that a renderer reads for every frame it draws; it should not be parsed
with a text parser, and it should not carry the punctuation of a language
nobody is going to read it in.

The reader is here so that the format has exactly one implementation and
the tests can prove the writer and the reader agree.
"""

import struct

MAGIC = b"FBIN"
VERSION = 1

# The value kinds.
K_NULL = 0
K_STR = 1
K_INT = 2
K_NUM = 3
K_BOOL = 4
K_LIST = 5
K_MAP = 6
K_COLOUR = 7
K_THEMED = 8
K_ARROW = 9
K_PHRASE = 10
K_IDENT = 11
K_COMBINE = 12
K_CALL = 13

# The element kinds.
E_SCREEN = 1
E_ELEMENT = 2


class FbinError(Exception):
    pass


# ---- writing ----

def _u8(value):
    return struct.pack("<B", value)


def _u16(value):
    return struct.pack("<H", value)


def _u32(value):
    return struct.pack("<I", value)


def _i64(value):
    return struct.pack("<q", value)


def _f64(value):
    return struct.pack("<d", value)


def _string(text):
    data = text.encode("utf-8")
    if len(data) > 0xFFFF:
        raise FbinError("string longer than 65535 bytes")
    return _u16(len(data)) + data


def encode_value(value):
    kind = value.get("k")
    if kind == "str":
        return _u8(K_STR) + _string(value["v"])
    if kind == "ident":
        # A name that is not a token reference — a style, a layout mode.
        # It is a name and not a string, because a renderer has to be
        # able to tell `style:title` from `label:"title"`.
        return _u8(K_IDENT) + _string(value["v"])
    if kind == "int":
        return _u8(K_INT) + _i64(int(value["v"]))
    if kind == "num":
        return _u8(K_NUM) + _f64(float(value["v"]))
    if kind == "bool":
        return _u8(K_BOOL) + _u8(1 if value["v"] else 0)
    if kind == "colour":
        text = value["v"].lstrip("#")
        if len(text) == 3:
            text = "".join(c * 2 for c in text)
        if len(text) == 8:
            text = text[:6]
        if len(text) != 6:
            raise FbinError("bad colour %r" % value["v"])
        return _u8(K_COLOUR) + struct.pack("<I", int(text, 16))
    if kind == "themed":
        return (_u8(K_THEMED)
                + _string(value["light"].get("v", ""))
                + _string(value["dark"].get("v", "")))
    if kind == "list":
        items = value["v"]
        out = _u8(K_LIST) + _u16(len(items))
        for item in items:
            out += encode_value(item)
        return out
    if kind == "map":
        pairs = value["v"]
        out = _u8(K_MAP) + _u16(len(pairs))
        for key, item in pairs:
            out += _string(key) + encode_value(item)
        return out
    if kind == "arrow":
        left, right = value["v"]
        return _u8(K_ARROW) + encode_value(left) + encode_value(right)
    if kind == "combine":
        left, right = value["v"]
        return _u8(K_COMBINE) + encode_value(left) + encode_value(right)
    if kind == "call":
        name, args = value["v"]
        return (_u8(K_CALL)
                + _string(name.get("v", ""))
                + encode_value(args))
    if kind == "phrase":
        items = value["v"]
        out = _u8(K_PHRASE) + _u16(len(items))
        for item in items:
            out += encode_value(item)
        return out
    if kind is None:
        return _u8(K_NULL)
    raise FbinError("unknown value kind %r" % kind)


def encode_node(node):
    out = _u8(E_SCREEN if node.kind == "screen" else E_ELEMENT)
    out += _string(node.name)
    out += _u16(len(node.props))
    for key, value in node.props.items():
        out += _string(key) + encode_value(value)
    out += _u16(len(node.args))
    for value in node.args:
        out += encode_value(value)
    out += _u16(len(node.children))
    for child in node.children:
        out += encode_node(child)
    return out


def write(sections):
    """
    Build a .fbin from a list of (name, payload bytes) sections.
    """
    body = _u16(len(sections))
    for name, payload in sections:
        if len(name.encode("utf-8")) > 255:
            raise FbinError("section name longer than 255 bytes")
        body += _u8(len(name)) + name.encode("utf-8")
        body += _u32(len(payload)) + payload

    header = MAGIC + _u16(VERSION) + _u16(0) + _u32(len(body))
    return header + body


def compile_screen(node):
    """The payload of the `screen` section."""
    return encode_node(node)


# ---- reading ----

class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, count):
        if self.pos + count > len(self.data):
            raise FbinError("truncated")
        out = self.data[self.pos:self.pos + count]
        self.pos += count
        return out

    def u8(self):
        return struct.unpack("<B", self.take(1))[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i64(self):
        return struct.unpack("<q", self.take(8))[0]

    def f64(self):
        return struct.unpack("<d", self.take(8))[0]

    def string(self):
        return self.take(self.u16()).decode("utf-8", "replace")


def decode_value(reader):
    kind = reader.u8()
    if kind == K_NULL:
        return {"k": "null", "v": None}
    if kind == K_STR:
        return {"k": "str", "v": reader.string()}
    if kind == K_IDENT:
        return {"k": "ident", "v": reader.string()}
    if kind == K_INT:
        return {"k": "int", "v": reader.i64()}
    if kind == K_NUM:
        return {"k": "num", "v": reader.f64()}
    if kind == K_BOOL:
        return {"k": "bool", "v": bool(reader.u8())}
    if kind == K_COLOUR:
        rgb = reader.u32()
        return {"k": "colour", "v": "#%06X" % rgb}
    if kind == K_THEMED:
        light = reader.string()
        dark = reader.string()
        return {"k": "themed", "light": {"k": "colour", "v": light},
                "dark": {"k": "colour", "v": dark}}
    if kind == K_LIST:
        count = reader.u16()
        return {"k": "list", "v": [decode_value(reader) for _ in range(count)]}
    if kind == K_MAP:
        count = reader.u16()
        pairs = []
        for _ in range(count):
            key = reader.string()
            pairs.append((key, decode_value(reader)))
        return {"k": "map", "v": pairs}
    if kind == K_ARROW:
        left = decode_value(reader)
        right = decode_value(reader)
        return {"k": "arrow", "v": [left, right]}
    if kind == K_COMBINE:
        left = decode_value(reader)
        right = decode_value(reader)
        return {"k": "combine", "v": [left, right]}
    if kind == K_CALL:
        name = reader.string()
        args = decode_value(reader)
        return {"k": "call", "v": [{"k": "ident", "v": name}, args]}
    if kind == K_PHRASE:
        count = reader.u16()
        return {"k": "phrase", "v": [decode_value(reader) for _ in range(count)]}
    raise FbinError("unknown value kind %d" % kind)


def decode_node(reader):
    kind = reader.u8()
    if kind not in (E_SCREEN, E_ELEMENT):
        raise FbinError("unknown node kind %d" % kind)
    name = reader.string()

    props = {}
    for _ in range(reader.u16()):
        key = reader.string()
        props[key] = decode_value(reader)

    args = [decode_value(reader) for _ in range(reader.u16())]
    children = [decode_node(reader) for _ in range(reader.u16())]

    return {"kind": "screen" if kind == E_SCREEN else "element",
            "name": name, "props": props, "args": args, "children": children}


def read(data):
    """Read a .fbin into a dict of sections, with the screen decoded."""
    reader = Reader(data)
    magic = reader.take(4)
    if magic != MAGIC:
        raise FbinError("not a .fbin")
    version = reader.u16()
    if version != VERSION:
        raise FbinError("unsupported version %d" % version)
    reader.u16()                      # flags
    length = reader.u32()
    body = reader.take(length)

    inner = Reader(body)
    sections = {}
    for _ in range(inner.u16()):
        name = inner.take(inner.u8()).decode("utf-8")
        payload = inner.take(inner.u32())
        if (name == "screen" or name.startswith("screen:")) and payload:
            sections[name] = decode_node(inner.__class__(payload))
        else:
            sections[name] = payload
    return sections
