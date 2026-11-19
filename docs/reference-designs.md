# Reference designs for FinnOS

This page is a research input for the "full OS completion" program. It records
**observable patterns** from six reference systems and converts the useful ones
into recommendations that fit the existing FinnOS design system.

It changes no code and makes no promise that a pattern is implemented here.
Where FinnOS does not have a feature, this page says so.

## How to read the evidence

| Tag | Meaning |
|---|---|
| **[S]** | Sourced: a claim traceable to a numbered source in [Sources](#sources). A direct vendor/HIG source is preferred; `[S-community]` marks a community measurement or teardown, not an official spec. |
| **[G]** | Grounded: measured in the FinnOS Figma file `FinnOS - Abhi` via the Figwright bridge (page/frame/node id given), or read from `ui/_tokens.fui`. This is FinnOS's own source of truth, not an aspiration. |
| **[I]** | Inference: my design recommendation. Not a factual claim about the reference OS, and not a statement that FinnOS implements it. |

Method note: the Figma file was read through `figwright.ping`, `get_pages`,
`get_document`, `get_screenshot`, and `scan_text_nodes` on 2026-11-19. The
Colors and Text Styles pages contain no frames; the palette and type scale
live in `ui/_tokens.fui`, which is the only token source ([G],
`docs/design/contract.md`). Screenshots of Figma frames `71:54`, `20:64`, and
`2:169` were inspected visually in addition to the node data.

---

## 0. The FinnOS baseline we are designing against

These are **[G]** facts, not references. Every recommendation below must land
on one of these surfaces or be rejected.

| Surface | Measured in Figma / tokens | Source |
|---|---|---|
| Desktop artboards | `1280×832`; taskbar group `713×44` centered at bottom | Frames `4:24` (Home), `11:19` (Control Center); nodes `4:59`, `11:20` |
| Glass chrome | Taskbar pills `154×44` (Time), `130×44` (Hardware Options), `130×44` (Home Buttons), `254×44` (Apps); radius `12`, `cornerSmoothing 0.6`, white at `70%` opacity, `BACKGROUND_BLUR radius 35` | Nodes `11:21`, `11:23`, `11:34`, `11:46`; `ui/_tokens.fui` `blur_glass 35`, `corner_smoothing 0.6` |
| Control Center | Panel `444×300`, radius `12`, blur `35`; bottom bar `444×50` white `40%` with `DROP_SHADOW radius 35, black 5%, offset (0,-1)` | Node `11:216`, `17:42` |
| Control Center controls | Pill sliders `432×36`, radius `100` (one is `mixed: 100/0/0/100`); sub-tiles `211×42` and `211×58`, radius `6`; tile title `14`, status `12`; media tile `211×94` radius `6`; album art `82×82` radius `2` | Nodes `17:24`, `17:30`, `11:250`, `11:258`, `11:229`, `11:266` |
| Settings | Window ≈`800×500` inside `1280×832` artboard; persistent left sidebar ≈`250` px; search field; colored circular nav icons; selected row is a filled gray pill; content header `16`, nav labels `14`, row labels and metadata `12`; traffic-light-style controls at top-right | Frames `71:54` (Sound), `20:64` (About); `scan_text_nodes` output |
| Notifications | Component set `740×241` with light and dark stacked-card variants; dark card ≈`#414141B2` | Node `2:169`; `ui/_tokens.fui` `glass_dark #161922`, comment citing `#414141B2` |
| Palette | `bone`, `tile`, `ink`, `mute`, `dim`, `accent #007BFF`, `glass`, `nav_sel`, `hero_tint`, semantic surfaces `danger/success/warning` — each light+dark | `ui/_tokens.fui` |
| Radii | `r_xs 4`, `r_sm 6`, `r_md 8`, `r_lg 12`, `r_xl 20`, `r_pill 999`, legacy aliases `r_nav 5`, `r_note 10.4`, `r_panel 10.4`, `corner_smoothing 0.6` | `ui/_tokens.fui` |
| Type | Fixel Display 400/500/600/700; `time 34/40`, `title 17/22`, `clock 18/22`, `label 12/16`, `label_sm 11/15`, `detail 10/14`, `micro 9/12` | `ui/_tokens.fui` |
| Motion | `surface_open 280ms out_back`, `surface_close 180ms in_out`, `scrim_in 200ms out`, `scrim_out 140ms in_out`, `hover_in 110ms out`, `press_in 80ms out`, `fill 200ms out`, `grid_cell 200ms out`, `grid_step 22ms linear`, `grid_delay 60ms linear`, `theme 360ms in_out` | `ui/_tokens.fui`; analytic springs `kSnappy/kSmooth/kBouncy/kOverscroll` in `src/ui/finn/spring.hpp` per `docs/architecture.md` |

**Open documentation discrepancy (not fixed here, outside this file's
ownership).** `docs/design/contract.md` says compositor backdrop blur is
implemented in `src/comp/backdrop_blur.cpp`; `docs/design-language.md` line 133
calls the same blur `Specified-Only`. `docs/architecture.md` line 145 lists "no
compositor GPU shader backdrop blur" as a limit. The owner of those pages
should reconcile them; this document does not assert which is true. [G]

---

## 1. macOS

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Window chrome | Three controls at top-left: close (red), minimize (yellow), zoom/full-screen (green). Glyphs appear on hover; green normally enters full screen, Option-click zooms. Key window uses color, inactive windows go gray. | [S] Apple *Windows* HIG [1]; [S-community] `namethatui` [2] |
| Window controls sizing | Gap between adjacent traffic lights described as "seven pixels" in a developer discussion — a useful order of magnitude, **not** an official spec. The commonly cited 12 pt diameter is **[I]**; I could not source it from Apple's HIG. | [S-community] Michael Tsai blog comment [3]; [I] for diameter |
| Settings windows | HIG guidance: dim minimize and zoom; only close stays enabled. Also support Escape / ⌘+. to close. Toolbar tabs should carry both icon and label so they survive truncation into a menu. | [S] Apple HIG-derived guidance [4] |
| Menu bar | Height **24 pt**. Prefer short one-word titles; title-style capitalization for multiword titles. Menu bar extras should use template (black-and-clear) symbols so the system recolors them for light/dark and selection. | [S] Apple *The menu bar* HIG [5] |
| Dock | Magnification expands the hovered icon and is user-tunable in Dock settings; double-click title bar defaults to zoom. Icon buttons in HIG guidance are 24–32 px with ≥8 px between button edges. **Dock tile size is not specified by the HIG**; do not assert one. | [S] Apple HIG Controls [6]; [S] Dock magnification guide [7] |
| Mission Control | No authoritative metric was retrieved. The transferable pattern is a spatial overview in which live window thumbnails scale down and regroup by space/display; exact scale factors are **[I]**. | [I] |
| SF typography | SF Pro is the system face on macOS. No Dynamic Type on macOS. Default **13 pt**, minimum **10 pt**. HIG tracking (1/1000 em): 11 pt +24, 12 pt +20, 13 pt +16, 15 pt +4, 16 pt 0, 17 pt −4, 20 pt 0. Split "Text" below 20 pt and "Display" at/above 20 pt. | [S] Apple *Typography* HIG [8]; [S] WWDC20 typography notes [9] |
| Vibrancy | `NSVisualEffectView` is a **background** view: translucency and blur come from a semantic `material` (`.sidebar`, `.popover`, `.menu`, `.hudWindow`); foreground content becomes vibrant only when it is in that view's hierarchy / opts into `allowsVibrancy`. `behindWindow` samples the desktop; `withinWindow` does not. Deprecated `.light` must not be used after 10.14. Avoid "glass on glass": put fills/transparency on top layers, not a second effect view. | [S] Apple `NSVisualEffectView` [10]; [S] AppKit vibrancy write-ups [11] [12]; [S-community] reverse engineering [13] |
| Settings hierarchy | Two panes: a fixed source list of categories and a content pane; a search field at the top of the source list; selection is a filled row, not a heavier font. | [S] [4]; [G] Figma `71:54` matches this shape |
| Motion | No sourced Apple durations for these surfaces were retrieved. Treat "quick, non-bouncy, ~200–300 ms" as **[I]** and do not attribute it to Apple. | [I] |

---

## 2. Windows 11

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Mica | **Opaque**, dynamic material that samples the desktop wallpaper **once**; tints long-lived windows (apps, Settings) and falls back to neutral when the window is inactive. Explicit fallbacks: High Contrast, transparency off, Battery Saver, low-end hardware, inactive window, Windows < 22000. | [S] Microsoft Learn, *Mica material* [14] |
| Mica layering | Two layers: Mica base + `LayerFillColorDefaultBrush` (low-opacity solid) content, as contiguous background or cards. Mica Alt adds a **commanding layer** (`LayerOnMicaBaseAltFillColorDefaultBrush`) between base and content — Microsoft's example is a tabbed title bar. Do not apply a backdrop material more than once or to a UI element. | [S] [14] |
| Acrylic | **Translucent** frosted glass, brighter in Windows 11; reserved for **transient, light-dismiss surfaces** — flyouts, context menus, search predictions. Mode aware (light/dark). | [S] Microsoft Learn *Materials* [15]; [S] Fluent 2 *Material* [16] |
| Material vocabulary | Fluent 2 defines four materials: **solid, mica, acrylic, smoke**. Solid/occluding materials are base layers; smoke is transparent and used sparingly for immersive surfaces. | [S] Fluent 2 [16] |
| Corners / shadows | Depth is expressed by layered surfaces and drop shadows. Official radius numbers were not retrieved from Microsoft docs; a widely used Windows 11 taskbar mod defaults to `GlobalRadius=8`, `TaskbarHeight=50`, `BottomGap=12`, `ItemMargin=4,2,4,2`, `TintOpacity=0.15`. These are **community mod defaults**, not a Microsoft spec. | [S] Fluent Design depth [17]; [S-community] FluentGlass taskbar guide [18]; [I] for treating 8 px as a general rule |
| Taskbar | Centered alignment is the default grouping in current Windows 11 customization; "show smaller taskbar buttons" now reduces icon **and** bar size. Widgets is a taskbar entry point. | [S-community] [18]; [S] taskbar customization coverage [19] [20] |
| Settings navigation | `NavigationView` comes in **Left** and **Top** modes; the content area can follow a **standard** (contiguous) or **card** (segmented) pattern. Mica should be visible in the title bar via an extended transparent custom title bar. Breadcrumb specifics are **[I]**. | [S] [14] |
| Widgets | A taskbar Widgets button opens a card feed; layout/pinning specifics were not sourced here and are **[I]**. | [S] [18] [20]; [I] for layout |

---

## 3. HarmonyOS 2

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Distributed UI | Atomic services run across "1+8+N" device classes; the same service can be delivered as an independent entry or embedded in another app, with cross-device migration/operation as first-class concepts. | [S] Huawei *Atomic Service Introduction* [21]; [S] Huawei glossary [22] |
| Atomic services | Installation-free, lightweight service forms with a card as the entry; a practical comparison puts an atomic service at **≤10 MB** versus **≥100 MB** for a traditional app. Service cards can be added to favorites or pinned to the home screen via Service Center. | [S] Huawei [21]; [S] implementation guide [23]; [S] HarmonyOS overview [24] |
| Control panel / Service Center | Quick controls and service cards are reached from a system-level panel; the sourced pattern is "cards + favorites/pinned", with Live View extending to widgets, capsules and immersive modes. Exact grid columns and panel height were **not** sourced and are **[I]**. | [S] [21] [24]; [S] HarmonyOS NEXT design [25]; [I] for geometry |
| Typography | HarmonyOS Sans is the default face. Font-size unit is `fp` (analogous to `sp`): default **16 fp** for `Text`, `TextInput`, `TextClock`, menus; **14 fp** for de-emphasized picker items; **20 vp Medium** for the selected picker item; accent `#ff007dff`. A 1.5× user font scale turns 16 fp into 24 vp. | [S] Huawei font/unit docs [26] [27] |
| Card grid | Cards are the atomic unit of both home screen and service discovery; ArkUI examples use 8 px padding inside a card and a 120 px-tall weather card. Treat those example values as illustrative, not a system grid spec. | [S] [23] |
| Visual language | HarmonyOS 2 Design System (2021, 13–24 months development) is built for a distributed OS. The described interface uses vivid system colour plus reflective "spatial" light/blur/glow, glassmorphism and soft-UI (neumorphic) shading — a middle ground between skeuomorphism and flat. | [S] iF Design award page [28]; [S] [24] |
| Multi-window | Floating windows and split-screen are first-class feature guides, with cross-device continuity as the differentiator. | [S] [25] |
| Motion | No numeric HarmonyOS 2 easing/duration table was retrieved. **[I]** only: favour short, soft, non-linear transitions for card-to-panel expansion. | [I] |

---

## 4. iOS 16

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Lock Screen customization | Two widget zones: **one** above the clock (shares space with a shrinking date; single line of text/controls), and up to **four** slots below it. Small widgets take one slot, large widgets two; zones are not interchangeable. | [S] TidBITS, *Customize Your Lock Screen in iOS 16* [29] |
| Depth Effect | A segmented foreground subject can overlap the clock, placing the clock in a middle layer and the subject in front. Adding widgets to the zone **below** the clock disables the effect and pushes the photo behind the widgets. A blur toggle is available. This is the concrete precedent for layered chrome. | [S] [29]; [S] OD2 layered-lock-screen write-up [30] |
| Focus | Four suggested modes (Do Not Disturb, Sleep, Personal, Work) plus Driving/Fitness/Gaming/etc.; a Focus can be linked to a specific Lock Screen or Home page. Options include *Show on Lock Screen* for silenced notifications, *Dim Lock Screen*, and *Hide Notification badges*. | [S] Computerworld iOS 16 Focus guide [31] |
| Notification stacking | Default Lock Screen format is a **stack**: newest notification on top, earlier ones receding behind it. Swipe down for a count, up for the full list; the default is configurable in Settings → Notifications. | [S] Macworld [32] |
| Control Center | No authoritative metric sheet retrieved for this release. **[I]** only: group by domain (connectivity, orientation, media, brightness/volume) with one large target per control and a scrim-dismiss gesture. | [I] |
| "Dynamic-island-like" layered chrome | The sourced mechanism is the multi-plane Lock Screen (wallpaper / clock / foreground subject), not a pill cutout. Live Activities provide ongoing status. Treating an iOS pill cutout as a required pattern is **[I]** for FinnOS — see synthesis. | [S] [30]; [I] |

---

## 5. ColorOS 14

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Aquamorphic design | Inspired by nature: adaptive colours, nature-derived sound sets (Starry Night, Cloud, Ripple, Sunshine, Crystal), innovative AOD and "lively animations". Aquamorphic Colour uses sea-through-the-day gradients that change gradually. | [S] OPPO ColorOS 14 site [33] |
| Aqua Dynamics | The system shifts between **bubbles, capsules and panels** to present activity status and notifications; the same activity "flows" to the shelf, Lock Screen and notification area. This is the explicit precedent for a small persistent activity surface that expands in place. | [S] [33]; [S] Forbes review [34] |
| Fluid motion | Described as "gently flowing river" — intuitive, free-flowing interactions. No numeric duration/easing published; **[I]** only. | [S] [33]; [I] |
| Smart Sidebar / File Dock | Sidebar is swiped in from the upper right edge; File Dock holds text/links/images with up to **99** items per drag batch and cross-device sharing when signed into the same account. A 30-day recall is reported for File Dock contents. | [S] OPPO [33]; [S] OPPO community [35]; [S] Forbes [34] |
| Large folders & card layout | "Large Folders", Shelf and home-screen widgets were introduced with ColorOS 13's Aquamorphic design, alongside a **card-styled layout** and a 1 Hz AOD path on LTPO 2.0. | [S] OPPO ColorOS 13 announcement [36] |
| Privacy | Picture Keeper supports selective photo/video sharing; **Auto Pixelate** pixelates names and avatars in screenshots with one tap. OS-level camera/microphone privacy *indicators* were not sourced for ColorOS 14 specifically — that claim is **[I]**. | [S] [33]; [I] for indicators |
| Performance claims | Trinity Engine; up to 21 GB reclaimed on a 256 GB model, up to 28 apps in background for 72 h — vendor lab figures, included only to show the "smoothness is a system property" framing. | [S] [33] |

---

## 6. iOS 17 animation patterns

| Area | Specific observable pattern | Tag / source |
|---|---|---|
| Spring model | Springs are configured with **duration + bounce** instead of mass/stiffness/damping; bounce range **−1.0 … 1.0**. Named presets: **smooth, snappy, bouncy**, each tunable by overriding duration or adding extra bounce. `Animation.default` is now a spring equivalent to `Animation.spring`. | [S] WWDC23 *Animate with springs* [37]; [S] WWDC23 notes [38]; [S] iOS 17 release notes [39] |
| Widget animation | Widgets have **implicit spring animation and implicit content transitions** between timeline entries; `.numericText()` and `.interpolate` are named content transitions. Max animation duration **2 s**. Concrete examples in Apple material: `.spring(duration: 0.2)` and `.smooth(duration: 1.8)` with `.push(from: .bottom)`. | [S] Apple *Animating data updates* [40]; [S] WWDC23 *Bring widgets to life* [41] |
| Interactive widgets | `Button` and `Toggle` inside widgets, backed by App Intents; the widget triggers a timeline reload. This turns a glass tile into an action surface rather than a display. | [S] [41]; [S] WidgetKit guide [42] |
| Gesture handoff | During gesture-change callbacks, velocity is tracked and applied to the animation that finishes when the gesture ends. That is the mechanism behind flick-to-open/dismiss feeling physical. | [S] [39] |
| StandBy | Full-screen, glanceable, viewed from a distance while the phone is on its side and charging; clocks, photos, widget Smart Stacks, Live Activities, Siri, calls and larger notifications. Note that the brightness slider is non-functional there (release note) — a useful honesty precedent. | [S] Apple Newsroom [43]; [S] [39] |
| NameDrop-style transitions | The sourced behaviour is proximity-triggered sharing: bring two devices together to exchange a contact card, with explicit *Share* / *Receive Only* choice and consent; same gesture can start SharePlay. The **visual transition itself** (card lift, two-device mirroring, settle) is **[I]** — no source with frame-level timing was retrieved. | [S] Apple Newsroom [43]; [S] Colorado DHSEM explainer [44]; [I] for visuals |

---

## 7. FinnOS synthesis

Concrete recommendations, each mapped onto an existing name in
`ui/_tokens.fui` or an existing component from `docs/architecture.md` and
`docs/design-language.md`. **Nothing here requires a new colour or a new
font family.** New names are flagged as proposals; the rest are reuses.

### 7.1 Material roles — map, don't multiply

| Role | Reference pattern | FinnOS decision |
|---|---|---|
| Long-lived window surface | macOS key/inactive differentiation [1]; Windows Mica base [14] | Use existing `bone_light/bone_dark` and `tile_light/tile_dark` as **opaque** surfaces for `FinnWindow`; keep the key/inactive distinction as ink strength (`ink`→`mute`) rather than a new colour. |
| Shell chrome (dock, Control Center, Home Menu) | macOS `.sidebar`/`.popover` vibrancy [10]; Acrylic for transient surfaces [15] | Keep `glass_panel 0.65` + `blur_glass 35` and radius `r_lg 12` with `corner_smoothing 0.6` [G]. Do **not** add a second blur layer inside these panels — "no glass on glass" [13]. |
| Notification card | iOS stack [32]; Figma dark card ≈`#414141B2` [G] | Keep `glass_dark`, `blur_notification 15`, `r_note 10.4`. Stack earlier notifications with the existing `grid_step 22ms linear` / `grid_delay 60ms linear` pair — no new timing token needed. |
| Transient menu / flyout | Acrylic is for light-dismiss surfaces [15] | Use `glass_tile 0.85` (already the 2-pass density) and `elev_popover 4`; avoid applying Mica-like opacity here. |
| Accent | Finn brand blue, same in both appearances [G] | Keep `accent #007BFF` / `on_dark #0A84FF`. Note it is close to HarmonyOS's `#ff007dff` selected accent [27] but is a brand fact, not a borrowed colour. |

### 7.2 Tokens to add or keep

All spellings below are proposed additions to `ui/_tokens.fui`; regenerate
`src/ui/generated_tokens.hpp` and run `make lint` if adopted. [I]

| Proposal | Value | Why it fits |
|---|---|---|
| `motion: spring_hero 320ms out_back` | proposal | Maps macOS/iOS panel arrival onto the already-shipped `surface_open 280ms out_back` and `springs::kSnappy`; 320 ms only if a larger surface needs it. Prefer reusing `surface_open` unchanged. |
| `motion: widget_entry 200ms out` | proposal = `grid_cell` | iOS widget entry transitions are implicit springs capped at 2 s [40]; FinnOS's `grid_cell 200ms out` is the same perceptual job. Reuse, do not add. |
| `motion: activity_expand 280ms out_back` | proposal = `surface_open` | Aqua Dynamics bubbles→capsules→panels [33] maps to one expand/collapse pair: `surface_open` / `surface_close 180ms in_out`. Reuse. |
| `motion: velocity_handoff 1` | proposal | iOS 17 tracks gesture velocity into the finishing animation [39]. Implement as a flag in `SpringAnimator`/`GestureProgressTracker`, not as a colour or duration token. |
| `weight: stack_recede 0.85` | proposal | Each older notification in a stack sits at slightly reduced opacity/scale [32]; express as a scalar on `glass_dark`, not a new palette entry. |
| `type: lock_time 34/40` | reuse `time` | A Lock Screen clock is already covered by `time 34/40`. Do not add a second clock face. |
| `radius: pill` | reuse `r_pill 999` | Figma Control Center sliders are radius `100` on a `36` px-tall track [G] = a pill. `r_pill` already expresses this. |

**Radii verdict:** the existing scale already covers every measured reference
surface. `r_sm 6` → Control Center sub-tiles; `r_lg 12` → panels and window
controls; `r_pill` → sliders/action pills; `r_note 10.4` → notifications. No
new radius token is justified. [I]

### 7.3 Components

1. **`FinnWindow` chrome.** Keep the Figma window-control pill (`130×44`,
   radius `12`, blur `35`) [G]. For Settings specifically, follow the macOS
   rule and draw minimize/zoom disabled while close stays live [4]; the
   compositor/Shell already distinguishes windows by focus, so the inactive
   state can be ink→mute. A traffic-light variant with a 12 pt dot and ~7 px
   gap is **[I]** and should stay behind a token/theme flag — the 7 px figure
   is community-sourced [3] and the 12 pt is my inference.
2. **Control Center.** The Figma geometry is already the right target:
   `444×300` panel, `50` px bottom bar, two columns of `211`-wide sub-tiles
   with a measured ~`10` px gutter (6 → 217, 227 → 438), `432×36` pill
   sliders, `211×94` media tile [G]. HarmonyOS's pinned service cards [21]
   and Aqua Dynamics capsules [33] suggest a **compact capsule row** above
   the tiles for one live activity; this is **[I]** and must read real state
   only.
3. **Settings.** Keep the persistent `250 dp` sidebar at ≥840 dp, the `68 dp`
   nav rail at 600–839 dp, and a bottom sheet below 600 dp
   (`docs/design-language.md` lines 217–239) [G]. Type: header `16`, nav `14`,
   row `12` [G] — note this is larger than Apple's 13 pt macOS body default
   [8], which is appropriate for a 4 px-based, touch-friendly system, not a
   defect.
4. **Notifications.** Implement the iOS 16 stack with existing tokens: top
   card full opacity, each lower card offset by `grid_step 22ms`-paced motion
   and `grid_delay 60ms` stagger, `stack_recede` (proposed) for opacity.
   Swipe-down → count, swipe-up → list [32] is a useful gesture contract.
5. **Lock screen.** FinnOS already has a real `LockScreenWindow` with an
   opaque cover-fitted wallpaper, clock and password field
   (`docs/architecture.md` §The lock screen) [G]. iOS Depth Effect [29] [30]
   suggests an optional foreground layer **only when a suitable layered
   wallpaper actually exists**; otherwise keep the current opaque cover. A
   Dynamic-Island-style cutout is **[I]** and should not be faked: there is no
   camera cutout abstraction or Live Activity source in this tree.
6. **Privacy indicators.** Only show camera/microphone/location indicators
   from real device state (PipeWire, V4L2, BlueZ/NetworkManager as available)
   — never synthesise them. With no backend, render the row absent or
   disabled, per the honesty rule in `docs/design/contract.md`. Auto
   Pixelate [33] is a plausible future action, not an existing capability.
7. **StandBy-like mode.** **[I]**: a charge/sideways ambient mode could reuse
   `LockScreenWindow` plus `time 34/40` and widget cards, but it needs a real
   "charging + on side" signal; Apple's own release note shows even a
   brightness slider is honestly disabled there [39]. Do not build the UI
   before the signal exists.

### 7.4 Motion budget (existing tokens first)

| Trigger | Token(s) | Reference rationale |
|---|---|---|
| Popover / Control Center open | `surface_open 280ms out_back` | Panel expansion, iOS spring arrival [37] |
| Popover / Control Center close | `surface_close 180ms in_out` | Faster exit than entry is already a FinnOS rule |
| Scrim behind popover | `scrim_in 200ms out` / `scrim_out 140ms in_out` | light-dismiss surfaces [15] |
| Widget / card value change | `grid_cell 200ms out` + `fill 200ms out` | implicit widget transitions [40] |
| Notification stack step | `grid_step 22ms linear`, `grid_delay 60ms linear` | stacked receding cards [32] |
| Toggle and pill fill | `press_in 80ms out`, `fill 200ms out` | fast pointer answer, quicker than withdrawal |
| Theme switch | `theme 360ms in_out` | keep; Aquamorphic colour drift [33] is a longer, separate ambient effect |
| Drag / flick release | `springs::kSnappy` with velocity handoff | iOS 17 velocity tracking [39] |

### 7.5 Explicit non-recommendations

- Do not copy Apple's 24 pt menu bar into the `44` px Figma taskbar: the
  FinnOS taskbar is a touch target with a 4 px spacing system [S] [5] [G].
- Do not add a `shadow_*` global elevation token: Figma's Control Center
  `(0,-1) blur 35, black 5%` shadow is deliberately local, as
  `ui/_tokens.fui` documents [G].
- Do not put app ids in `src/comp` to support any of this; `make lint`
  enforces the boundary (`docs/design/contract.md`).
- Do not describe any of §7 as shipped. Items marked **[I]** are proposals;
  items marked **[G]** are measured; items marked **[S]** describe other
  operating systems.

---

## Sources

1. Apple, *Windows* — https://developer.apple.com/design/human-interface-guidelines/windows
2. NameThatUI, *Traffic Lights (Window Controls)* — https://namethatui.com/macos/traffic-lights
3. Michael Tsai, *Mac Toolbar Guidelines* (reader comment on inter-button pixels) — https://mjtsai.com/blog/2025/01/13/mac-toolbar-guidelines
4. Zenn/usagimaru, *macOS Settings Window Guidelines* (HIG-derived) — https://zenn.dev/usagimaru/articles/b2a328775124ef?locale=en
5. Apple, *The menu bar* — https://developer.apple.com/design/human-interface-guidelines/the-menu-bar
6. Apple HIG, *Controls* — https://leopard-adc.pepas.com/documentation/UserExperience/Conceptual/AppleHIGuidelines/XHIGControls/XHIGControls.html
7. MacSales, *Mastering the macOS Dock* — https://eshop.macsales.com/blog/39477-tech-tips-mastering-the-macos-dock-part-1
8. Apple, *Typography* — https://developer.apple.com/design/human-interface-guidelines/typography
9. WWDC20 notes, *The details of UI typography* — https://wwdcnotes.com/documentation/wwdc20-10175-the-details-of-ui-typography
10. Apple, `NSVisualEffectView` — https://developer.apple.com/documentation/appkit/nsvisualeffectview
11. Philip Blank, *Vibrancy, NSAppearance, and Visual Effects* — https://philz.blog/vibrancy-nsappearance-and-visual-effects-in-modern-appkit-apps
12. MACKuba, *Dark Side of the Mac: Appearance & Materials* — https://mackuba.eu/2018/07/04/dark-side-mac-1
13. Oskar Groth, *Reverse Engineering NSVisualEffectView* — https://oskargroth.com/blog/reverse-engineering-nsvisualeffectview
14. Microsoft Learn, *Mica material* — https://learn.microsoft.com/en-us/windows/apps/design/style/mica
15. Microsoft Learn, *Materials used in Windows apps* — https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/materials
16. Fluent 2 Design System, *Material* — https://fluent2.microsoft.design/material
17. Wikipedia, *Fluent Design System* — https://en.wikipedia.org/wiki/Fluent_Design_System
18. Windhawk, *windows-11-taskbar-styling-guide / FluentGlass README* — https://github.com/ramensoftware/windows-11-taskbar-styling-guide/blob/main/Themes/FluentGlass/README.md
19. Windows Central, *Windows 11 brings back taskbar positioning and resizing* — https://www.windowscentral.com/microsoft/windows-11/windows-11-brings-back-taskbar-positioning-and-resizing-but-its-not-the-same-as-on-windows-10
20. PCMag, *Microsoft Finally Backs Down: The Windows 11 Taskbar Update* — https://www.pcmag.com/news/microsoft-finally-backs-down-the-windows-11-taskbar-update-youve-been-waiting
21. Huawei, *Atomic Service Introduction* — https://developer.huawei.com/consumer/en/doc/atomic-guides-V5/atomic-service-definition-V5
22. Huawei, *HarmonyOS Glossary* — https://developer.huawei.com/consumer/en/doc/harmonyos-guides/glossary
23. Medium, *Comprehensive Guide to Developing HarmonyOS Atomic Services* — https://medium.com/@zhengwei7747/comprehensive-guide-to-developing-harmonyos-atomic-services-practical-implementation-bf41dbbf8148
24. Wikipedia, *HarmonyOS* — https://en.wikipedia.org/wiki/HarmonyOS
25. Huawei, *HarmonyOS NEXT Design* — https://developer.huawei.com/consumer/en/design
26. Huawei developer blog, *font size / fp unit* — https://developer.huawei.com/consumer/cn/blog/topic/03203549674797020
27. Huawei, *DatePicker* (default `14fp`/`16fp`/`20vp` text styles, `#ff007dff` accent) — https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V5/ts-basic-components-datepicker-V5
28. iF Design, *HarmonyOS 2 Design System* — https://ifdesign.com/en/winner-ranking/project/harmonyos-2-design-system/351330
29. TidBITS, *How to Customize Your Lock Screen in iOS 16* — https://tidbits.com/2022/09/21/how-to-customize-your-lock-screen-in-ios-16
30. OD2 Engineering, *Create iOS Depth Effect Wallpaper* — https://od2.in/blog/product-demos/how-to-create-ios-depth-effect-wallpapers-lock-screen
31. Computerworld, *How to set up and use Focus modes on iOS 16* — https://www.computerworld.com/article/1612090/how-to-set-up-and-use-focus-modes-on-ios-16.html
32. Macworld, *How to customize an iPhone Lock Screen with widgets and wallpaper styles* — https://www.macworld.com/article/786981/how-to-customize-lock-screen-ios-16.html
33. OPPO, *Discover ColorOS 14* — https://www.oppo.com/en/coloros14
34. Forbes, *Three Essential Changes In Oppo's New ColorOS 14* — https://www.forbes.com/sites/ewanspence/2023/11/29/oppo-coloros-14-review-ui-ai-aqua-dynamic-trinity-engine-auto-pixelate
35. OPPO Community, *Interactive, Intuitive and User Friendly – ColorOS 14* — https://community.oppo.com/thread/1573535275009703945
36. PR Newswire, *OPPO Launches ColorOS 13* (Large Folders, Shelf, card-styled layout, 1 Hz AOD) — https://www.prnewswire.com/news-releases/oppo-launches-coloros-13-global-version-featuring-brand-new-aquamorphic-design-and-intelligent-experiences-based-on-oppos-latest-innovative-technology-301609068.html
37. Apple, WWDC23 *Animate with springs* — https://developer.apple.com/videos/play/wwdc2023/10158
38. WWDC23 notes, *Animate with springs* — https://wwdcnotes.com/documentation/wwdc23-10158-animate-with-springs
39. Apple, *iOS & iPadOS 17 Release Notes* — https://developer.apple.com/documentation/ios-ipados-release-notes/ios-ipados-17-release-notes
40. Apple, *Animating data updates in widgets and Live Activities* — https://developer.apple.com/documentation/widgetkit/animating-data-updates-in-widgets-and-live-activities
41. Apple, WWDC23 *Bring widgets to life* — https://developer.apple.com/videos/play/wwdc2023/10028
42. Medium, *WidgetKit: Build Interactive Widgets for iOS 17* — https://medium.com/@chandra.welim/widgetkit-build-interactive-widgets-for-ios-17-75e00a8eede8
43. Apple Newsroom, *iOS 17 makes iPhone more personal and intuitive* — https://www.apple.com/newsroom/2023/06/ios-17-makes-iphone-more-personal-and-intuitive
44. Colorado DHSEM, *iOS 17 Feature "NameDrop" Feature Facts* — https://dhsem.colorado.gov/blog-post/ios-17-feature-namedrop-feature-facts

Local grounding (not web sources): `ui/_tokens.fui`,
`docs/design/contract.md`, `docs/architecture.md`, `docs/design-language.md`,
and the Figma file `FinnOS - Abhi` node ids cited in §0. All web sources were
consulted on 2026-11-19.
