# The `.fbin` format

`.fbin` is the compiled form of a `.fui` screen: what the system loads,
and what a renderer reads. It is binary rather than JSON on purpose — a
compiled artefact is read for every frame it draws, it should not be
parsed with a text parser, and it should not carry the punctuation of a
language nobody is going to read it in.

This document is the specification. The writer and the reader are both
in `fbin.py`, and the tests prove that they agree; if the code and this
document ever disagree, the code is wrong until one of them changes.

## Conventions

- **Byte order is little-endian** everywhere. Every integer and every
  float in the file is little-endian.
- **Offsets are byte offsets from the start of the thing being
  described**, so a reader can walk the file without seeking around.
- A `u8`, `u16`, `u32` is an unsigned integer of 1, 2 and 4 bytes. An
  `i64` is a signed 64-bit integer. An `f64` is an IEEE 754 double.
- A **string** is a `u16` byte length followed by that many bytes of
  UTF-8. The length is in bytes, not characters, and a string longer
  than 65535 bytes cannot be written.
- A **value** is a `u8` kind followed by a payload whose shape depends
  on the kind.
- A **node** is a screen or an element: a name, its properties, its
  arguments and its children.

## The file

```
+---------------------------+
| magic      "FBIN"   4 bytes
| version    u16      = 1
| flags      u16      = 0
| body_len   u32
+---------------------------+
| body       body_len bytes
+---------------------------+
```

The header is twelve bytes. `magic` is the four bytes `FBIN`; a file
that does not start with them is not a `.fbin`. `version` is the format
version, and a reader that does not know it must refuse the file rather
than guess. `flags` is reserved and is written as zero. `body_len` is
the length of the body, which follows the header immediately.

## The body

The body is a list of sections. A section is a name and a payload, and
the payload is opaque to the reader: only the `screen` section has a
known shape.

```
section_count   u16
section*        section_count times
```

A section is:

```
name_len        u8
name            name_len bytes of UTF-8
payload_len     u32
payload         payload_len bytes
```

The section name is at most 255 bytes. A reader that does not know a
section name skips it.

### The sections a compile writes

| Section | What it holds |
|---|---|
| `target:<name>` | one node: the adaptive target, one section per target |
| `screen` | one node: the screen |
| `screen:<name>` | one node: a further screen, when a file declares more than one |
| `tokens` | one value: the theme's token table, as a map |

The `screen` section holds the screen *after* the theme has been
resolved into it: a `token.surface` reference has already become the
colour it names. A colour that has a light half and a dark half stays
themed — the appearance switch is a property of the whole theme, and
the renderer picks the half that matches.

The `tokens` section holds the theme as it was, so that a tool can see
what the screen was compiled against. Its keys are sorted, so the same
theme always compiles to the same bytes.

A file that declares only a theme or only targets has no `screen`
section. That is a library, not a screen, and it compiles to a binary
with the sections it does have.

## A node

```
kind        u8      1 = screen, 2 = element
name        string
prop_count  u16
prop*       prop_count times
arg_count   u16
arg*        arg_count times
child_count u16
child*      child_count times
```

A **prop** is a string key followed by a value. The keys of a node are
unique except when a property was written more than once in the source:
a repeated property is one key whose value is a list of the values.

The children follow their parent, depth first, in the order they were
written. There is no back pointer: a reader walks the tree once.

## Values

The kind byte says what the value is.

| Kind | Number | Payload |
|---|---|---|
| null | 0 | nothing |
| string | 1 | a string |
| int | 2 | an `i64` |
| number | 3 | an `f64` |
| bool | 4 | a `u8`: 0 is false, anything else is true |
| list | 5 | a `u16` count, then that many values |
| map | 6 | a `u16` count, then that many string/value pairs |
| colour | 7 | a `u32`: `0x00RRGGBB` |
| themed | 8 | a string (the light half) and a string (the dark half) |
| arrow | 9 | two values: the left and the right |
| phrase | 10 | a `u16` count, then that many values |
| ident | 11 | a string: a name that is not a token reference |
| combine | 12 | two values: the two things that make one |
| call | 13 | a string (the name) and a value (the arguments) |

