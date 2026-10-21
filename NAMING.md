# FinnOS Naming Scheme

## Rules
1. **One "Finn" root.** Every first-party product carries the `Finn` prefix — no exceptions, no second family brand.
2. **Platform names are one word, CamelCase:** `FinnOS`, `FinnUI`, `FinnAI`, `FinnSDK`. The suffix is a *layer*, not a product.
3. **Apps read as "Finn" + a noun**, written with a space and Title Case: `Finn Code`, `Finn Files`, `Finn Store`. The noun must be a plain English word — never invented, never abbreviated.
4. **System daemons and binaries are lowercase, hyphenated:** `finn-comp`, `finnpkg`, `finn-shell`. A binary name is never shown to a user.
5. **File type is the language, not the brand:** `.fui` (source), `.fbin` (compiled). Never `.finnui` or `.finnos`.
6. **"FinnOS X" marks a first-party subsystem of the OS itself** — part of the base image, not an installable app: `FinnOS Compose`, `FinnOS Update`.
7. **No AI in the name of anything that isn't AI.** Only `FinnAI` may carry it.

## Platform
| Name | Role |
|---|---|
| **FinnOS** | The operating system |
| **FinnUI** | The UI language — tokens, layout primitives, adaptive targets. The `.fui` spec |
| **FinnOS Compose** | The framework that composes FinnUI into a running shell — compiler + state + renderer |
| **FinnOS Update** | Atomic A/B OTA subsystem, driven by `finnpkg` |
| **FinnAI** | Opt-in local inference runtime. Ships off. No models in the base image |
| **FinnSDK** | Public toolkit: `fuic`, the client library, the app templates, the docs |

## System components
| Binary | Role |
|---|---|
| `finn-comp` | The compositor / rendering engine and the shell (direct DRM/KMS, dumb buffers; no X11, no Wayland, no EGL) |
| `finnd` | The control plane: the single permissioned entry point for state, input, display, power and apps |
| `finnctl` | Control-plane client — every `finnd` verb is scriptable from a shell |
| `finn-mcp` | MCP adapter. Exposes the control plane to agents. Runs where the agent runs, not on the device |
| `finnpkg` | Package manager + OTA frontend (`.finn` packages) |
| `fuic` | FinnUI compiler: `.fui` → `.fbin` |

### The shell

The desktop shell — the boot splash (`finnboot`), the first-boot setup
screen (`finnsetup`), the dock (`finndock`), the Control Center
(`finncontrol`), the Notification Center (`finnclock`), the Start Menu
(`finnmenu`) and the window manager (`finnwindow`) — is **not a separate
binary**. It is a set of surfaces inside `finn-comp`, because a surface
that lives in the compositor's own frame costs a blit rather than a round
trip over a socket. `finn-shell` is reserved for the name if that ever
changes; today there is no such process, and there should not be one.

### Compositor modules
Named for what they render, not for how: `finnvg` (vector rasteriser), `finntext`
(Fixel Display text), `finnstate` (the system state the chrome reflects),
`finnboot` (the boot splash), `finnsetup` (the first-boot setup screen),
`finndock` (the dock), `finncontrol` (the Control Center), `finnclock` (the
Notification Center and clock page), `finnmenu` (the Start Menu),
`finnwindow` (the window manager and app windows), `finnpointer` (evdev
pointer + cursor), `finnsock` (unix-socket helpers), `finnproto` (the
control wire protocol), `finnapps` (the application registry), `finndraw`
(the shared drawing primitives), `finnblur` (the blurred wallpaper),
`finnanim` (the animation tracks), `finnnotify` (the notification queue),
`finngpu` (GBM/EGL compositing).

### Control-plane rule
`finnd` is the OS API. MCP is one binding of it, and lives outside the OS.
No external protocol — MCP or otherwise — is ever implemented inside a
system daemon. Control is root-equivalent authority: it is bound to
`/run/finn/control.sock` (mode 0660), never to TCP, authenticated per
connection, and audited. It is switched off by default, on the same intent
as the zero-AI rule.

## First-party apps
| Name | Role |
|---|---|
| **Finn Code** | Editor / IDE |
| **Finn Files** | File manager |
| **Finn Store** | App distribution (`finnpkg` GUI) |
| **Finn Vault** | Credentials and keys |
| **Finn Terminal** | Shell host |
| **Finn Notes** | Notes |
| **Finn Studio** | Visual FinnUI designer, outputs `.fui` |
| **Finn Sync** | Device-to-device sync |

## Reserved — do not use
`Findex` (conflicts with the JDK indexer), `Finka`, `Finnix` (existing Linux distro), `FinnX`, `Finn Shell` as a *product* name (the shell is a compositor subsystem).

## Visual lockup
The boot mark is four dots in a 2×2 grid plus a sparkle — always rendered in the family accent `#7AB8FF` over `#0B0E14`, or inverted on light surfaces. The wordmark is "FinnOS" in **Fixel Display SemiBold**; never letterspaced, never all-caps, never with a space.
