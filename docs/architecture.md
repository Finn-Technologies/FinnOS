# Architecture

The processes and shared libraries that make up FinnOS.

| Component | Owns | Runs as |
|---|---|---|
| `finn-comp` | Qt 6 Wayland Compositor (KWin-inspired): DRM/KMS & offscreen/headless output, 8-layer scene stack, `WorkspaceModel`, `InputRouter`, GPU Dual-Kawase blur shader, lock-request/idle timers (`XKB_KEY_l`), zero app IDs | root |
| `libfinnui` & `libfinnui_qml` (`FinnSDK`) | Qt 6 C++20 + QML UI framework (Kirigami/Breeze-inspired): live tokens, `FinnThemeBridge`, 120 Hz analytic spring physics (`spring.hpp`), `FinnFormFactor` (Desktop/Tablet/Mobile/TV), `FinnSquircle` (`cornerSmoothing=0.6`), HugeIcons `image://finn-icon/`, 19 AOT-compiled `FinnUI 1.0` QML controls including adaptive `FinnApplicationWindow` | shared libraries |
| `libfinnplatformtheme` & `libfinnstyle` | Custom Qt6 `QPlatformTheme` and `QProxyStyle` injected via `QT_QPA_PLATFORMTHEME=finn` & `QT_STYLE_OVERRIDE=finn` | Qt6 plugins |
| `finn-session` | Unified app catalogue, `.desktop`/Flatpak/Wine discovery, launch broker, runtime compat probe | `finn` |
| `finn-shell` | Unified adaptive System UI (Plasma Corona-inspired): single binary supporting Desktop (Figma 4-pill dock, Home Menu, Spotlight, Control Center, 3-card Notification stack), Tablet (hybrid status bar + touch dock), Mobile (Plasma Mobile homescreen, Action Drawer, card Task Switcher, 134×4dp gesture pill), and TV (Plasma Bigscreen 10-foot shelves, 1.5× typography, D-pad spatial focus, Quick Settings drawer), plus Lock Screen | `finn` |
| `finn-wallpaper` | Background WebP wallpaper surface (`background` layer) | `finn` |
| `finn-update` | A/B OTA update client (`src/ota/`): Ed25519 manifest check, HTTP Range download, `zstd` delta patching, `systemd-boot`/`GRUB` slot commit/rollback | root (units) / `finn` (read-only status) |
| `finn-android` | Clean-room Android 17 application runtime (`src/android/`): native APK/ZIP reader, binary AXML manifest/layout parser, `resources.arsc` basics, DEX header verification and opcode decoding, ART-style analysis, per-user installer, Qt6/Wayland view bridge | `finn` |
| First-party apps | Adaptive Qt 6 `FinnUI` apps: `finn-settings` (1:1 Figma About & Sound + Form Factor switcher), `finn-files`, `finn-terminal` (native PTY + soft-keys), `finn-notes`, `finn-store`, `finn-viewer`, `finn-monitor`, `finn-installer`, `finn-oobe`, optional `flourish` | `finn` |

`finnd` — a bespoke privileged system daemon — is not written. Instead, FinnOS integrates directly with standard upstream Linux mechanisms (systemd `logind`, `NetworkManager`, `BlueZ`, `PipeWire`/`wpctl`, `UPower`, and sysfs) when present, and marks missing hardware/services honestly disabled when absent.

## The boot chain

Firmware → FinnOS boot menu → kernel → Plymouth → compositor → shell. Every
visible part is FinnOS's:

- The install medium's three menus (syslinux, systemd-boot and the hand-booted
  GRUB menu) carry the same two FinnOS entries, with a three-second wait. An
  installed machine gets a three-second GRUB or systemd-boot menu whose
  distributor is `FinnOS`.
