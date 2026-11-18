# Form-Factor Simulator (`finn-sdk simulate`)

`finn-sdk simulate` lets developers test FinnSDK applications under phone, tablet, and desktop viewport dimensions and HiDPI scale factors.

---

## 1. Presets & CLI

| Form Factor | Default Orientation | Portrait Size | Landscape Size | Default Scale |
|---|---|---|---|---|
| `phone` | `portrait` | `390x844` | `844x390` | `2.0` |
| `tablet` | `portrait` | `768x1024` | `1024x768` | `2.0` |
| `desktop` | `landscape` | `900x1440` | `1440x900` | `1.0` |

Usage:

```sh
finn-sdk simulate --form-factor phone .
finn-sdk simulate --form-factor tablet --orientation landscape --headless .
finn-sdk simulate --form-factor desktop --dry-run --json .
```

Options:
- `--form-factor phone|tablet|desktop` (required)
- `--orientation portrait|landscape`
- `--scale <float>` (overrides default scale)
- `--headless` — spawns an isolated headless `finn-comp` (`WLR_BACKENDS=headless`) bound to `$XDG_RUNTIME_DIR/finn-sim-<pid>` with `FINN_OUTPUT_MODE`, `FINN_OUTPUT_WIDTH`, `FINN_OUTPUT_HEIGHT`, and `FINN_OUTPUT_SCALE` (`src/comp/server.cpp:152-160,1345`).
- `--dry-run` (optional `--json`) — resolves geometry and environment variables without building or launching binaries.

---

## 2. Exported Environment Variables

`finn-sdk simulate` exports:
- `FINN_FORM_FACTOR` (`phone`, `tablet`, or `desktop`)
- `FINN_SIM_ORIENTATION` (`portrait` or `landscape`)
- `FINN_SIM_WIDTH`, `FINN_SIM_HEIGHT`, `FINN_SIM_SCALE`
- `FINN_OUTPUT_MODE` (`<W>x<H>`), `FINN_OUTPUT_WIDTH`, `FINN_OUTPUT_HEIGHT`, `FINN_OUTPUT_SCALE` (consumed directly by `finn-comp` in `src/comp/server.cpp:152-160`)
- `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`

---

## 3. Current Reality & Honest Limits

1. **Headless compositor output works (`--headless`)**: `finn-comp` already reads `WLR_BACKENDS=headless`, `FINN_OUTPUT_MODE`, `FINN_OUTPUT_SCALE`, and `FINN_WAYLAND_SOCKET` (`src/comp/server.cpp`). When `finn-comp` is installed on FinnOS, `--headless` starts an output matching the target form factor.
2. **No automatic window resizing or responsive shell in `libfinnui` yet**: `FinnWindow` (`src/ui/finn/window.cpp`) does not currently read `FINN_FORM_FACTOR` or `FINN_SIM_WIDTH`/`FINN_SIM_HEIGHT`, nor does it switch to phone/tablet chrome. Apps calling `window.resize(740, 480)` keep their requested desktop size unless constrained by the compositor output (e.g. maximized) or adapted in app code.
3. **No touch/gesture synthesis**: `finn-sdk simulate` does not emulate touch events or virtual keyboards (`wl_touch` / `text-input-v3` are not implemented; see Audit B2).

---

## 4. Proposal: Responsive Form-Factor Support in `libfinnui`

To make `FINN_FORM_FACTOR` and `FINN_SIM_*` drive native `FinnWindow` behavior automatically (proposed for `src/ui/`, not yet implemented):

1. **`src/ui/finn/tokens.hpp` — Form-factor helper**:
   - Add `enum class FormFactor { Desktop, Tablet, Phone };`
   - Add `FormFactor current_form_factor(int window_width_dp = -1)` reading `FINN_FORM_FACTOR` first (`phone`, `tablet`, `desktop`) and falling back to breakpoints (`< 600dp` = `Phone`, `600..1023dp` = `Tablet`, `>= 1024dp` = `Desktop`).
2. **`src/ui/finn/window.cpp` — `FinnWindow` sizing & sidebar adaptation**:
   - In `FinnWindow::FinnWindow(...)`, if `FINN_SIM_WIDTH` and `FINN_SIM_HEIGHT` are set to valid positive integers (or `FINN_FORM_FACTOR=phone`), clamp initial `resize()` to the simulated viewport and set `kShadowMargin = 0` + fullscreen/edge-to-edge geometry when `FormFactor::Phone` is active.
   - Auto-collapse `sidebar_container_` (`setSidebarCollapsed(true)`) below `600dp` (`Phone`) and expose a `formFactorChanged(FormFactor)` signal on `resizeEvent()`.
3. **`src/ui/finn/components.cpp` — Touch target minimum sizing**:
   - Scale minimum interactive hit targets (`FinnButton`, `FinnIconButton`, `FinnToggle`) to `>= 44x44dp` when `current_form_factor() != FormFactor::Desktop`.
4. **Testing**: Add unit coverage in `tests/unit/` verifying breakpoint classification and `FINN_FORM_FACTOR` override precedence.
