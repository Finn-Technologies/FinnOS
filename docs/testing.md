# Testing

## Host

```sh
make test
make test-unit
make lint
```

- `make test` runs the Python suite with no display needed: `test_base.py`
  (package lists, units, installer scripts, icon parity), `test_catalogue.py`
  (`apps.registry`), `test_tokens.py` (`ui/_tokens.fui` vs generated header),
  and `test_icons.py` (SVG icon assets). Note: QEMU ISO boot tests skip
  automatically when `FINN_TEST_ISO` is not set.
- `make test-unit` compiles C++20 unit tests under `-fsanitize=address,undefined`
  for `AppRegistry` (`test_app_registry`), `search` & `WineRunner` CLI /
  registry helpers (`test_search`, `test_compat`), `sys_files`
  (`test_sys_files`), `SessionClient` (`test_session_client`), and animation
  easings (`test_animation`).
- `make lint` checks trailing whitespace, generated token sync
  (`tokens.py --check`), and confirms zero app ids appear inside `src/comp`.
- `base/mkfinniso --check` verifies parity between `base/` and `base/airootfs/`
  for all systemd units and tmpfiles configs.

## The VM

Display, Qt6 plugin, wlroots compositor, and shell integration checks require
Arch + Qt6 + seatd/headless Wayland in the dev VM:

1. Full `make` + `make install` compiles `finn-comp`, `finn-session`,
   `libfinnui`, `finnplatformtheme`, `finnstyle`, `finn-shell`, `finn-open`,
   and all 8 targets under `apps/`.
2. `finn-sdk check`, `finn-sdk new`, and `finn-sdk build` verify standalone
   third-party SDK compilation against installed `FinnSDKConfig.cmake`.
3. `finn-comp` starts on DRM (through `seatd`) or headless/pixman, serves
   `xdg_wm_base`, `zwlr_layer_shell_v1`, `zwlr_foreign_toplevel_management_v1`,
   `zwlr_screencopy_manager_v1`, and on-demand `Xwayland`.
4. `finn-session --list` advertises all 7 native apps; `finn-session --launch`
   starts native Qt6 apps cleanly onto `/run/finn/wayland-0`.
5. `vm/shot.sh` captures frames with `grim` in Light and Dark appearances and
   verifies `finn-wallpaper`, `finn-shell` (Dock, Home menu, Spotlight Search,
   Control Center), and native Qt6 app windows.

## What is not tested automatically

- **Automated pointer/keyboard UI driving across arbitrary app widgets**:
  basic pointer/keyboard forwarding has been checked manually on the VM, while
  Qt widget drag-and-drop and IME input-method protocols remain untested.
- **Physical x86_64 bare-metal hardware & native Wine execution**: the ARM64 dev
  VM verifies `finn::inspect_pe` and clean missing-runtime handling (`Wine` is
  not available natively on Arch Linux ARM64 without x86 emulation).
- **Multi-monitor and mixed per-output HiDPI**: v1 targets single-output
  workspaces cleanly.
- **True compositor-wide multi-window backdrop blur**: glass surfaces blur/tint
  cohesively via tokens and translucency; scene-graph Gaussian blur of arbitrary
  windows behind another window is not implemented in `finn-comp`.
- **Per-app sandboxing and root `finnd` daemon**: not implemented yet (see
  [roadmap.md](roadmap.md)).
