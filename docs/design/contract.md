# The FinnOS design contract

This page is the rule for the system architecture. A change that makes the
code and this page disagree is not finished.

## The base stays

Arch Linux: the kernel, firmware, systemd, pacman, mesa, libdrm, libinput,
Wayland, and Qt 6. The installer, the ISO build, and the package lists in `base/`
stay. The FinnOS userland builds cleanly on top of this base.

The base is an engineering fact, not a brand. Nothing a person reads while
booting or using FinnOS names it: the menus, the console, the Plymouth theme,
the identity files, and the app copy are FinnOS's, and `tools/check-branding.sh`
(run by `make lint` and `base/mkfinniso --check`) refuses another distribution's
name in those surfaces. The documentation is the exception on purpose — it says
what the machine is actually built from.

## The boot chain

`finn-boot` (`finn-bootx64.efi`, `finn-bootaa64.efi`) provides a freestanding
UEFI GOP graphical Startup Manager over UAPI Boot Loader Specification entries
and Unified Kernel Images. The kernel command line carries
`quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles`, so
Plymouth owns the display from inside the initramfs with no blinking text
cursor. The `finnos` theme shows the 72 × 72 px Finn mark (`finn::kBootMarkSizePx`)
immediately with a bar driven only by real progress reports, and hands off to
`finn-comp` (or `finn-recovery.service` when `finn.recovery` is passed). Both
the live and installed initramfs carry the same theme assets.

## The programs

| Program | Owns | Runs as |
|---|---|---|
| `finn-boot` | UEFI GOP Startup Manager, BLS/UKI entries, boot-counting | UEFI firmware |
| `finn-comp` | Qt 6 `QWaylandQuickCompositor`: outputs, workspaces, 8-layer scene stack, CPU backdrop blur & Liquid Glass | root (`LIBSEAT_BACKEND=seatd`) |
| `libfinnui` & `libfinnui_qml` (`FinnSDK`) | Qt 6 C++20 widget & `FinnUI 1.0` QML libraries, CSD windows, springs, tokens | shared libraries |
| Qt 6 Finn plugins | `libfinnplatformtheme.so` + `libfinnstyle.so` for all Qt 6 apps | Qt 6 plugins |
| `finn-shell` | Qt Quick System UI: dock, Home Menu, Spotlight, Control Center, notifications, Overview, Lock Screen | `finn` |
| `finn-wallpaper` | standalone background wallpaper surface (when `FINN_WALLPAPER_STANDALONE=1`) | `finn` |
| `finn-session` | unified 7-source `AppRegistry`, `SO_PEERCRED` launch/uninstall/compat broker | `finn` |
| `finn-update` | OTA A/B slot status, Ed25519 manifest check/download/apply, boot health commit | `root` (units) / `finn` (read-only status) |
| `finnd` | audited root machine authority | root — not written yet |

The compositor knows nothing about app ids, packages, the shell, or menus.
If a module needs to know what an app is called, it is in the wrong process.

## Authority

| Socket | Owner | Mode | Serves |
|---|---|---|---|
| `$XDG_RUNTIME_DIR/finn/session.sock` | `finn-session` | `0600 finn` | app launch, uninstall, compat probe |
| `/run/finn/control.sock` | `finnd` (reserved) | `0660 root:finn-authority` | machine authority, not written yet |

The display socket is `/run/finn/wayland-0`, `0660 root:finn`, and compositor
state IPC lives in `/run/finn/state` (`0775 root:finn`). Clients connect using
the absolute `WAYLAND_DISPLAY=/run/finn/wayland-0`. There is no TCP listener and
no hidden control daemon.

## The app contract

An app is a program whose window appears as an `xdg_toplevel`. The compositor
treats all toplevels uniformly.

Catalogue metadata lives in `assets/apps.registry` (`/usr/share/finn/apps.registry`):

```
id | Name | icon | command | note | source
```

`/var/lib/finn/installed` lists installed native app ids. First-party apps
live in `apps/<id>/` and link `FinnSDK::FinnUIQml` or `FinnSDK::FinnUI`.
Third-party apps build standalone via `find_package(FinnSDK)` / `finn-sdk`, or
integrate via Linux (`.desktop`, Flatpak, AppImage), Windows (`finn-win`/Wine),
macOS (`finn-darwin`), OpenHarmony (`finn-ohos`), or Android (`finn-android`).

CI (`make lint`) fails if any known app id appears under `src/comp`.

## Display

`finn-comp` is a Qt 6 `QWaylandQuickCompositor` implementing `xdg-shell`,
`xdg-decoration`, `text-input-v3`, and `wp_viewporter`. Native Qt 6 `FinnSDK`
apps use custom client-side chrome (`FinnApplicationWindow` or `FinnWindow`,
with `AdaptiveFormFactor` across `Desktop`/`Tablet`/`Mobile`/`TV`); unmodified
toolkits keep their own titlebars. Scene layers keep one fixed order
(`src/comp/core/scene_stack.hpp`):

```
background -> background layer -> bottom layer -> app toplevels -> top layer -> fullscreen -> overlay -> lock
```

## Sandbox

Apps currently run as `finn` with the user's authority (`linux_sandbox=env-only`).
Per-app transient systemd units or enforced namespace/seccomp sandboxing and
privilege-separated `finnd` remain planned.

## Honesty

A hardware sensor, network device, Bluetooth adapter, battery, brightness
slider, or compatibility runtime that is absent is reported as absent or
disabled — never faked with plausible numbers.

## One copy of a fact

Shared paths and constants (`kBootMarkSizePx = 72`) live in
`src/common/paths.hpp`. Palette and design tokens live in `ui/_tokens.fui` and
generate `src/ui/generated_tokens.hpp`.

## Non-goals for the current release

No multi-monitor independent scale/layout manager, no Secure Boot / `dm-verity`
/ TPM2 sealing, no cellular telephony stack, and no built-in AI model or
telemetry. Backdrop blur and Liquid Glass refraction are currently rendered on
CPU in `src/comp/server.cpp` and `src/comp/backdrop_item.cpp`
(`src/common/liquid_glass.hpp`); `src/comp/shaders/dual_kawase_blur.frag` is
compiled but not yet wired to a QML `ShaderEffect`.
