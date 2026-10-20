# The roadmap

Where FinnOS is, and what comes next. Written so that the next person —
human or agent — knows what "done" means for each piece.

## Now

| | |
|---|---|
| the compositor | DRM/KMS, page flips, a GPU path and a software path |
| the shell | the dock, the Control Center, the clock page, the Start Menu |
| the control plane | `finnd`, `finnctl`, the audit log, the protocol |
| the registry | the catalogue the Start Menu draws, the install list the package manager owns |
| the SDK | `fuic` (the `.fui` compiler), the Python client, the app templates |
| the adapter | `finn-mcp`, the agent-facing half of the control plane |

## Next

### Windows

The largest missing piece, and everything else is smaller without it.

A window manager needs three things the system does not have yet:

1. **A compositor client protocol.** A client connects, says "I want a
   surface, this size, at this position", and the compositor gives it
   back. The protocol should be the same line protocol the control plane
   uses, with a second socket for surfaces, and it should be binary —
   a surface stream is pixels, not text.
2. **A window list.** The shell needs to know what is open, so the dock's
   first bubble — the reserved one — can show running apps, and so
   `Alt-Tab` has something to switch between.
3. **A focus and stacking model.** Click to raise, a keyboard focus that
   follows the Super keys, and a rule about which surface takes the
   pointer.

The rules for the shell chrome that are already in force apply to windows
too: nothing moves under the pointer, and a surface is a cached layer.

### Keyboard

The compositor reads a pointer and nothing else. It needs an evdev
keyboard, a key map, and a way to deliver keys to whatever has focus.
Until then there is no Super+Space, and the Start Menu is opened by
clicking the dock's launcher button.

### Apps that draw

`app.launch` starts a program, and the program has no way to talk to the
compositor. When the client protocol exists, a `.fui` app becomes a
client: the `.fbin` is its scene graph, and it asks for a surface.

The first-party apps are named and waiting:

| App | What it is |
|---|---|
| **Finn Code** | editor and IDE |
| **Finn Files** | file manager |
| **Finn Store** | app distribution, the `finnpkg` GUI |
| **Finn Notes** | notes |
| **Finn Studio** | the visual `.fui` designer |
| **Finn Sync** | device-to-device sync |
| **Finn Vault** | credentials and keys |
| **Finn Terminal** | the shell host |

### Packaging and OTA

`finnpkg` is a stub. The design it is aiming at:

- a `.finn` package is a tarball with a manifest, a `.fbin` and assets;
- the manifest names the app, the id it installs as, and the files it
  owns;
- installation records the id in `/var/lib/finnpkg/installed`, which is
  the same list the Start Menu reads;
- an update is an **A/B slot** switch: the new system is written to the
  inactive slot, the bootloader is told which one to boot, and the switch
  is confirmed by the new system coming up. A failed boot falls back.

The A/B switch is the only safe way to update a machine that is often
running on battery, in a bag, with nobody watching.

### FinnAI

Opt-in, local, and never in the base image. When it arrives it will be a
service the user turns on, with weights fetched on demand through
`finnpkg`, and it will talk to the system only through the control plane —
the same door every other client uses.

## Later

- **The other targets.** Mobile, MobileXL and TV are specified in
  `ui/_targets.fui` and there is no code behind them. The compositor's
  density model is already in place; the input model is not.
- **The UI language, finished.** `fuic` compiles a `.fui` file to a
  `.fbin`. The renderer that draws a `.fbin` does not exist, and the
  token table is still kept in two places by hand.
- **A second shell.** The compositor draws the shell in C. A shell that
  loads its own layout would let the OS be themed without touching the
  compositor — but it would also mean a shell crash takes the display
  with it, so it is a trade, not a plan.
