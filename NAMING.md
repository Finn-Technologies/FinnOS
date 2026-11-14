# FinnOS Naming Scheme

## Rules
1. **One "Finn" root.** Every first-party product carries the `Finn` prefix.
2. **Platform names are one word, CamelCase:** `FinnOS`, `FinnUI`, `FinnAI`,
   `FinnSDK`. The suffix is a layer, not a product.
3. **Apps read as "Finn" + a plain English noun:** `Finn Files`, `Finn Terminal`,
   `Finn Notes`, `Finn Store`, `Finn Viewer`, `Finn Monitor`. Only **Settings**
   stands alone.
4. **Daemons and binaries are lowercase and hyphenated:** `finn-comp`,
   `finn-session`, `finn-shell`, `finn-open`, `finn-sdk`. A binary name is
   never shown to a user.
5. **The library is `libfinnui`;** the custom Qt6 UI framework and design language
   are **FinnUI** / **FinnSDK**.
6. **A system surface is named for what it renders:** `finn-shell` renders the
   shell; `finn-comp` composites.
7. **No AI in the name of anything that is not AI.** Only `FinnAI` may carry
   it, and FinnAI is not implemented.

## Platform
| Name | Role |
|---|---|
| **FinnOS** | The operating system |
| **FinnUX** | The FinnOS desktop environment & shell experience (`finn-shell`, windowing, lock screen) |
| **FinnUI** | The Qt6-based FinnOS UI language and shared runtime (`libfinnui`) |
| **FinnQt Plugins** | Custom `QPlatformTheme` (`libfinnplatformtheme.so`) and `QProxyStyle` (`libfinnstyle.so`) |
| **FinnSDK** | Developer C++20/Qt6 headers (`<FinnSDK/FinnSDK.hpp>`), CMake package, project template, and `finn-sdk` CLI |
| **FinnOS Compose** | The session package: `finn-comp`, `finn-session`, `finn-shell`, `libfinnui`, the Qt6 plugins, the apps, and their data |
| **FinnOS Update** | Reserved: OTA frontend. Not written |
| **FinnAI** | Reserved and off by default. Not implemented |

## Programs
| Binary | Role |
|---|---|
| `finn-comp` | The wlroots compositor: DRM or headless, xdg-shell, layer-shell, foreign-toplevel, Xwayland, screencopy |
| `finn-session` | The user broker: catalogue, installed list, `.desktop`/Flatpak/Wine launch, compat status |
| `finn-shell` | The Qt6/LayerShellQt dock, running-window switcher, home menu, Spotlight search, Control Center, notifications, and lock screen |
| `finn-wallpaper` | The background layer and its WebP wallpaper (`background` layer) |
| `finn-files` | Finn Files |
| `finn-settings` | Settings |
| `finn-terminal` | Finn Terminal (native PTY VT100/ANSI terminal) |
| `finn-notes` | Finn Notes (multi-tab Markdown/text editor) |
| `finn-store` | Finn Store (installed apps, Flatpak/pacman browser, Wine PE inspector, FinnSDK hub) |
| `finn-viewer` | Finn Viewer (raster and SVG image viewer) |
| `finn-monitor` | Finn Monitor (CPU sparkline, memory, live process manager) |
| `finn-installer` | Bare-Metal GUI Installer (disk partitioning, bootloader, base system deployment) |
| `finn-oobe` | First-Time Setup (`hi`, `How do we talk?`, Wi-Fi, account, appearance, and 6-ring ribbon completion) |
| `finn-open` | Unified CLI target/file/URL opener (`finn-session` client) |
| `finn-sdk` | Developer scaffolding, build, run, register, and Doctor CLI |
| `libfinnui` | The Qt6 app runtime shared library |

## Modules
| Module | Role |
|---|---|
| `src/comp` | `finn-comp`: server, output, seat, toplevels, layers, Xwayland, screencopy |
| `src/ui` | `libfinnui` and public `<FinnSDK/*>` headers: tokens, theme, custom CSD window, Figma components, LayerShell wrapper |
| `src/qtplugin` | Custom Qt6 platform theme & widget style plugins (`QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`) |
| `src/session` | `finn-session`, `SessionClient`, and unified `AppRegistry` |
| `src/compat` | Linux `.desktop` / Flatpak parser & Windows PE / Wine runner |
| `src/system` | Honest `/proc`, `/sys`, settings readers & async D-Bus/NetworkManager/BlueZ/PipeWire/logind controllers |
| `src/shell` | `finn-shell` desktop overlay |
| `src/common` | Paths shared by two modules, and only that |
| `apps/<id>` | First-party native Qt6 apps (`wallpaper`, `files`, `settings`, `terminal`, `notes`, `store`, `viewer`, `monitor`) |
| `sdk/template` | Starter native Qt6/C++20 project for `finn-sdk new` |

## First-party apps
| Name | Binary | Role |
|---|---|---|
| **Settings** | `finn-settings` | System settings, appearance, hardware & compatibility status |
| **Finn Files** | `finn-files` | Native file manager with inline PE inspector & app integration |
| **Finn Terminal** | `finn-terminal` | Native PTY terminal emulator with scrollback & ANSI/256-color grid |
| **Finn Notes** | `finn-notes` | Tabbed Markdown & plain-text editor |
| **Finn Store** | `finn-store` | Unified installed, Linux (`Flatpak`/`pacman`), Windows (`.exe`), and FinnSDK software hub |
| **Finn Viewer** | `finn-viewer` | Raster + crisp zoomed SVG viewer |
| **Finn Monitor** | `finn-monitor` | System CPU/RAM telemetry & process control |

All seven entries live in `assets/apps.registry`. Reserved future names: **Finn Browser**, **Finn Code**, **Finn Vault**, **Finn Studio**, **Finn Sync**.

## Reserved — do not use
`Findex` (conflicts with the JDK indexer), `Finka`, `Finnix` (an existing Linux distribution), `FinnX`, `Finn Shell` as a product name (the shell is `finn-shell`).

## Visual lockup
The boot mark is four dots in a 2×2 grid plus a sparkle, in the family accent `#7AB8FF` over `#0B0E14`, or inverted on light surfaces. The wordmark is "FinnOS" in **Fixel Display SemiBold**; never letterspaced, never all-caps, never with a space.
