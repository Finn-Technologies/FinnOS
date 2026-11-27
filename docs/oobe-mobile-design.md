# OOBE flow and mobile/tablet shell — design rationale

This page describes the first-class setup flow in
`src/shell/full_oobe_flow.{hpp,cpp}` (now wired into the 10-step `apps/oobe/main.cpp`
and `ShellCorona::maybeLaunchFirstBootOobe()`, backed by `tools/finn-oobe-apply`)
and records the token-derived mobile/tablet geometry rationale.

> **Current status note:** `src/shell/mobile_shell_layout.{hpp,cpp}` was removed
> during the QML `finn-shell` migration; its geometry tokens live on in
> `src/ui/finn/form_factor.hpp` and `FinnApplicationWindow`, while
> `src/shell/ui/mobile/MobileShell.qml` and `tablet/TabletShell.qml` are
> currently empty 6-line `Item` stubs awaiting QML implementation. The
> `full_oobe_flow` state machine and capability-gating contract below remain
> active and tested under ASan/UBSan (`tests/unit/test_oobe.cpp`).

> **Honesty first.** The flow model reports outcomes handed to it by a caller
> that has actually performed work. It never invents a Wi-Fi connection, an
> account, a locale, or a capability. A field with no measurement stays
> `Unknown`/`NotAttempted`/`Unset` and is named in the completion report. The
> Qt host composes real `finn::ui` widgets; it is not a static mockup.

---

## 1. Scope and status

| Piece | Status here | Notes |
|---|---|---|
| Eight-step flow model, history, back/forward, per-step validation | **Implemented** (pure C++20, headless) | `full_oobe_flow.hpp/.cpp`; verified by a standalone assertion test |
| Capability snapshot with `Unknown`/`Available`/`Unavailable` | **Implemented** | All six capabilities default to `Unknown`, detail `"not probed"` |
| Completion report naming absent/unmeasured capabilities | **Implemented** | Generated only by arriving at `Finish` through `advance()` |
| Token-derived phone/tablet geometry and motion mapping | **Implemented** (pure C++20) | `mobile_shell_layout.hpp`; verified by a standalone assertion test |
| Qt host composing `FinnSurface`, `FinnLabel`, `FinnButton`, `FinnNavRail`, `FinnDivider`, `FinnProgressBar`, spring-driven step and sheet transitions | **Implemented** | `mobile_shell_layout.cpp`; creates widgets through `create_mobile_shell_host()` |
| Wiring into the `finn-shell` / `finn-oobe` binaries and their CMake targets | **Not done** | This worker owns no CMakeLists.txt; the factory is ready to be called by the owning module. The existing app has seven screens (hi, language, Wi-Fi, account, appearance, accessibility, completion); this model adds **privacy** and **region/locale** as first-class validated steps |
| Real Wi-Fi scan/connect, privileged account ops, settings writes **inside this model** | **Not done** | The existing `apps/oobe` already reads real networks through `finn::sys::FinnDesktopServices::network()`, calls `connectWifi()`, writes `~/.config/finn/account.conf` and runs `pkexec chpasswd` / `chfn`. This model accepts only measured outcomes (a real connect callback, a real account result) and refuses to claim them itself |
| Compositor Gaussian backdrop blur (35 px / 15 px) | **Not implemented** in v1 | `docs/design-language.md` §5.2; sheets use a real dim scrim and token fills instead |
| 3D interactive lamp on the theme step | **Implemented in `apps/oobe`, not embedded here** | `finn::oobe::OobeLamp3DWidget` (`apps/oobe/lamp_widget.hpp`) is already used at the app's appearance step; `OobeAppearancePreview` is a separate static preview. This host does not duplicate the lamp and does not claim to own it |
| Drag-to-dismiss on sheets, gesture back-swipe | **Not implemented** | Tap-outside and explicit Back are implemented; the gesture primitives (`GestureProgressTracker`, `rubber_band`) are available for a later pass |
| Phone/tablet frames in Figma | **Do not exist** | `docs/figma-audit.md` FA-B4; the geometry below is derived from tokens + the desktop OOBE frames, not invented screens |

---

## 2. Token values used (exact)

Source of truth: [`ui/_tokens.fui`](../ui/_tokens.fui) →
[`src/ui/generated_tokens.hpp`](../src/ui/generated_tokens.hpp). Only the
generated namespace values are referenced in code; the motion block is not
emitted by `tools/tokens.py`, so its numbers are transcribed with their token
names in `mobile_shell_layout.hpp` and repeated here for review.

