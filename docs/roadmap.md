# Roadmap

FinnOS has migrated its UI, shell, native applications, and developer SDK to
**Qt6 + C++20** (`FinnSDK`, `libfinnplatformtheme`, `libfinnstyle`) on top of
the `wlroots 0.20` compositor. These milestones track what has shipped vs. what
remains next.

## Shipped in the Qt6 + FinnSDK milestone

- **Custom Qt6 OS integration**: `libfinnui` (`FinnSDK::FinnUI`), `QT_QPA_PLATFORMTHEME=finn`,
  and `QT_STYLE_OVERRIDE=finn`, driven by `ui/_tokens.fui` and Fixel Display.
- **Developer SDK (`FinnSDK`)**: `finn-sdk` CLI (`new`, `build`, `run`, `register`, `check`),
  CMake package (`find_package(FinnSDK)`), headers (`<FinnSDK/FinnSDK.hpp>`), and `sdk/template/`.
- **Complete FinnUX shell (`finn-shell`)**: 4-pill Figma Dock with live `wlr-foreign-toplevel-management`
  running dots & Window Switcher, Home menu, Spotlight search + math evaluator,
  Control Center (appearance switch + real NetworkManager/BlueZ/UPower/PipeWire probes),
  Figma notifications (`340×80`), and macOS-inspired multi-user Lock Screen.
- **Settings & Accessibility Suite (`finn-settings`)**: `ChromeMode::FigmaSplit`,
  250px Figma sidebar with circular icon badges, real hardware/system controls,
  and full Accessibility suite (Orca Screen Reader, High Contrast, Color Filters,
  Text Scale, Reduced Motion, Sticky/Filter Keys, Pointer Size).
- **7 Native Apps + Installer + OOBE + Wallpaper**: `Settings`, `Finn Files`, `Finn Terminal`,
  `Finn Notes`, `Finn Store` (Windows 11 Store layout with `FinnNavRail`), `Finn Viewer`,
  `Finn Monitor`, `finn-installer`, `finn-oobe`, and `finn-wallpaper`.
- **Compatibility layers**: XDG `.desktop` + Flatpak + AppImage discovery, `finn-open` dispatcher,
  on-demand `Xwayland` in `finn-comp`, PE inspection + Wine prefix launcher (`wine_runner`),
  and Waydroid Android runtime (`android_runner`).

## 1. The root authority — `finnd`

Machine-level privileged actions that should not live inside user apps or the
compositor remain future work:

- unix control socket `/run/finn/control.sock` (`0660 root:finn-authority`) with `SO_PEERCRED`;
- audited verbs for sysfs brightness, poweroff/reboot orchestration, system updates, and
  root-owned network/package actions;
- no app launching (that stays `finn-session`) and no TCP listener.

## 2. App sandbox

Each app running in a transient systemd user unit (or user namespace/seccomp
profile) with `NoNewPrivileges`, `ProtectSystem=strict`, scoped state dirs,
and no direct access to `/run/finn` outside controlled sockets. Once `finnd`
handles DRM/power/brightness actuators, `finn-comp` can also drop root via `seatd`.

## 3. Compositor polish & display features

- Compositor-level scene backdrop blur behind translucent `layer-shell` and glass surfaces.
- Multi-monitor outputs and mixed per-output fractional/integer scaling.
- `ext-session-lock-v1` protocol integration in `LockScreenWindow`, clipboard history UI, and Wayland IME text input.
- Automated OTA A/B update slots and signed first-party repositories.

## What "done" means

A change is done when:

- `src/comp` contains zero app ids (`make lint`);
- host tests (`make test`, `make test-unit`, `make lint`, `base/mkfinniso --check`) pass;
- the VM build installs cleanly and `vm/shot.sh` screenshots are visually verified;
- docs accurately state both implemented features and remaining gaps.
