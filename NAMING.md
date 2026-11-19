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
| **FinnOS Update** | OTA A/B update client (`finn-update`), signed manifests, and Settings Update pane |
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
| `finn-darwin` | Clean-room macOS compatibility runtime (`Mach-O`, `libSystem`, `libobjc`, `Foundation`, `CoreGraphics`/`QuartzCore`, core `Swift` ABI, `AppKit-on-Qt6`, `.dmg`/`.pkg`/`.zip`/`.app` installer) |
| `finn-ohos` | Clean-room OpenHarmony 6.1 LTS (API Level 23) runtime & installer (`.hap`/`.hsp`/`.app` ZIP + HTTP install, Panda `modules.abc` VM, `UIAbility`, `ArkUI-on-Qt6`, NAPI host bridge) |
| `finn-sdk` | Developer CLI: scaffolding, manifest validation, signed `.finnapp` packaging, simulator, build, run, register, check |
| `finn-update` | OTA A/B update CLI (`status`, `check`, `download`, `apply`, `commit`, `rollback`, `health-check`, `config`) |
| `libfinnui` | The Qt6 app runtime shared library |

## Modules
| Module | Role |
|---|---|
| `src/comp` | `finn-comp`: server, output, seat, pointer/touch/tablet, toplevels, layers, `ext-session-lock-v1`, Xwayland, screencopy |
| `src/ui` | `libfinnui` and public `<FinnSDK/*>` headers: tokens, theme, spring physics (`spring.hpp`), custom CSD window (`FinnFormFactor`), widgets, LayerShell wrapper |
| `src/qtplugin` | Custom Qt6 platform theme & widget style plugins (`QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`) |
| `src/session` | `finn-session`, `SessionClient`, and unified `AppRegistry` |
| `src/compat` | Linux `.desktop` / Flatpak parser & installer, Windows PE / Wine runner, macOS (`macos_runner`), OpenHarmony 6.1 LTS (`ohos_runner`), and Waydroid Android (`android_runner`) probes |
| `src/darwin` | `finn-darwin`: clean-room 64-bit Mach-O loader, `libSystem`, Objective-C 2.0 + `objc_msgSend` asm, Foundation, CoreGraphics/QuartzCore, core Swift ABI, AppKit-on-Qt6 bridge, HTTP + `.dmg`/`.pkg`/`.zip`/`.icns` installer |
| `src/ohos` | `finn-ohos`: clean-room OpenHarmony 6.1 LTS (API 23) native ZIP `.hap`/`.hsp`/`.app` + HTTP installer, Panda `modules.abc` VM, `UIAbility` lifecycle, ArkUI-on-Qt6 bridge, and NAPI host bridge |
| `src/system` | Honest `/proc`, `/sys`, settings readers & async D-Bus/NetworkManager/BlueZ/PipeWire/logind controllers |
| `src/shell` | `finn-shell` responsive desktop/tablet/phone overlay (`dock`, `popovers`, `notifications`, `LockScreenWindow`) |
| `src/ota` | `finn-update` and `finn_ota`: Ed25519 manifest verification, HTTP Range download, `zstd` delta patching, `systemd-boot`/`GRUB` slot switching |
| `src/common` | Paths shared by two modules, and only that |
| `repo` | First-party `[finnos]` pacman repository scripts (`gen-signing-key.sh`, `publish.sh`, `finnos-mirrorlist`) |
| `apps/<id>` | First-party native Qt6 apps (`wallpaper`, `settings`, `files`, `terminal`, `notes`, `store`, `viewer`, `monitor`, `installer`, `oobe`, optional `flourish`) |
| `sdk` | `finn-app.schema.json` manifest schema, `sdk/template/` starter project, and host test suite |

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

The boot splash shows a small **white** version of that mark on the dark field
with a fully rounded, white progress bar beneath it — no spinner, status text
or blinking cursor. The bar advances only on real boot-progress reports. It is drawn from inside the
initramfs so it is the first thing FinnOS puts on the display; the firmware's
own logo before the kernel starts is not ours to draw. The ribbon "hi."
animation belongs to first-time setup (`finn-oobe`), never to a normal boot.
