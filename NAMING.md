# FinnOS Naming Scheme

## Rules
1. **One "Finn" root.** Every first-party product carries the `Finn` prefix.
2. **Platform names are one word, CamelCase:** `FinnOS`, `FinnUX`, `FinnUI`, `FinnSDK`, `FinnAI`. The suffix is a layer, not a product.
3. **Apps read as "Finn" + a plain English noun:** `Finn Files`, `Finn Terminal`, `Finn Notes`, `Finn Store`, `Finn Viewer`, `Finn Monitor`, `Finn Calculator`, `Finn Archive`, `Finn Capture`. Only **Settings** and **Flourish** stand alone.
4. **Daemons and binaries are lowercase and hyphenated:** `finn-comp`, `finn-session`, `finn-shell`, `finn-boot`, `finn-open`, `finn-sdk`, `finn-update`. A binary name is never shown to a user.
5. **The libraries are `libfinnui` and `libfinnui_qml`;** the custom Qt 6 UI framework and design language are **FinnUI** / **FinnSDK**.
6. **A system surface is named for what it renders:** `finn-shell` renders the shell; `finn-comp` composites.
7. **No AI in the name of anything that is not AI.** Only `FinnAI` may carry it, and FinnAI is not implemented.

## Platform
| Name | Role |
|---|---|
| **FinnOS** | The operating system |
| **FinnUX** | The FinnOS desktop environment & shell experience (`finn-shell`, windowing, lock screen) |
| **FinnUI** | The Qt 6 C++20 + QML UI language and shared runtimes (`libfinnui`, `libfinnui_qml`) |
| **FinnQt Plugins** | Custom `QPlatformTheme` (`libfinnplatformtheme.so`) and `QProxyStyle` (`libfinnstyle.so`) |
| **FinnSDK** | Developer C++20/Qt 6/QML headers (`<FinnSDK/FinnSDK.hpp>`), `FinnUI 1.0` QML module, CMake/pkg-config package, project template, and `finn-sdk` CLI |
| **FinnOS Compose** | The session package: `finn-comp`, `finn-session`, `finn-shell`, `libfinnui`, `libfinnui_qml`, the Qt 6 plugins, the apps, and their data |
| **FinnOS Update** | OTA A/B update client (`finn-update`), signed manifests, and Settings Update pane |
| **FinnAI** | Reserved and off by default. Not implemented |