The kinds that carry more than one value are written in order, and the
order is part of the format: `arrow` is the left and then the right,
`combine` is the two sides, `call` is the name and then the arguments.

### The kinds in detail

**string** is text. It is not a name: a name is an `ident`, so that a
renderer can tell `style:title` from `label:"title"`.

**int** is a whole number, **number** is a real one. A number with a
unit — `96px`, `180ms`, `50%` — is a **phrase** of a number and an
`ident` that names the unit.

**colour** is `0x00RRGGBB`. The `#` is not written. A three-digit
colour in the source (`#FFF`) is expanded to six digits, and an
eight-digit colour (`#RRGGBBAA`) keeps its first six, because the
format has no alpha.

**themed** is a colour with two halves: the light one and the dark
one, both as strings with their `#`. The reader gives them back as
colours.

**map** is an ordered list of pairs, not a dictionary: the same key may
appear twice, and the order is the order in the source.

**phrase** is a sequence of values that belong together — the words of
a property, or a number and its unit.

**arrow** is a mapping written `a -> b`: the left and the right.

**combine** is two values that make one, written `a + b`.

**call** is a named call: `linear(180deg, ...)`. The name is a string
and the arguments are a value — a **list** when they are positional, a
**map** when they are named.

**ident** is a name: `title`, `system.theme`, `token.surface` when it
has not been resolved.

## An example

The screen

```
screen Notes {
  target: Desktop (primary)
  Column(padding:20, gap:12) {
    Text("Hello", style:title)
  }
}
```

compiles to a file whose body holds one `screen` section. That section
is a node:

```
kind        u8      1  (screen)
name        string  "Notes"
prop_count  u16    1
prop*       "target" -> a call:
                          the name "Desktop" and a list with one
                          ident, "primary"
arg_count   u16    0
child_count u16    1
child*      a node:
               kind        u8   2  (element)
               name        string  "Column"
               prop_count  u16    2
               prop*       "padding" -> int 20
                           "gap"     -> int 12
               arg_count   u16    0
               child_count u16    1
               child*      a node:
                            kind        u8   2  (element)
                            name        string  "Text"
                            prop_count  u16    1
                            prop*       "style" -> ident "title"
                            arg_count   u16    1
                            arg*        string  "Hello"
                            child_count u16    0
```

## Reading one in C

FinnOS core provides a bounded reader in `src/core/finnfbin.c` and
`finnfbin.h`. It owns a copy of the input, validates the header, sections,
values and scene-tree bounds, then exposes the decoded nodes and values to a
compositor-side renderer. The current reader does not draw the tree or launch
an app. The API's hard limits (16 MiB per file, 4096 nodes, 16384 values and
64 levels of nesting) keep malformed files from causing unbounded allocation
or recursion.

A compositor-side caller can load the scene and inspect its sections with
`finnfbin.h`:

```c
finn_fbin_document document;
char error[128];
if (finn_fbin_parse(bytes, length, &document, error, sizeof error) == 0) {
    const finn_fbin_section *screen = finn_fbin_find(&document, "screen");
    if (screen && screen->node) {
        /* Compose screen->node into finn-comp's DRM-owned frame. */
    }
    finn_fbin_free(&document);
}
```

The parser rejects unsupported versions and flags, truncation, invalid node
or value kinds, trailing bytes, non-finite numbers and inputs beyond its
resource limits. Unknown sections remain available as opaque payloads so a
future reader can add data without changing the file header.

## What is not in the format

- **No padding and no alignment.** Every field is at the offset the
  previous field ended at.
- **No checksums.** A `.fbin` is written once, by the compiler, and
  read by the system that wrote it.
- **No source.** A `.fbin` does not carry the `.fui` it came from, and
  it does not carry the line numbers. A renderer does not need them.