- All boot entries pass `quiet splash vt.global_cursor_default=0
  plymouth.ignore-serial-consoles`. `quiet` keeps kernel text off the console,
  `splash` hands the framebuffer to Plymouth in the initramfs, and the third
  parameter removes the blinking text cursor. The last one matters on any
  machine with a serial console (the ARM dev VM's `ttyAMA0`, a server BMC):
  without it Plymouth sees the serial console and forces its text "details"
  theme, which ignores `quiet` and prints boot messages instead of the mark.
  There is no text-console phase between the firmware logo and the boot mark.
- The dev machine's initramfs loads `virtio_gpu` early so Plymouth has a DRM
  renderer before the compositor starts. Without a renderer Plymouth falls
  back to text even with the splash enabled.
- The `finnos` Plymouth theme draws the Finn mark from `mark.png` on the
  `#0B0E14` boot field, with `ShowDelay=0`. A quiet, macOS-style bar sits under
  it: the fill advances only on real Plymouth boot-progress reports, and until
  one arrives a short highlight sweeps the track without claiming a percentage.
  The theme, its config, the mark and both bar images are copied into the live
  and the installed initramfs, because Plymouth starts before the real root is
  mounted.
- The ribbon "hi." animation is for first-time setup only. `finn-oobe` plays it
  on its first step when `~/.config/finn/oobe_complete` is absent; a normal
  boot has no shell-drawn splash over the desktop. Plymouth has already shown
  the mark and bar by then.
- The compositor takes the display over from Plymouth; `finn-wallpaper` and
  `finn-shell` come up behind it. `Ctrl+Alt+F2` is still a rescue console;
  with the boot cursor disabled it has no visible block cursor, though input
  works normally.

## The lock screen

`LockScreenWindow` is a full-screen overlay with a real `/etc/passwd` user
list and a real `su`-backed password check. It now has three triggers instead
of only the Home Menu item:

- **Super+L**: `finn-comp` writes `/run/finn/state/lock-request`; `finn-shell`
  watches that directory, opens the overlay and consumes the file. The
  compositor knows no app id — it only asks for a lock through a shared path.
- **Idle**: `finn-comp` tracks real keyboard and pointer activity and reads
  `auto_lock_minutes` from the settings file. When the timeout elapses it uses
  the same request file. `0` means never; no timeout is invented.
- **Suspend**: `finn-shell` listens for logind's `PrepareForSuspend` signal and
  locks first when `lock_on_sleep` is set.

`/run/finn/state` is group-writable (`0775 root:finn`) so `finn-shell` can
consume the request that root's compositor wrote. `finn-comp` handles
`ext-session-lock-v1` (`wlr_session_lock_manager_v1` in `lock_tree_`) and gates
WM shortcuts (`Alt+Tab`, `Alt+F4`, `Super+Q`, `Super+D`) when locked;
`LockScreenWindow` runs `su` asynchronously and permits empty-password unlock
only when `passwd -S` explicitly reports `NP` (no password).

The lock screen paints an opaque, cover-fitted wallpaper so nothing behind it
can be seen, with a light scrim for the clock and password field. Settings has
a **Lock Screen Wallpaper** row with Choose Image / Default. An empty lock
wallpaper falls back to the desktop wallpaper, and an empty desktop wallpaper
falls back to `/usr/share/finn/wallpaper/default.webp`, so a machine always
shows an image without a configuration step.

The time saved is the removed waiting: two bootloader menus and the installer
each used to hold ten seconds. Plymouth itself has a small startup cost; it is
there so the time that remains is spent behind the mark rather than behind a
cursor.

What the firmware draws before the kernel starts is the firmware's own and
outside this tree. The mark is the first thing FinnOS draws.

## The display

`finn-comp` is a native C++20 / Qt 6 Wayland Compositor (`src/comp/`, mirroring KWin's workspace/scene architecture). On Linux hardware it links `Qt6::WaylandCompositor` over EGLFS/KMS (`QT_QPA_PLATFORM=eglfs`, `LIBSEAT_BACKEND=seatd`) serving `/run/finn/wayland-0` (`xdg-shell`, `wlr-layer-shell`, fractional scaling, pointer/touch/keyboard input, idle/Super+L lock requests); on non-Linux hosts without `Qt6WaylandCompositor` it builds cleanly into a Qt Quick preview harness for offscreen compositor verification. The scene graph compiles a GPU Dual-Kawase backdrop blur fragment shader (`src/comp/shaders/dual_kawase_blur.frag`).

The scene graph has one fixed 8-layer stacking order (`src/comp/core/scene_stack.hpp`):

```
background → background layer → bottom layer → app toplevels → top layer → fullscreen → overlay → lock
```

`finn-wallpaper` binds to the `background` layer, decodes `finn::kDefaultWallpaperPath` through Qt, and applies theme dimming. `finn-shell` runs a unified adaptive Qt Quick `SystemUI` (`ShellCorona`) that selects Desktop, Tablet, Mobile, or TV containments live from `src/ui/finn/form_factor.hpp` (or explicit `form_factor=` preference in Settings).

Native `FinnSDK` apps use `FinnApplicationWindow` (`FinnSDK::FinnUIQml`, QML `import FinnUI 1.0`) or `FinnWindow` (`FinnSDK::FinnUI`), adapting automatically across Desktop (250px Figma split-pane + floating 88×28 window controls pill), Tablet (collapsible NavRail), Mobile (master/detail + 56dp bottom navigation bar), and TV (10-foot overscan-safe 1.5× typography + D-pad focus rings).

Window management keys:
- `Alt+F1`: cycle focus across app toplevels
- `Super` / `Meta` (or Dock Home button): Home Menu
- `Alt+Space`: Spotlight Search
- `Ctrl+Alt+F2`: rescue console (`getty@tty2`)

A developer restarts the compositor deliberately with `systemctl restart finn-comp`.

## Authority

- The Wayland display socket lives at `/run/finn/wayland-0` (`0660`, group `finn`), and Xwayland sockets live under `/tmp/.X11-unix` (`1777 root:root`) because `finn-comp` runs as root (using `seatd` via `LIBSEAT_BACKEND=seatd`) while all user services and apps run as user `finn`. Services omit `PrivateTmp=yes` so Xwayland and Wine/X11 clients share `/tmp/.X11-unix`.
- `finn-session` runs as `finn` on `$XDG_RUNTIME_DIR/finn/session.sock` (`0600`), checks `SO_PEERCRED`, and spawns launched apps via `posix_spawn`.
- There is no TCP listener and no hidden second door.

## The app model & compatibility

An app in FinnOS is a program opening an `xdg_toplevel` (Wayland) or Xwayland surface bridged into the scene. Nothing under `src/comp` knows any app id (`make lint` enforces this).

`finn-session` aggregates seven real sources into one launcher model:
1. **First-party native apps** from `/usr/share/finn/apps.registry` + `/var/lib/finn/installed`.
2. **Third-party `FinnSDK` apps** registered in `~/.local/share/finn/apps/`.
3. **Linux applications** discovered recursively from Freedesktop `.desktop` directories (including Wine `winemenubuilder` shortcuts under `~/.local/share/applications/wine/`) and Flatpak exports, plus verified AppImages in `~/Applications`, `~/.local/share/finn/appimages` and `/opt`. `Finn Store`, `Finn Files`, and `finn-session` also handle Flatpak installer formats directly (`.flatpakref`, `.flatpak` bundles, and `.flatpakrepo` remotes via `flatpak --user`). `src/compat/appimage_runner` detects AppImage types and host architecture from real magic bytes, probes FUSE at runtime, and plans FUSE or `--appimage-extract-and-run` launches; `src/compat/linux_app_probe` reports ELF interpreter, architecture and missing libraries honestly. See [`docs/linux-app-coverage.md`](linux-app-coverage.md).
4. **Windows executables** (`.exe` / `.msi`, with `.msix` detected but not installable) launched via `finn-win` / Wine when available on `x86_64`, plus portable apps registered in `~/.local/share/finn/windows.conf` and apps discovered from per-prefix Uninstall keys. Each app may use an isolated Wine prefix. `docs/windows-app-coverage.md` records the supported formats, runtime requirements and the gaps — MSIX installation, full `.lnk` resolution and `.ico` extraction are not implemented.
5. **macOS applications** (`.app` bundles installed in `~/.local/share/finn/macos/Applications` or `~/Applications`) launched via **`finn-darwin`** (`src/darwin/`): a clean-room 64-bit Mach-O / Universal dynamic loader (`LC_DYLD_CHAINED_FIXUPS`, `LC_DYLD_INFO_ONLY`, bundled `@rpath` dylibs, pre-flight `diagnose`), Darwin `libSystem.B.dylib` ABI shim (POSIX, sockets, pthreads, CommonCrypto SHA-256), Objective-C 2.0 runtime (`objc_msgsend.S` for `aarch64`/`x86_64`, ABI v2 realization, `_objc_msgSend$<sel>` stubs), CoreFoundation/Foundation (`NSJSONSerialization`, file I/O, persisted `NSUserDefaults`, `NSError`, `NSDate`, `CF*` C APIs), CoreGraphics/QuartzCore (`CGBitmapContextCreate`, `CALayer`), core Swift runtime ABI shims, `AppKit-on-Qt6` bridge (`NSWindow`, `NSMenu`, `NSButton`, `NSTextField`, `NSTextView`, `NSScrollView`, `NSSlider`, `NSProgressIndicator`, `NSOpenPanel`/`NSSavePanel`, `NSTimer`, `NSPasteboard`, `NSWorkspace`, `NSImage`), and HTTP/HTTPS + native XML/`bplist00`/`.icns`/`.pkg`/`.dmg`/`.zip` installer support.
6. **OpenHarmony 6.1 LTS applications (API Level 23)** installed in `~/.local/share/finn/ohos/` and launched via **`finn-ohos`** (`src/ohos/`): native `zlib`-backed ZIP `.hap` / `.hsp` / `.app` App Pack & HTTP/HTTPS installer, `module.json` / `pack.info` JSON5 parser (supporting API 23 `allowSelfRedirect` and `LaunchParam` startup metrics), `resources.index` + icon extractor, ELF `.so` ABI matching, ArkCompiler Panda `PANDA\0\0\0` (`modules.abc`) bytecode VM, `UIAbility` lifecycle + built-in `@ohos.*` kits (`hilog`, `window`, `router`, persisted `preferences`, `file.fs`, `deviceInfo`, `util`, `bundleManager`, `app`, `curves`, `matrix4`), ES module import binding resolution, NAPI (`napi_*`) host bridge, and declarative `ArkUI-on-Qt6` widget bridge (including API 23 `onAttach`/`onDetach` hooks and `Grid` `multiSelect`).
7. **Android 17 applications** installed in `~/.local/share/finn/android/` and launched via **`finn-android`** (`src/android/`): a clean-room runtime with no Waydroid dependency — native `zlib`-backed APK/ZIP reader, binary `AndroidManifest.xml` (AXML) parser with real typed attributes, `resources.arsc` basics, DEX header verification (Adler-32 + SHA-1), a table-driven Dalvik opcode decoder with honest unsupported-opcode diagnostics, ART-style method/class analysis, ELF ABI matching, per-user install/uninstall with real icon extraction, and a Qt6/Wayland view bridge with `onCreate`/`onResume` lifecycle reporting. See [`docs/android-coverage.md`](android-coverage.md) for the exact modelled surface.

## Custom Qt6 stack, tokens & motion

FinnOS does not leave Qt6 with stock desktop widgets. Both `FinnSDK` apps and unpatched third-party Qt6 apps run with:
- `QT_QPA_PLATFORMTHEME=finn` (`libfinnplatformtheme.so`): applies the FinnOS palette generated from `ui/_tokens.fui`, loads `Fixel Display` fonts, maps semantic icons from `/usr/share/finn/icons/`, and watches `/var/lib/finn` + `~/.config/finn` for live dark/light, accent, and reduced-motion updates without restarting apps.
- `QT_STYLE_OVERRIDE=finn` (`libfinnstyle.so`): custom `QProxyStyle` rendering flat hairline borders, tokenized radii, clean button/input/slider/checkbox states, and refined scrollbars.
- `src/ui/finn/spring.hpp`: analytic damped-harmonic-oscillator spring solver (`SpringAnimator`, `springs::kSnappy/kSmooth/kBouncy/kOverscroll`, rubber-band overscroll) integrated into `libfinnui` with live `reduced_motion()` crossfade fallbacks.
- `src/compat/finn_qt_theme.hpp`: the macOS and OpenHarmony compatibility runtimes apply the same live FinnOS palette, Fixel Display type and `finn` widget style as native apps. ArkUI windows use `FinnWindow` frameless glass chrome; AppKit `NSWindow` hosts use FinnOS bone/tile/ink surfaces, accent focus rings and rounded controls. Guest apps therefore follow the desktop theme instead of rendering as generic Qt widgets.

## What is not here yet

- **Android framework coverage (`finn-android`)**: the runtime parses and analyses real APKs and renders a modelled view subset on the FinnOS Qt6/Wayland theme, but it does not implement the Android framework, Binder, Zygote, ART, JNI, Google Mobile Services or full resource-qualifier/drawable inflation. Only a documented Dalvik opcode subset executes; other methods are reported as skipped, never faked. There is no Waydroid fallback.

- **No bespoke privileged `finnd` daemon.** Package installation inside `Finn Store` supports unprivileged per-user Flatpak operations (`flatpak --user`); privileged OS updates run through `finn-update` systemd units and `pkexec`, while system-wide `pacman` installs guide the user to `Finn Terminal`.
- **No weather forecast backend.** The clock pill displays real local time without fabricated temperatures or forecasts.
- **No OS-level native app sandbox or enforced `.finnapp` permissions.** Native C++20/Qt6 apps run directly with user `finn` privileges; `finn-app.json` permissions are declarative metadata in v1, and sandboxed distribution uses `flatpak`. `src/compat/linux_sandbox` cleans the child environment (stripping `LD_*` and plugin-path hijack variables) and can build a bubblewrap argv only when `bwrap` exists, but that is a best-effort namespace wrapper, not an enforced permission sandbox. AppImages are not signature-verified, and there is no GTK theme for third-party GTK apps.
- **Verified boot & hosted repo limits.** `src/ota/finn-update` implements Ed25519 manifest signing, `zstd` delta patching, and `systemd-boot`/`GRUB` A/B slot switching (`vm/finn-install.sh --ab-slots`), and `repo/` provides repository signing/publishing scripts — however Secure Boot, `dm-verity`, TPM2 sealing, live `repo.finnos.org` hosting (`finnos-keyring` ships an empty placeholder keyring), and real-disk A/B power-cycle rollback tests are not complete.
- **Display, DRM hardware & localization limits.** The rewritten Qt 6 Wayland Compositor (`finn-comp`) and unified adaptive `finn-shell` + core apps were compiled, unit-tested under ASan/UBSan, and visually verified via Qt 6 offscreen rendering on macOS (`Qt 6.12`). Physical Linux DRM/KMS (`eglfs_kms` + `Qt6WaylandCompositor` + `LayerShellQt`) runtime testing on target hardware/VM remains to be run when a Linux Wayland target is reachable. Per-display arrangement editor, mixed per-output HiDPI, Wayland IME (`zwp_text_input_v3`), `QTranslator` localization bundles, and cellular modem/telephony (`ModemManager`) are not yet implemented.
- **Partial runtime accessibility enforcement.** High Contrast and Reduced Motion apply live; `text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, and packaged `orca` screen reader integration remain incomplete.
- **ARM64 Wine runtime**: `base/packages.aarch64` does not invent unavailable upstream `wine` packages; `probe_wine_runtime()` inspects binaries/prefixes honestly and keeps launch buttons disabled until a compatible runtime (`wine` + optional translator) exists on the machine.
- **Incomplete macOS framework coverage (`finn-darwin`)**: `finn-darwin` runs unmodified 64-bit Mach-O C, Objective-C, Foundation, CoreGraphics/QuartzCore, core Swift ABI, and AppKit-on-Qt6 apps natively on matching CPU architecture (`arm64` on `aarch64`, `x86_64` on `x86_64`) and downloads/installs `.app`/`.pkg`/`.zip`/`.dmg` packages (using `7z` for HFS+/APFS `.dmg` extraction). Full Swift stdlib & SwiftUI, Metal GPU shaders, WebKit, CoreData, Security/Keychain, and Apple App Store / iCloud are not yet implemented; `finn-darwin diagnose` lists every missing framework/symbol honestly before launch.
- **OpenHarmony 6.1 LTS system kit coverage (`finn-ohos`)**: `finn-ohos` installs `.hap`/`.hsp`/`.app` packages locally or over HTTP/HTTPS, executes Panda `modules.abc` bytecode and `UIAbility` lifecycles with API 23 `startupTimeMs` & `allowSelfRedirect`, bridges ArkUI onto Qt6/Wayland, and loads host-ABI NAPI `.so` modules. Proprietary/hardware system kits (`ArkWeb`, `DistributedDataObject`, Telephony, NFC, Camera, Sensor, MapKit) and OpenHarmony cryptographic signature enforcement are not yet bridged and are reported honestly by `finn-ohos diagnose`.
