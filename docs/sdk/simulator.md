# FinnSDK Simulator & Adaptive Form Factors

`finn-sdk simulate` launches a FinnOS app with deterministic viewport dimensions and `FINN_FORM_FACTOR` overrides so developers can test responsive layouts on a desktop machine.

## CLI presets

```sh
finn-sdk simulate --form-factor desktop <app-dir>   # 1280×800
finn-sdk simulate --form-factor tablet  <app-dir>   # 1024×768
finn-sdk simulate --form-factor phone   <app-dir>   # 390×844
finn-sdk simulate --form-factor tv      <app-dir>   # 1920×1080 (10-foot 1.5× scale)
```

Exported environment variables:
- `FINN_FORM_FACTOR=desktop|tablet|mobile|tv`
- `FINN_SIM_WIDTH` / `FINN_SIM_HEIGHT`
- `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`

For headless CI or macOS host preview without a Wayland compositor, combine with `QT_QPA_PLATFORM=offscreen` or run `finn-comp --preview`.

## How `libfinnui` & `libfinnui_qml` adapt (`src/ui/finn/form_factor.hpp`)

`resolve_adaptive_form_factor()` resolves `AdaptiveFormFactor` (`Desktop`, `Tablet`, `Mobile`, `TV`) in order:
1. `FINN_FORM_FACTOR` environment override
2. `form_factor=` preference in `~/.config/finn/settings`
3. TV display hint
4. Window/screen width breakpoints from `ui/_tokens.fui` (`bp_phone_max=599`, `bp_tablet_max=1024`)

`FinnApplicationWindow` (`FinnUI 1.0`) and `FinnWindow` adapt chrome automatically:
- **Desktop**: Split-pane sidebar + floating 88×28 window controls pill.
- **Tablet**: Collapsible navigation rail.
- **Mobile**: Master/detail stack + bottom navigation bar + 44 dp touch targets.
- **TV**: 10-foot overscan-safe margins, 1.5× typography scale, and D-pad focus rings.

## Honest limits

- **Shell form factors**: While `FinnApplicationWindow`, `finn-oobe`, and `recovery-mode` adapt across form factors, `finn-shell`'s `TabletShell.qml`, `MobileShell.qml`, and `TvShell.qml` are currently empty 6-line `Item` stubs (only `DesktopShell.qml` is implemented).
- **Hardware emulation**: `finn-sdk simulate` resizes viewports and sets form-factor hints; it does not emulate physical touchscreens, virtual keyboards, cellular modems, or DRM refresh pacing. See [docs/emulator-quickstart.md](../emulator-quickstart.md).