## Programs
| Binary | Role |
|---|---|
| `finn-bootx64.efi` / `finn-bootaa64.efi` | Freestanding UEFI GOP graphical Startup Manager (`src/boot/`, BLS Type #1/#2, boot-counting, multi-OS detection) |
| `finn-comp` | The Qt 6 `QWaylandQuickCompositor`: `xdg-shell`, `xdg-decoration`, `text-input-v3`, `wp_viewporter`, 8-layer scene stack, 4 virtual desktops, CPU backdrop blur + Liquid Glass SDF |
| `finn-session` | The user broker: unified catalogue across 7 sources, `.desktop`/Flatpak/AppImage/Wine/macOS/OHOS/Android launch & uninstall, compat probe |
| `finn-shell` | The Qt Quick System UI: 4-pill dock, Home Menu, Spotlight search, Control Center, notifications, workspace overview, lock screen, and first-boot OOBE launch |
| `finn-wallpaper` | Standalone background WebP wallpaper surface (`background` layer; `finn-comp` renders the wallpaper directly on Wayland unless `FINN_WALLPAPER_STANDALONE=1`) |
| `finn-files` | Finn Files |
| `finn-settings` | Settings |
| `finn-terminal` | Finn Terminal (native POSIX PTY VT100/ANSI terminal) |
| `finn-notes` | Finn Notes (Markdown/plain-text editor) |
| `finn-store` | Finn Store (Curated, Flathub/Flatpak, Aurora/Play/F-Droid Android APKs, `winget-pkgs` Windows, local package sideloading) |
| `finn-viewer` | Finn Viewer (raster and SVG image viewer) |
| `finn-monitor` | Finn Monitor (CPU sparkline, memory, live process manager) |
| `finn-calculator` | Finn Calculator (standard & scientific calculator with history tape) |
| `finn-archive` | Finn Archive (browse, extract, and create `.zip` / `.tar*` archives) |
| `finn-capture` | Finn Capture (full-screen screenshot capture via compositor IPC) |
| `finn-flourish` | Flourish (optional multi-tab Chromium web browser via `Qt6::WebEngineQuick`) |
| `recovery-mode` | FinnOS Recovery Mode (100% offline squashfs installer, GPT/MBR & UEFI/BIOS dual-boot, Disk Utility, Bootloader Repair, System Logs, World Keyboards, Network, embedded PTY terminal) |
| `finn-oobe` | First-Time Setup (10-step onboarding: `hi.`, language, region, keyboard, Wi-Fi, account, appearance + 3D lamp, accessibility, privacy, complete) |
| `finn-open` | Unified CLI target/file/URL opener (`finn-session` client) |
| `finn-cursors` | SVG-to-XCursor theme generator (`finn-light`, `finn-dark`) |
| `finn-display` | Display resolution & scale helper |
| `finn-win` | Windows compatibility CLI (`Wine`/`Proton`/`Hangover`, MSI/EXE install/uninstall, PE inspection) |
| `finn-darwin` | Clean-room macOS compatibility runtime (`Mach-O`, `libSystem`, `libobjc`, `Foundation`, `CoreGraphics`, subset `Swift`/`SwiftUI`, `AppKit-on-Qt6`, `.dmg`/`.pkg`/`.zip`/`.app` installer) |
| `finn-ohos` | Clean-room OpenHarmony 6.1 LTS (API Level 23) runtime & installer (`.hap`/`.hsp`/`.app` ZIP + HTTP install, subset Panda `modules.abc` VM, `UIAbility`, `ArkUI-on-Qt6`, NAPI host bridge) |
| `finn-android` | Clean-room Android 17 (API 37) inspector & layout previewer (native APK/split-APK, binary AXML, `resources.arsc`, DEX verification/disassembly, 14-widget Qt 6 previewer, 16-opcode integer micro-interpreter) — no Waydroid |
| `finn-sdk` | Developer CLI: scaffolding, manifest validation, signed `.finnapp` packaging, simulator, build, run, register, check |
| `finn-update` | OTA A/B update CLI (`status`, `check`, `download`, `apply`, `commit`, `rollback`, `health-check`, `config`, `simulate-boot`) |
| `finn-guest-tools` | Virtualization guest integration CLI (`status`, `install`, `enable`, `mount-shared`, `uninstall`) for VMware Tools, VirtualBox Guest Additions, and QEMU Guest Agent |
| `finn-emulator` | Host emulator/simulator probe and launcher (Lima, QEMU, headless compositor, Qt offscreen) |
| `finn-oobe-apply` | Audited one-shot root provisioning helper invoked by `finn-oobe` |
| `libfinnui` / `libfinnui_qml` | The Qt 6 C++20 widget and QML `FinnUI 1.0` app runtime shared libraries |

## Modules
| Module | Role |
|---|---|
| `src/boot` | `finn-bootx64.efi` / `finn-bootaa64.efi`: freestanding UEFI GOP bootloader, BLS/UKI parser, boot UI |
| `src/comp` | `finn-comp`: Qt 6 `QWaylandQuickCompositor`, `WorkspaceModel`, `InputRouter`, 8-layer `SceneLayer` stack, CPU backdrop blur + Liquid Glass SDF |
| `src/ui` | `libfinnui`, `libfinnui_qml`, and public `<FinnSDK/*>` headers: tokens, theme, spring physics (`spring.hpp`), `AdaptiveFormFactor`, 29 C++ widgets, 19 `FinnUI 1.0` QML controls |
| `src/qtplugin` | Custom Qt 6 platform theme & widget style plugins (`QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`) |
| `src/session` | `finn-session`, `SessionClient`, and unified `AppRegistry` |
| `src/compat` | Linux `.desktop`/Flatpak/AppImage/sandbox/ELF probes, `.finnapp` installer, Windows PE/ICO/MSI/Wine/Hangover (`wl_clamp.c`), and macOS/OHOS/Android runners |
| `src/darwin` | `finn-darwin`: clean-room 64-bit Mach-O loader, `libSystem`, Objective-C 2.0 + `objc_msgSend` asm, Foundation, CoreGraphics, subset Swift/SwiftUI, AppKit-on-Qt6, installer |
| `src/ohos` | `finn-ohos`: clean-room OpenHarmony 6.1 LTS (API 23) `.hap`/`.hsp`/`.app` installer, subset Panda `modules.abc` VM, `UIAbility`, ArkUI-on-Qt6, NAPI host bridge |
| `src/android` | `finn-android`: clean-room Android 17 APK/split-APK + AXML + `resources.arsc` inspector, DEX verifier/disassembler, 16-opcode micro-interpreter, per-user installer, Qt 6 AXML layout previewer |
| `src/recovery` | Pure C++20 zero-Qt recovery & dual-boot planner (`recovery_engine`) with FIPS 180-4 SHA-512 verification |
| `src/system` | Honest `/proc`, `/sys`, settings readers (`sys_files`), async D-Bus/NetworkManager/BlueZ/PipeWire/logind controllers (`desktop_services`), and hypervisor/guest-tools probe (`guest_tools`) |
| `src/shell` | `finn-shell` Qt Quick System UI (`ShellCorona`, `DesktopShell.qml`, `LockScreenOverlay.qml`, `full_oobe_flow`, `search`, `blur_kernel`) |
| `src/ota` | `finn-update` and `finn_ota`: Ed25519 manifest verification, HTTP Range download, `zstd` delta patching, `systemd-boot`/`GRUB` slot switching |
| `src/common` | `paths.hpp` (paths shared by two modules) and `liquid_glass.hpp` (header-only CPU SDF refraction renderer) |
| `repo` | First-party `[finnos]` pacman repository scripts (`gen-signing-key.sh`, `publish.sh`, `finnos-mirrorlist`) |
| `apps/<id>` | First-party native Qt 6 apps (11 registry apps + `wallpaper`, `oobe`, `recovery-mode`) |
| `sdk` | `finn-app.schema.json` manifest schema, `sdk/template/` starter project, and host test suite |

## First-party apps
| Name | Binary | Role |
|---|---|---|
| **Settings** | `finn-settings` | System settings, appearance, hardware, accounts, guest tools, compatibility & OTA updates |
| **Finn Files** | `finn-files` | Native file manager with `.finnapp` inspection, folder emblems & app integration |
| **Finn Terminal** | `finn-terminal` | Native POSIX PTY terminal emulator with scrollback & VT100/ANSI grid |
| **Finn Notes** | `finn-notes` | Markdown & plain-text editor |
| **Finn Store** | `finn-store` | Unified Curated, Flathub/Flatpak, Android (Aurora/Play/F-Droid), Windows (`winget`), and local package sideloading hub |
| **Finn Viewer** | `finn-viewer` | Raster & SVG image viewer with folder gallery |
| **Finn Monitor** | `finn-monitor` | System CPU sparkline, RAM telemetry & process control |
| **Finn Calculator** | `finn-calculator` | Standard & scientific calculator with history tape |
| **Finn Archive** | `finn-archive` | `.zip` and `.tar*` archive viewer, extractor & creator |
| **Finn Capture** | `finn-capture` | Full-screen screenshot capture & clipboard utility |
| **Flourish** | `finn-flourish` | Optional multi-tab Chromium web browser (`Qt6::WebEngineQuick`) |

All eleven entries live in `assets/apps.registry`. Reserved future names: **Finn Code**, **Finn Vault**, **Finn Studio**, **Finn Sync**.

## Reserved — do not use
`Findex` (conflicts with the JDK indexer), `Finka`, `Finnix` (an existing Linux distribution), `FinnX`, `Finn Shell` as a product name (the shell is `finn-shell`).

## Visual lockup
The boot mark is four dots in a 2×2 grid plus a sparkle, in the family accent `#7AB8FF` over `#0B0E14`, or inverted on light surfaces. The wordmark is "FinnOS" in **Fixel Display SemiBold**; never letterspaced, never all-caps, never with a space.

The boot splash shows a 72 × 72 px **white** version of that mark (`finn::kBootMarkSizePx`) on the dark field with a fully rounded, white progress bar beneath it — no spinner, status text or blinking cursor. The bar advances only on real boot-progress reports. It is drawn from inside the initramfs so it is the first thing FinnOS puts on the display; the firmware's own logo before the kernel starts is not ours to draw. The ribbon "hi." animation belongs to first-time setup (`finn-oobe`), never to a normal boot.