### 2.1 Breakpoints (`finn::ui::breakpoints`) — dp

| Token | Value | Code use |
|---|---|---|
| `bp_phone_max` | `599` | Phone if `0 < width_dp <= 599` |
| `bp_tablet_min` | `600` | Tablet lower bound |
| `bp_tablet_max` | `1024` | Tablet if `width_dp <= 1024` |
| `bp_desktop_min` | `1025` | Desktop above 1024 |

`static_assert`s on all four values in `mobile_shell_layout.hpp` turn any token
drift into a compile error. `resolve_layout_class()` mirrors
`finn::shell::resolve_form_factor()` in `shell_layout.hpp`; the Qt
implementation includes that header so the two stay in one family.

### 2.2 Spacing (`finn::ui::spacing`) — px/dp, 4-grid

`sp_0 0`, `sp_half 2`, `sp_1 4`, `sp_2 8`, `sp_3 12`, `sp_4 16`, `sp_5 20`,
`sp_6 24`, `sp_8 32`, `sp_10 40`, `sp_12 48`, `sp_16 64`.

Used: page margins `sp_4` (phone) / `sp_6` (tablet) / `sp_8` (desktop); section
gap `sp_4` / `sp_5`; list gap `sp_3`; nav cluster gap `sp_5` (Figma's 20 px);
gesture-pill bottom inset `sp_2`; sheet handle inset `sp_2` and content inset
`sp_4`.

### 2.3 Radii (`finn::ui::radii`)

`r_xs 4.0`, `r_sm 6.0`, `r_md 8.0`, `r_lg 12.0`, `r_xl 20.0`, `r_nav 5.0`,
`r_bubble 10.4`, `r_panel 10.4`, `r_tile 5.37`, `r_note 10.4`, `r_card 10.4`,
`r_app 7.67`, `r_hero 39.0`, `r_pill 999`, `corner_smoothing 0.6`.

Figma option rows (`37:98`, `70:12`) measure `cornerRadius: 10` with
`cornerSmoothing: 0.6`; the code uses the generated `r_panel = 10.4` alias so
the row radius is not re-invented. Sheets use `r_xl = 20`, matching the
phone/tablet bottom-sheet rule in `docs/design-language.md` §10.

### 2.4 Motion (`ui/_tokens.fui` motion block)

| Token | Value | Where it drives this module |
|---|---|---|
| `surface_open` | `280ms out_back` | Forward step cross-travel; sheet entrance travel |
| `surface_close` | `180ms in_out` | Back step cross-travel; sheet dismissal |
| `scrim_in` | `200ms out` | Sheet scrim fade-in |
| `scrim_out` | `140ms in_out` | Sheet scrim fade-out |
| `hover_in` / `hover_out` | `110ms out` / `200ms out` | Exposed for widgets built by callers |
| `press_in` / `press_out` | `80ms out` / `180ms out` | Exposed for widgets built by callers |
| `fill` | `200ms out` | Progress fill |
| `theme` | `360ms in_out` | Appearance step transition |

`motion_spring()` maps each duration + curve family to the existing analytic
solver through `finn::ui::SpringParams::from_response(seconds, zeta)`:

| Token | T (s) | ζ | ω₀ = 2π/T | stiffness k | damping c | Rationale |
|---|---|---|---|---|---|---|
| `surface_open` | 0.280 | 0.62 | 22.440 | 503.551 | 27.826 | underdamped ≈ 8% overshoot ≈ `out_back`; one small land, no wobble |
| `surface_close` | 0.180 | 1.0 | 34.907 | 1218.470 | 69.813 | critical = `in_out`, no overshoot on exit |
| `scrim_in` | 0.200 | 0.90 | 31.416 | 986.960 | 56.549 | near-critical = `out`, fast arrival, gentle settle |
| `scrim_out` | 0.140 | 1.0 | 44.880 | 2014.205 | 89.760 | critical, shorter than entry |
| `theme` | 0.360 | 1.0 | 17.453 | 304.617 | 34.907 | slowest token, no overshoot for a whole-theme change |

`ShellSpringDriver` (from `shell_layout.hpp`) steps these springs on a timer,
carries velocity across retargeting, and settles instantly when
`finn::ui::reduced_motion()` is true — so no separate reduced-motion branch is
needed in the widget code. Reduced motion never plays a success animation for
a control that cannot act.

### 2.5 Colors (exact, for the painted sheet/scrim/handles)

`bone` `#FBFBFD`/`#0D0F14`; `tile` `#FFFFFF`/`#161922`;
`surface_canvas` `#F2F4F8`/`#111319`; `surface_input` `#F4F6FA`/`#1C202B`;
`ink` `#0F141A`/`#F2F4F8`; `dim` `#525866`/`#9EA6B5`;
`mute` `#868E9B`/`#8891A0`; `accent` `#007BFF` in both appearances;
`hair` `#14000000`/`#1CFFFFFF`; `danger` `#EF4444`/`#F87171`;
`success` `#16A34A`/`#22C55E`; `warning` `#D97706`/`#F59E0B`.
The sheet scrim is a measured `QColor(0,0,0, 120 × spring)`, not one of these
surface tokens — it is a dim, not a surface.

### 2.6 Type

Figma OOBE text styles: `text_w40jxa` = Fixel Display Regular **24 px** (step
prompt), `text_byvzlv` = Fixel Display Regular **18 px** (option rows),
`text_msf57u` = Fixel Display Regular **20 px** (wordmark). The `hi.` glyph in
frame `36:3` uses `Bumbbled` 36 px, a Figma-only face that does not ship; the
implemented intro uses `FinnRibbonAnimation` (`docs/motion.md`) rather than a
fake font. Code uses `finn_font_px(metrics.prompt_px)` and
`finn_font_px(metrics.row_label_px)` so Fixel Display and the Noto fallbacks
resolve through `libfinnui`.

---

## 3. Figma grounding (MCP reads, 19 Nov 2026)

`figwright ping` → connected to `FinnOS - Abhi`, plugin 0.7.0. The
**First Time Setup** page is `36:2`. Four full-size (1280×832) frames exist:

| Frame | Id | Measured facts carried into code |
|---|---|---|
| `hi` | `36:3` | Intro is one full-screen step; branding lockup at (589,180) is a 20×20 mark + 15 px gap + 68×14 wordmark; single Next circle at (613,631), 56×56, radius 100, 10 px inner padding, chevron vector 11×22 stroke 2.75 |
| `How do we talk?` | `36:53` | Back/Next cluster at (574,631): two 56 px circles with a **20 px** gap (→ `nav_cluster_gap = sp_5`); option list `37:96` at (805,195), **350×648**, vertical item gap 10 px, 14 rows |
| `A big world. But one country. Where?` | `70:3` | Same nav cluster; list `70:11` is 350×648; prompt is a 2-line 24 px statement at (225,348.5) |
| `Dark or Light? Your choice.` | `39:7` | Same nav cluster and 24 px prompt; the annotation's 3D lamp is implemented in `apps/oobe/lamp_widget.hpp`, outside this module (see §1) |

Progressive blur on `37:96`/`70:11`: LAYER_BLUR radius `20`, startOffset
`y = 0.607253`, endOffset `y = 1.0`, CSS bands `2.15 / 4.30 / 6.45 / 8.60 px`.
That is `blur_progressive = 20` in `_tokens.fui`; the existing CPU
implementation lives in `apps/oobe/main.cpp` (`box_blur_image`), which is why
this module does not duplicate it.

Option rows: 350×37, `cornerRadius 10`, `cornerSmoothing 0.6`, horizontal
padding 8, vertical 12, 18 px label at x=8.

**Motion audit:** `figwright get_motion_context({nodeId:"36:3"})` returned
`coverage.status = "complete"`, `visitedNodes = 12`, `animatedNodes = 0`.
There is no baked Figma motion to copy; the token timings in §2.4 are therefore
the authoritative motion source, consistent with `docs/motion.md` §Figma.

**Figma row gap vs the 4 dp grid:** the frames use 10 px; `_tokens.fui` and
`docs/design-language.md` require 4 px multiples (figma-audit FA-M6). The code
uses `sp_3 = 12` and documents the deliberate remediation rather than
silently mixing grids.

---

## 4. The flow model

### 4.1 Steps and gates

| # | Step id | Title | Gate to advance |
|---|---|---|---|
| 0 | `welcome` | `hi.` | none — informational |
| 1 | `connectivity` | `Where do we connect?` | `Connected` with a real manager capability and a named SSID, **or** a failed/unavailable/offline result with an explicit `continue_offline` acknowledgement |
| 2 | `account` | `What should we call you?` | profile name present **and** `Created` (requires measured account service) **or** `LocalOnly` |
| 3 | `privacy` | `How much should we remember?` | explicit `choice_made`; all-off is a valid choice |
| 4 | `region_locale` | `A big world. But one country. Where?` | non-empty locale/region from the supplied catalogue; timezone optional but checked when a catalogue exists |
| 5 | `accessibility` | `Need a hand?` | always passable; `Unset` means keep system defaults, screen-reader `On` requires a measured AT bus |
| 6 | `theme` | `Dark or light? Your choice.` | explicit `Light` or `Dark` |
| 7 | `finish` | `You're all set` | terminal; report generated on entry |

`advance()` validates the current step before moving, pushes history, and
finalises only on arrival at `Finish`. `back()` and `goTo(target)` only revisit
steps already in history; a forward jump is refused so no validation gate can
be skipped. Going back invalidates the completion report. All mutators return
`false` and leave state untouched when an input is inconsistent or claims an
unmeasured capability.

**Why `Auto` appearance is not offered:** v1 has no clock-driven appearance
service; the theme is re-read from settings. Offering `Auto` would require
guessing intent, so only `Light`/`Dark` exist. **Why "skip" is explicit:**
continuing offline and choosing a local profile are recorded as named states,
never as "connected"/"created".

### 4.2 Capability honesty

`CapabilitySnapshot` carries `network_manager`, `wifi_hardware`,
`wired_link`, `account_service`, `settings_write`, `accessibility_bus`. Each
`Capability` has `state ∈ {Unknown, Available, Unavailable}` and a detail
string; every one defaults to `Unknown` / `"not probed"`.

- `setConnectivity(Connected)` refuses a result unless `network_manager` is
  `Available` and the SSID is named.
- `setConnectivity(Unavailable)` refuses to record absence unless the absence
  was measured.
- `setAccount(Created)` refuses unless `account_service` is `Available`;
  `Failed` and `Unavailable` must carry a reason.
- `setAccessibility(screen_reader=On)` refuses without a measured
  accessibility bus.
- `setCapabilities()` withdraws any previously recorded `Connected`/`Created`
  success when its capability is no longer `Available`, and resets the screen
  reader choice. A stale success is not allowed to survive a hardware change.
- The completion report lists every capability that is `Unavailable` **or**
  `Unknown` with its reason, and states that the completion marker at
  `finn::kOobeCompleteRel` is written only by a real settings sink.

### 4.3 Reference patterns for the flow

- **iOS 16 / Apple Setup Assistant:** one question per screen, quiet
  typographic hierarchy, bottom Back/Next controls, Next disabled until the
  answer is valid. Adopted as the one-step-one-gate model and the circular
  nav cluster geometry from Figma.
- **Windows 11 OOBE:** network and account are optional but acknowledged
  ("continue without"); adopted as `continue_offline` and `LocalOnly` rather
  than a silent skip or a fake success.
- **Material 3 / Android setup:** tri-state accessibility ("keep default" is
  distinct from off) and least-privacy defaults with an explicit continue;
  adopted in `TriState` and `PrivacyState`.

---

## 5. Phone and tablet layout

### 5.1 Measured metrics (from `compute_layout`)

| Dimension | Phone `<600 dp` | Tablet `600–1024 dp` | Desktop `>=1025 dp` |
|---|---|---|---|
| Page margin | `sp_4` 16 | `sp_6` 24 | `sp_8` 32 |
| Section gap | `sp_4` 16 | `sp_5` 20 | `sp_5` 20 |
| Option row height | 48 | 44 | 37 (Figma measured) |
| Primary action / button | 48 | 40 | 56 (Figma circle) |
| Input height | 44 | 40 | 42 (Figma measured) |
| Bottom bar | 56 | 56 | 0 (dock owns nav) |
| Nav circles | 48 | 48 | 56 |
| Nav rail | 0 | 68 | 0 |
| Master pane / detail min | 0 / 0 | 320 / 320 | 0 / 0 |
| Sheet top radius | `r_xl` 20 | `r_xl` 20 | — |
| Gesture pill | 134×4, 8 from bottom | — | — |
| OOBE column | `min(350, width − 2·margin)` | same | 350 |

The 350 px column is Figma `37:96`; on a 360 dp phone it reflows to 328 dp
(360 − 2·16). Tablet adds the 68 dp rail and a 320 dp master/detail split.

*Relationship to the existing app:* `apps/oobe/main.cpp::oobe_form()` already
resolves Phone/Tablet/Desktop at the same 600/1024 dp boundaries and uses
16 / 24 / 80 dp page gutters plus 44 dp phone rows. This module takes the
breakpoints from the generated tokens (one copy of the fact) and uses the same
16/24 dp phone/tablet margins; its desktop branch is a token-based fallback,
not a replacement — the app keeps Figma's 80 px desktop page inset and owns
desktop OOBE layout.

### 5.2 Widget composition (`mobile_shell_layout.cpp`)

`create_mobile_shell_host()` returns a `MobileShellHostApi*` backed by a real
`QWidget` containing:

- `FinnSurface(Canvas)` background;
- a header with `FinnLabel` prompt (24 px), step counter and `FinnProgressBar`;
- a `QScrollArea` whose resizable host owns the current step's real widget;
- a bottom action bar with two `FinnButton`s — `setNavigationEnabled()` makes
  an unavailable action genuinely disabled (it refuses presses);
- `FinnNavRail` + `FinnDivider` shown only on tablet, fed by `setRailSteps()`;
- a token-coloured 134×4 gesture handle on phone;
- `SheetOverlay`: a dim scrim, an `r_xl` sheet drawn with
  `finn_rounded_path()` (smoothing 0.6), a 36×4 handle, and tap-outside close.

`animateStepContent()` snapshots the old and new step with `QWidget::grab()`
and cross-travels them under `motion_spring(SurfaceOpen)` or
`motion_spring(SurfaceClose)` in a `StepTransition` overlay. The overlay is
transparent to input, so a click during the transition still reaches the
widget underneath. Nothing animates in a loop.

### 5.3 Reference patterns for the form factors

- **iOS 16 home indicator and sheets:** the 134×4 gesture pill 8 dp from the
  bottom, and the 20 dp rounded sheet with a small drag handle; the host uses
  the same shape for the sheet handle at 36×4.
- **ColorOS 14:** large 44–48 dp touch targets, layered translucency and a
  progressive bottom blur on long lists (the blur itself lives in
  `apps/oobe`, not duplicated here).
- **HarmonyOS 2:** the 68 dp rail + 320 dp two-pane tablet split, with the
  phone collapsing to one pane at the 600 dp boundary.
- **Material 3 modal sheet:** a real dim scrim with `scrim_in`/`scrim_out`
  timings and tap-outside dismissal; no fake glass where a dim is intended.

---

## 6. Build and verification

Exact commands run in the worktree on macOS, with the host compiler
`/usr/bin/c++` (Apple clang):

```
$ c++ -std=c++20 -Wall -Wextra -Werror -I src -c src/shell/full_oobe_flow.cpp -o /tmp/full_oobe_flow.o
exit=0 (no diagnostics)

$ c++ -std=c++20 -Wall -Wextra -Werror -I src -c src/shell/mobile_shell_layout.cpp -o /tmp/mobile_shell_layout.o
In file included from src/shell/mobile_shell_layout.cpp:9:
src/shell/shell_layout.hpp:7:10: fatal error: 'QColor' file not found
    7 | #include <QColor>
      |          ^~~~~~~~
1 error generated.
exit=1
```

Qt6 is not installed on the host and `pkg-config` is not present
(`pkg-config --exists Qt6Widgets` → `command not found`). The Qt-side source is
kept correct against the APIs in `src/ui/finn/components.hpp`,
`src/ui/finn/window.hpp`, `src/ui/finn/theme.hpp` and
`src/shell/shell_layout.hpp`; it must be compiled in the VM, where Qt6 Widgets
and `FinnSDK` exist. No Qt dependency was introduced into the two headers, so
the model and the geometry remain host-testable.

Headless tests (temporary files outside the worktree, not committed):

```
$ c++ -std=c++20 -Wall -Wextra -Werror -I src /tmp/test_full_oobe_flow.cpp \
      src/shell/full_oobe_flow.cpp -o /tmp/test_full_oobe_flow && /tmp/test_full_oobe_flow
full_oobe_flow: all assertions passed

$ c++ -std=c++20 -Wall -Wextra -Werror -I src /tmp/test_mobile_layout.cpp \
      -o /tmp/test_mobile_layout && /tmp/test_mobile_layout
mobile_shell_layout: all assertions passed
```

The flow test covers: validation gates at every step; refusal of a connection
without a measured manager; refusal of account creation without a service;
withdrawal of a recorded success when a capability disappears; refusal of an
invented locale; refusal of a screen reader without an AT bus; back/revisit
semantics; forward-jump rejection; completion report contents; and reset. The
geometry test covers every breakpoint edge (599/600/1024/1025), token-derived
margins and radii, the 350→328 dp column reflow, and the exact
duration/damping mapping of the motion springs.

**Not verified on this host:** DRM/input behaviour, the composited sheet
appearance, and the Qt compile. Per `AGENTS.md`, a compositor change that only
compiles is not a working change; no such claim is made here.
