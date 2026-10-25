# FinnSDK

The toolkit for building on FinnOS. It has two halves:

- **`fuic`**, the compiler: `.fui` in, `.fbin` out.
- **`finnsdk`**, the client library: talk to a running FinnOS over the
  control protocol.

FinnOS core now includes a bounded C reader for `.fbin` scene trees in
[`src/core/finnfbin.c`](../src/core/finnfbin.c). It is runtime groundwork
only: the compositor does not render those trees yet, and there is no `.fbin`
app host.

Neither needs a third-party package. The SDK has to run on whatever host
the agent runs on, and a toolkit that needs a wheel to answer a question
about a window is a toolkit nobody will use.

## Install

From a checkout:

```sh
pip install -e sdk
```

That puts `fuic` and `finn-sdk` on your path. To use the SDK without
installing anything, run the wrappers in `sdk/bin` — they put the SDK on
the path for you:

```sh
sdk/bin/fuic compile ui/Desktop.fui -o Desktop.fbin
sdk/bin/finn-sdk state
```

Python 3.8 or newer. Nothing else.

## The compiler

`fuic` turns a screen into the binary the system loads.

```sh
fuic compile ui/Desktop.fui -o Desktop.fbin    # compile
fuic check   ui/Desktop.fui                    # parse it and report
fuic dump    ui/Desktop.fui                    # print the tree
fuic new     "Finn Notes"                      # a new app from the template
```

A compile is three steps: parse the source, resolve the theme's tokens
into the tree, and write the binary. The tokens are resolved at compile
time on purpose — a renderer should not have to carry the theme to draw
one screen.

The tokens come from the theme the screen is compiled against: the
`_tokens.fui` beside the screen if there is one, and otherwise the
theme of the system, which is `ui/_tokens.fui`. A screen refers to
`token.surface` rather than to a colour: the appearance switch is a
property of the whole theme. `--theme` says which file to use instead,
and `--strict` turns the warnings into an error.

The language itself is described in
[../docs/ui-language.md](../docs/ui-language.md). The binary it produces
is described in [finnsdk/fuic/FORMAT.md](finnsdk/fuic/FORMAT.md), which
is precise enough to write a reader for.

### From Python

```python
from finnsdk.fuic import compile_file, compile_source, read_fbin

warnings, data = compile_file("ui/Desktop.fui", "Desktop.fbin")
sections = read_fbin(data)
screen = sections["screen"]
print(screen["name"], len(screen["children"]))
```

`compile_source` takes the text instead of a path, and returns the
sections and the warnings rather than writing a file.

### The templates

`sdk/templates` holds the two files `fuic new` writes:

| File | What it is |
|---|---|
| `app.fui` | a screen: the skeleton of a first-party app |
| `app.manifest` | the manifest of a `.finn` package |

`fuic new "Finn Notes"` writes `apps/notes/Notes.fui` and
`apps/notes/notes.manifest`, and says nothing else. The name follows
the house rule in `NAMING.md`: the brand and a plain English noun, never
an invented word.

The templates ship with the source tree, so `fuic new` needs the SDK
installed from a checkout (`pip install -e sdk`). A wheel does not carry
them, and the command says so rather than writing a broken app.

The `.finn` package format is not implemented — `finnpkg` is a stub — so
the manifest is the shape the SDK writes and the package format is a
design that has not been settled.

## The client

`FinnClient` is the control plane as Python. Every verb in
[../docs/control-protocol.md](../docs/control-protocol.md) has a method,
with the same names.

```python
from finnsdk import FinnClient

with FinnClient() as finn:
    state = finn.state()
    print(state.battery_pct, state.volume, state.wifi)

    finn.set_state(volume=40)
    finn.set_appearance(dark=True)

    for app in finn.apps():
        print(app.id, app.state, app.name)

    finn.launch("notes")
```

A field in the state has an absent value rather than a plausible
default: a battery of `-1` means there is no battery, and a wireless of
`-1` means there is no adapter. A machine that cannot do something says
so.

The client falls back to the compositor's own socket when the control
plane is not running, which is enough to move the pointer and take a
frame. `FinnClient(path=...)` says which socket to use, and
`FINN_CONTROL_SOCK` says it to every client at once.

When the device refuses, the client raises `FinnError` with the
message the device gave:

```python
from finnsdk import FinnError

try:
    finn.launch("nonesuch")
except FinnError as exc:
    print("the device said:", exc)
```

### From a shell

The same verbs, with the same names:

```sh
finn-sdk state
finn-sdk state --volume 40
finn-sdk apps
finn-sdk launch notes
finn-sdk pointer --x 300 --y 400 --click
finn-sdk shot /tmp/desktop.ppm
finn-sdk power restart
```

`finn-sdk shot` writes a PPM — the capture is a PPM, and no image
library is needed to take it.

## The layout

```
sdk/
  pyproject.toml        the package: two commands, no dependencies
  bin/fuic              run the compiler from a checkout
  bin/finn-sdk          run the client from a checkout
  templates/            the app skeleton and the package manifest
  finnsdk/
    __init__.py         FinnClient, FinnError, FinnState
    protocol.py         the wire: the sockets, the line protocol
    client.py           the verbs, as Python
    cli.py              `finn-sdk`
    fuic/
      lexer.py          the tokens
      parser.py         the tree
      theme.py          the token table, and the resolution of references
      fbin.py           the binary format, read and written
      compiler.py       parse, resolve, write
      cli.py            `fuic`
      FORMAT.md         the `.fbin` format, specified
```

## What is not here

- **No emulator.** The SDK talks to a running FinnOS; it does not draw
  one.
- **No package manager.** `fuic new` writes the files of an app; the
  `.finn` format and `finnpkg` are not written yet.
- **No AI.** Nothing in the SDK talks to a model, and nothing in the OS
  does either.
