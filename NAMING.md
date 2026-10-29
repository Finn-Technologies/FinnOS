# FinnOS Naming Scheme

## Rules
1. **One "Finn" root.** Every first-party product carries the `Finn` prefix — no exceptions, no second family brand.
2. **Platform names are one word, CamelCase:** `FinnOS`, `FinnUI`, `FinnAI`, `FinnSDK`. The suffix is a *layer*, not a product.
3. **Apps read as "Finn" + a noun**, written with a space and Title Case: `Finn Code`, `Finn Files`, `Finn Store`. The noun must be a plain English word — never invented, never abbreviated.
4. **System daemons and binaries are lowercase, hyphenated:** `finn-comp`, `finnpkg`, `finnctl`. A binary name is never shown to a user.
5. **File type is the language, not the brand:** `.fui` (source), `.fbin` (compiled). Never `.finnui` or `.finnos`.
6. **"FinnOS X" marks a first-party subsystem of the OS itself** — part of the base image, not an installable app: `FinnOS Compose`, `FinnOS Update`.
7. **No AI in the name of anything that isn't AI.** Only `FinnAI` may carry it.

## Platform
| Name | Role |
|---|---|
| **FinnOS** | The operating system |
| **FinnUI** | The UI language — tokens, layout primitives, adaptive targets. The `.fui` spec |
| **FinnOS Compose** | The compositor and the control plane as one subsystem (`finnos-compose`): `finn-comp`, `finnd`, `finnctl` and the shell's data. The compiled-screen reader exists; the `.fbin` renderer does not yet |
| **FinnOS Update** | The runtime package and its OTA frontend (`finnpkg update`). There are no A/B slots yet |
| **FinnAI** | Reserved: an opt-in local inference runtime. It is not implemented, and no models are in the base image |
| **FinnSDK** | Public toolkit: `fuic`, the client library, the app templates, the docs |

## System components
| Binary | Role |
|---|---|
| `finn-comp` | The compositor / rendering engine and the shell (direct DRM/KMS; no X11; limited in-process Wayland xdg-shell/SHM client support; software buffers or GBM/EGL acceleration) |
| `finnd` | The control plane: the single permissioned entry point for state, input, display, power and apps |
| `finnctl` | Control-plane client — every `finnd` verb is scriptable from a shell |
| `finn-mcp` | MCP adapter. Exposes the control plane to agents. Runs where the agent runs, not on the device |
| `finnpkg` | Package manager + OTA frontend (`.finn` packages) |
| `fuic` | FinnUI compiler: `.fui` → `.fbin` |

### The shell

The desktop shell — the boot splash (`finnboot`), the first-boot setup
screen (`finnsetup`), the dock (`finndock`), the Control Center
(`finncontrol`), the Notification Center (`finnclock`), the Start Menu
(`finnmenu`), the window manager (`finnwindow`) and the browser chrome
(`finnbrowserapp`) — is **not a separate
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
`finnwindow` (the window manager and app windows), `finnbrowserapp` (the browser
window chrome and web view), `finnpointer` (evdev
pointer + cursor), `finnkbd` (evdev keyboard), `finnsock` (unix-socket
helpers), `finnconn` (per-connection line framing), `finnproto` (the
control wire protocol), `finnsettings` (the versioned settings store),
`finnapps` (the application registry), `finndraw`
(the shared drawing primitives), `finnblur` (the blurred backdrop),
`finnanim` (the animation tracks), `finnnotify` (the notification queue),
`finngpu` (GBM/EGL compositing).

### Control-plane rule
`finnd` is the OS API. MCP is one binding of it, and lives outside the OS.
No external protocol — MCP or otherwise — is ever implemented inside a
system daemon. Control is root-equivalent authority: it is bound to
`/run/finn/control.sock` (mode 0660), never to TCP, authenticated per
connection, and audited. Remote access is not a default, on the same intent as
the zero-AI rule.

## First-party apps
| Name | Role |
|---|---|
| **Finn Files** | File manager — exists |
| **Finn Store** | App distribution (`finnpkg` GUI) — exists |
| **Finn Browser** | Web browser (`browser`) — exists |
| **Finn Code** | Editor / IDE — reserved, not written |
| **Finn Vault** | Credentials and keys — reserved, not written |
| **Finn Terminal** | Shell host — reserved, not written |
| **Finn Notes** | Notes — reserved, not written |
| **Finn Studio** | Visual FinnUI designer, outputs `.fui` — reserved, not written |
| **Finn Sync** | Device-to-device sync — reserved, not written |

The system settings surface is called **Settings**, not *Finn Settings*: it is
part of the shell rather than an app in the family. Only the four shipped
entries in `assets/apps.registry` — Settings, Finn Files, Finn Store and
Finn Browser — are
real; the other names are reserved so that two people
do not invent two.

## Reserved — do not use
`Findex` (conflicts with the JDK indexer), `Finka`, `Finnix` (existing Linux distro), `FinnX`, `Finn Shell` as a *product* name (the shell is a compositor subsystem).

## Visual lockup
The boot mark is four dots in a 2×2 grid plus a sparkle — always rendered in the family accent `#7AB8FF` over `#0B0E14`, or inverted on light surfaces. The wordmark is "FinnOS" in **Fixel Display SemiBold**; never letterspaced, never all-caps, never with a space.
