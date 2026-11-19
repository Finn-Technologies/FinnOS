# Architecture

The processes and shared libraries that make up FinnOS.

| Component | Owns | Runs as |
|---|---|---|
| `finn-comp` | DRM/headless output, seats, pointer/touch/tablet, xdg-shell, layer-shell, `ext-session-lock-v1`, foreign-toplevel, `wlr_fractional_scale_v1`, Xwayland bridge, screencopy | root |
| `libfinnui` (`FinnSDK`) | Qt6 C++20 UI library: tokens, theme manager, spring motion (`spring.hpp`), custom CSD chrome (`FinnWindow`, `FinnFormFactor`), LayerShell windows, custom widgets, `SessionClient` | shared library |
| `libfinnplatformtheme` & `libfinnstyle` | Custom Qt6 `QPlatformTheme` and `QProxyStyle` injected via `QT_QPA_PLATFORMTHEME=finn` & `QT_STYLE_OVERRIDE=finn` | Qt6 plugins |
| `finn-session` | Unified app catalogue, `.desktop`/Flatpak/Wine discovery, launch broker, runtime compat probe | `finn` |
| `finn-shell` | Responsive desktop/tablet/phone dock, active-window indicators, Home Menu, Spotlight Search, Control Center, notifications, async lock screen | `finn` |
| `finn-wallpaper` | Background WebP wallpaper surface (`background` layer) | `finn` |
| `finn-update` | A/B OTA update client (`src/ota/`): Ed25519 manifest check, HTTP Range download, `zstd` delta patching, `systemd-boot`/`GRUB` slot commit/rollback | root (units) / `finn` (read-only status) |
| First-party apps | `finn-settings`, `finn-files`, `finn-terminal`, `finn-notes`, `finn-store`, `finn-viewer`, `finn-monitor`, `finn-installer`, `finn-oobe`, optional `flourish` | `finn` |

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

`finn-comp` uses wlroots 0.20. The backend is `drm` in a hardware session or `headless` for VM/automated verification. It advertises `wl_compositor`, `wl_shm`, `wl_subcompositor`, `wl_data_device_manager`, `xdg_wm_base`, `zwlr_layer_shell_v1`, `zwlr_foreign_toplevel_manager_v1`, `ext-session-lock-v1`, `wlr_fractional_scale_manager_v1`, `zwlr_screencopy_manager_v1`, `zxdg_output_manager_v1`, pointer/keyboard/`WLR_INPUT_DEVICE_TOUCH`/`WLR_INPUT_DEVICE_TABLET_TOOL` input, XKB `kb_layout` loading, and Xwayland on demand when X11 support is enabled.

The scene graph has one fixed stacking order:

```
background → background layer → bottom layer → app toplevels → top layer → fullscreen → overlay → lock
```

`finn-wallpaper` binds to the `background` layer, decodes `finn::kDefaultWallpaperPath` through Qt, and applies theme dimming. `finn-shell` surfaces bind to `top` (dock) and `overlay` (popovers, toasts, lock screen) with strict pointer input masks so transparent overlay regions pass clicks cleanly to apps beneath, adapting across `Desktop`, `Tablet`, and `Phone` breakpoints (`src/shell/shell_layout.hpp`).

Native `FinnSDK` apps use `FinnWindow` (custom frameless CSD with window controls, collapsible sidebar slot, edge resize, drag-move, and `FinnFormFactor` adaptation for `Desktop`, `Tablet`, and `Phone`). Foreign Wayland/Qt/GTK/X11 clients receive lightweight compositor SSD or keep their client titlebars.

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

`finn-session` aggregates five real sources into one launcher model:
1. **First-party native apps** from `/usr/share/finn/apps.registry` + `/var/lib/finn/installed`.
2. **Third-party `FinnSDK` apps** registered in `~/.local/share/finn/apps/`.
3. **Linux applications** discovered recursively from Freedesktop `.desktop` directories (including Wine `winemenubuilder` shortcuts under `~/.local/share/applications/wine/`) and Flatpak exports. `Finn Store`, `Finn Files`, and `finn-session` also handle Flatpak installer formats directly (`.flatpakref`, `.flatpak` bundles, and `.flatpakrepo` remotes via `flatpak --user`).
4. **Windows executables** (`.exe` / `.msi`) launched via `finn-win` / Wine when available on `x86_64`, plus portable apps registered in `~/.local/share/finn/windows.conf`.
5. **macOS applications** (`.app` bundles installed in `~/.local/share/finn/macos/Applications` or `~/Applications`) launched via **`finn-darwin`** (`src/darwin/`): a clean-room 64-bit Mach-O / Universal dynamic loader (`LC_DYLD_CHAINED_FIXUPS`, `LC_DYLD_INFO_ONLY`, bundled `@rpath` dylibs), Darwin `libSystem.B.dylib` ABI shim, Objective-C 2.0 runtime (`objc_msgsend.S` for `aarch64` and `x86_64`, ABI v2 realization, `_objc_msgSend$<sel>` stubs), CoreFoundation/Foundation, basic AppKit-on-Qt6 bridge, and native XML/`bplist00` plist, `.icns` PNG icon, `.pkg` (XAR + gzip/cpio), and `.dmg` (UDIF `koly`/`blkx`) handling. `finn-darwin diagnose` reports exact missing frameworks/symbols before launch so unsupported apps never fail silently.

