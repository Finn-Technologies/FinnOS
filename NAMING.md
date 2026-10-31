# FinnOS Naming Scheme

## Rules
1. **One "Finn" root.** Every first-party product carries the `Finn` prefix.
2. **Platform names are one word, CamelCase:** `FinnOS`, `FinnUI`, `FinnAI`,
   `FinnSDK`. The suffix is a layer, not a product.
3. **Apps read as "Finn" + a plain English noun:** `Finn Files`. Never an
   invented word, never an abbreviation.
4. **Daemons and binaries are lowercase and hyphenated:** `finn-comp`,
   `finn-session`, `finn-shell`. A binary name is never shown to a user.
5. **The library is `libfinnui`;** the UI language is **FinnUI**.
6. **A system surface is named for what it renders:** `finn-shell` renders the
   shell; `finn-comp` composites.
7. **No AI in the name of anything that is not AI.** Only `FinnAI` may carry
   it, and FinnAI is not implemented.

## Platform
| Name | Role |
|---|---|
| **FinnOS** | The operating system |
| **FinnUI** | The UI language and its runtime, `libfinnui` |
| **FinnOS Compose** | The session package: `finn-comp`, `finn-session`, `finn-shell`, `libfinnui`, the apps and their data |
| **FinnOS Update** | Reserved: the runtime package and its OTA frontend. Not written |
| **FinnAI** | Reserved and off by default. Not implemented |
| **FinnSDK** | The app template and the runtime headers |

## Programs
| Binary | Role |
|---|---|
| `finn-comp` | The wlroots compositor: DRM or headless, xdg-shell, layer-shell, window management |
| `finn-session` | The user broker: catalogue, installed list, launch |
| `finn-shell` | The dock, status and app menu as a layer-shell client |
| `finn-wallpaper` | The background layer and its WebP wallpaper |
| `finn-files` | Finn Files |
| `finn-settings` | Settings |
| `libfinnui` | The app runtime library (not a binary) |

`finnd`, `finnctl` and `finnpkg` are reserved for the roadmap in
[`docs/roadmap.md`](docs/roadmap.md).

## Modules
| Module | Role |
|---|---|
| `src/comp` | `finn-comp`: server, output, seat, toplevels, layers, screencopy |
| `src/ui` | `libfinnui`: canvas, text, vector, widgets, Wayland client, theme |
| `src/session` | `finn-session` and its client; the app registry reader |
| `src/shell` | `finn-shell` |
| `src/common` | paths shared by two modules, and only that |
| `apps/<id>` | a first-party app, one directory per app |
| `sdk/template` | the app a new contributor starts from |

## First-party apps
| Name | Role |
|---|---|
| **Finn Files** | File manager — exists |
| **Settings** | Settings — exists, part of the family but not "Finn Settings" |
| **Finn Store** | Reserved, not written |
| **Finn Code**, **Finn Vault**, **Finn Terminal**, **Finn Notes**, **Finn Studio**, **Finn Sync** | Reserved, not written |

The two shipped entries in `assets/apps.registry` are **Settings** and
**Finn Files**. The other names are reserved so two people do not invent two.

## Reserved — do not use
`Findex` (conflicts with the JDK indexer), `Finka`, `Finnix` (an existing
Linux distribution), `FinnX`, `Finn Shell` as a product name (the shell is
`finn-shell`).

## Visual lockup
The boot mark is four dots in a 2×2 grid plus a sparkle, in the family accent
`#7AB8FF` over `#0B0E14`, or inverted on light surfaces. The wordmark is
"FinnOS" in **Fixel Display SemiBold**; never letterspaced, never all-caps,
never with a space.
