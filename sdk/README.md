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
| `manifest.toml` | the manifest of a `.finn` package |

`fuic new "Finn Notes"` writes `apps/notes/Notes.fui` and
`apps/notes/manifest.toml`, and says nothing else. The name follows
the house rule in `NAMING.md`: the brand and a plain English noun, never
an invented word.

The templates ship with the source tree, so `fuic new` needs the SDK
installed from a checkout (`pip install -e sdk`). A wheel does not carry
them, and the command says so rather than writing a broken app.

The `.finn` package format is real: `finnpkg install <file.finn>` accepts a
tarball with a `manifest.toml`, a `SHA256SUMS` covering every file, and the
app's files. There is no signature check and no dependency graph; the manifest
is what the SDK writes and what the package manager reads. See
[../src/pkg/README.md](../src/pkg/README.md#the-finn-package).

## The client

`FinnClient` is the control plane as Python. It wraps the common verbs —
state, input, screen, windows, setup and settings — with methods of the same
names. The packaging, Flatpak, Wine, winget and update families are not
wrapped; use `finnctl` or `finnpkg` for those.

```python
from finnsdk import FinnClient

with FinnClient() as finn:
    state = finn.state()
    print(state.battery_pct, state.volume, state.wifi)

    finn.set_state(volume=40)
    finn.set_appearance(dark=True)

    for app in finn.apps():
        print(app.id, app.state, app.name)

    finn.launch("files")
```

A field in the state has an absent value rather than a plausible
default: a battery of `-1` means there is no battery, and a wireless of
`-1` means there is no adapter. A machine that cannot do something says
so.

`FinnClient(path=...)` says which socket to use, and `FINN_CONTROL_SOCK`
says it to every client at once. `FinnClient(direct=True)` talks to the
compositor's own socket (`/run/finn/comp.sock`) instead; that is the explicit
development path, it needs root, and it bypasses `finnd` and its audit log.
There is **no automatic fallback**: a device whose control plane is down is
unreachable rather than quietly unaudited.

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

A small set of them, with shell-friendly names:

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
    client.py           the common verbs, as Python
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
- **No package build command.** `fuic new` writes the files of an app, and
  `finnpkg install <file.finn>` installs one, but the SDK does not build the
  `.finn` tarball: that is `tar` plus `SHA256SUMS` today, and a first-class
  `finnpkg pack` is not written yet.
- **No AI.** Nothing in the SDK talks to a model, and nothing in the OS
  does either.