## Custom Qt6 stack, tokens & motion

FinnOS does not leave Qt6 with stock desktop widgets. Both `FinnSDK` apps and unpatched third-party Qt6 apps run with:
- `QT_QPA_PLATFORMTHEME=finn` (`libfinnplatformtheme.so`): applies the FinnOS palette generated from `ui/_tokens.fui`, loads `Fixel Display` fonts, maps semantic icons from `/usr/share/finn/icons/`, and watches `/var/lib/finn` + `~/.config/finn` for live dark/light, accent, and reduced-motion updates without restarting apps.
- `QT_STYLE_OVERRIDE=finn` (`libfinnstyle.so`): custom `QProxyStyle` rendering flat hairline borders, tokenized radii, clean button/input/slider/checkbox states, and refined scrollbars.
- `src/ui/finn/spring.hpp`: analytic damped-harmonic-oscillator spring solver (`SpringAnimator`, `springs::kSnappy/kSmooth/kBouncy/kOverscroll`, rubber-band overscroll) integrated into `libfinnui` with live `reduced_motion()` crossfade fallbacks.

## What is not here yet

- **No bespoke privileged `finnd` daemon.** Package installation inside `Finn Store` supports unprivileged per-user Flatpak operations (`flatpak --user`); privileged OS updates run through `finn-update` systemd units and `pkexec`, while system-wide `pacman` installs guide the user to `Finn Terminal`.
- **No weather forecast backend.** The clock pill displays real local time without fabricated temperatures or forecasts.
- **No OS-level native app sandbox or enforced `.finnapp` permissions.** Native C++20/Qt6 apps run directly with user `finn` privileges; `finn-app.json` permissions are declarative metadata in v1, and sandboxed distribution uses `flatpak`.
- **Verified boot & hosted repo limits.** `src/ota/finn-update` implements Ed25519 manifest signing, `zstd` delta patching, and `systemd-boot`/`GRUB` A/B slot switching (`vm/finn-install.sh --ab-slots`), and `repo/` provides repository signing/publishing scripts — however Secure Boot, `dm-verity`, TPM2 sealing, live `repo.finnos.org` hosting (`finnos-keyring` ships an empty placeholder keyring), and real-disk A/B power-cycle rollback tests are not complete.
- **Display, input & localization limits.** Single global output scale/layout (`outputs_.front()`), no per-display arrangement editor or mixed per-output HiDPI, no compositor GPU shader backdrop blur, no Wayland IME (`zwp_text_input_v3`), no `QTranslator` localization bundles, and no cellular modem/telephony stack (`ModemManager`).
- **Partial runtime accessibility enforcement.** High Contrast and Reduced Motion apply live; `text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, and packaged `orca` screen reader integration remain incomplete.
- **ARM64 Wine runtime**: `base/packages.aarch64` does not invent unavailable upstream `wine` packages; `probe_wine_runtime()` inspects binaries/prefixes honestly and keeps launch buttons disabled until a compatible runtime (`wine` + optional translator) exists on the machine.
- **Incomplete macOS framework coverage (`finn-darwin`)**: `finn-darwin` runs unmodified 64-bit Mach-O C, Objective-C, Foundation, and basic AppKit-on-Qt6 apps natively on matching CPU architecture (`arm64` on `aarch64`, `x86_64` on `x86_64`) and natively installs `.app`/`.pkg` (gzip+cpio) packages (with `7z` used for HFS+/APFS `.dmg` extraction). Complex macOS frameworks — Swift runtime / SwiftUI, Metal / CoreGraphics rendering, QuartzCore, WebKit, CoreData, Network framework, Security/Keychain, and Apple App Store / iCloud — are not yet implemented; `finn-darwin diagnose` lists every missing framework/symbol honestly before launch.
