# The FinnOS design contract

This page is the rule for the system architecture. A change that makes the
code and this page disagree is not finished.

## The base stays

Arch Linux: the kernel, firmware, systemd, pacman, mesa, libdrm, libinput,
Wayland, and Qt6. The installer, the ISO build and the package lists in `base/`
stay. The FinnOS userland builds cleanly on top of this base.

The base is an engineering fact, not a brand. Nothing a person reads while
booting or using FinnOS names it: the menus, the console, the Plymouth theme,
the identity files and the app copy are FinnOS's, and `base/mkfinniso --check`
refuses another distribution's name in those surfaces. The documentation is
the exception on purpose — it says what the machine is actually built from.

## The boot chain

The kernel command line carries `quiet splash vt.global_cursor_default=0`, so
Plymouth owns the display from inside the initramfs and there is no blinking
text cursor. The `finnos` theme shows the Finn mark immediately, holds it
with a bar driven only by real progress reports (an unmeasured sweep until
one arrives, never a fake percentage), and hands off to `finn-comp`. The
bootloader menus wait three seconds. Both the live and the installed initramfs
carry the theme, and `base/setup.sh` and `vm/finn-chroot.sh` install the same
files so a machine and the medium cannot show different marks.

## The programs

| Program | Owns | Runs as |
|---|---|---|
| `finn-comp` | outputs, seats, toplevels, layer surfaces, on-demand Xwayland | root (DRM via `seatd`) |
| `libfinnui` (`FinnSDK`) | Qt6 + C++20 UI library, CSD window frame, widgets, tokens | shared library |
| Qt6 Finn plugins | `finnplatformtheme` + `finnstyle` for all Qt6 apps | Qt6 plugins |
| `finn-shell` | dock, clock, Spotlight search, Home menu, Control Center, notifications | `finn` |
| `finn-wallpaper` | desktop background layer surface | `finn` |
| `finn-session` | registry, installed list, launch broker | `finn` |
| `finn-update` | OTA A/B slot status, signed manifest check/download/apply, boot health commit | `root` (units) / `finn` (read-only status) |
| `finnd` | audited root machine authority | root — not written yet |

The compositor knows nothing about app ids, packages, the shell or menus.
If a module needs to know what an app is called, it is in the wrong process.

## Authority

| Socket | Owner | Mode | Serves |
|---|---|---|---|
| `$XDG_RUNTIME_DIR/finn/session.sock` | `finn-session` | `0600 finn` | app launch |
| `/run/finn/control.sock` | `finnd` (reserved) | `0660 root:finn-authority` | machine authority, not written yet |

The display socket is `/run/finn/wayland-0`, `0660 root:finn`. Clients connect
using that absolute `WAYLAND_DISPLAY`. There is no TCP listener and no hidden
control daemon.

## The app contract

An app is a program whose window appears as an `xdg_toplevel` (native Wayland)
or an on-demand `Xwayland` toplevel. The compositor treats all toplevels
uniformly.

Catalogue metadata lives in `assets/apps.registry` (`/usr/share/finn/apps.registry`):

```
id | Name | icon | command | note | source
```

`/var/lib/finn/installed` lists installed native app ids. First-party apps
live in `apps/<id>/` and link `FinnSDK::FinnUI`. Third-party apps can build
standalone via `find_package(FinnSDK)` / `finn-sdk`, or run as unmodified Linux
(Wayland, X11, Flatpak) and Wine Windows applications via `finn-open` / `.desktop`
discovery.

CI (`make lint`) fails if any known app id appears under `src/comp`.

## Display

wlroots 0.20 with `xdg-shell`, `wlr-layer-shell`,
`wlr-foreign-toplevel-management`, `ext-session-lock-v1`, `wlr_fractional_scale_v1`,
screencopy, touch/tablet input, and on-demand `Xwayland`. Native
Qt6 `FinnSDK` apps use custom client-side CSD (`FinnWindow`, with responsive
`Desktop`/`Tablet`/`Phone` modes); unmodified toolkits
keep their own titlebars. Scene layers keep one fixed order:
`background -> background layer -> bottom layer -> app toplevels -> top layer -> fullscreen -> overlay -> lock`.

## Sandbox

Apps currently run as `finn` with the user's authority. Per-app transient systemd
units / namespace sandboxing and privilege-separated `finnd` remain planned.

## Honesty

A hardware sensor, network device, Bluetooth adapter, battery, brightness
slider, or Wine runtime that is absent is reported as absent or disabled — never
faked with plausible numbers.

## One copy of a fact

Shared paths live in `src/common/paths.hpp`. Palette tokens live in
`ui/_tokens.fui` and generate `src/ui/generated_tokens.hpp`.

## Non-goals for the current release

No multi-monitor independent scale/layout manager, no compositor GPU shader backdrop blur, no Secure Boot / `dm-verity` / TPM2 sealing, no cellular telephony stack, and no built-in AI model or telemetry.
