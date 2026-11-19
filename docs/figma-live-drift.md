# Figma ↔ live shell drift audit

A static audit of the FinnOS shell against the live Figma file
`FinnOS - Abhi`. Every Figma value below was read from the file through the
Figwright bridge on 2026-11-19 (`ping`, `get_pages`, `get_document`,
`get_node`, `get_screenshot`, `scan_text_nodes`); every code value was read
from the worktree at `/Users/abhi/.local/share/opencode/worktree/2ab435/full-os`.

**This is a code-and-design read, not a runtime capture.** No compositor or VM
screenshot was taken, so visual impact is inferred, not observed.

| Tag | Meaning |
|---|---|
| **[G]** | Figma-grounded: node id and value read from the live file. |
| **[C]** | Code-read: literal or expression at the exact `file:line` given. |
| **[I]** | Inference: impact, severity, or fix that is not itself a sourced fact. |

Figma page/node index used throughout **[G]**:

| Surface | Page | Nodes |
|---|---|---|
| Desktop / taskbar | `Desktop` (`2:283`) | frame `2:285`; taskbar `4:58`; pills `2:286` Time, `4:2` Hardware, `2:327` Home buttons, `2:315` Apps; app tile `2:345` |
| Home menu | `Desktop` (`2:283`) | frame `4:24`; menu `4:60`; footer `4:219`; favorites `4:196` / row `4:96`; categories `4:187` / tile `4:132`; divider `4:100` |
| Control Center | `Desktop` (`2:283`) | frame `11:19`; panel `11:216`; footer `17:42`; Wi‑Fi `11:250`; Bluetooth `11:258`; media `11:229`; sliders `17:30`, `17:24`; album art `11:266` |
| Spotlight | `Desktop` (`2:283`) | frame `4:226`; search pill `5:16`; recommendations `5:2` |
| Settings | `Settings App` (`20:63`) | window `20:98` (also `71:87`); sidebar `21:131`; search `21:132`; main content `21:119`; window options `20:106` |
| Notifications | `Notifications` (`2:146`) | component set `2:169`; light single `2:226`, dark single `2:170`; stack `2:178` with cards `2:226`/`2:237`/`2:246` |
| OOBE | `First Time Setup` (`36:2`) | `hi` `36:3` (Next `36:48`, branding `36:45`, "hi." `36:37`); language `36:53` (bar `36:70`); appearance `39:7` (bar `39:8`, copy `70:2`) |

The token file is `ui/_tokens.fui`; relevant lines: palette 15–52, spacing 58,
radii 64–78, type 85–93, material weights 102–103, blur/elevation 110–111,
motion 131–145.

---

## 1. Dock / taskbar

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| D1 | taskbar `4:58`, width | group `713` wide, composed `254 + 130 + 130 + 154` with three `15` gaps | root layout adds `16` px side margins → group spans `745` | `src/shell/dock.cpp:815-817`, widths `820-841`, computed `1043` | Set horizontal contents margins to `sp_0` (`ui/_tokens.fui:58`) and keep the `15` px inter-pill gap as a Figma-specific literal; keep stretch centring. | High |
| D2 | pills `2:286`/`4:2`/`2:327`/`2:315` fill | `#FFFFFF @ 70%`, `BACKGROUND_BLUR 35`, radius `12`, smoothing `0.6` | `shell_dock_fill` = `t.glass` at alpha `196` light (76.9%) / `210` dark (82.4%); radius `r_lg 12` ✓ | `src/shell/shell_layout.hpp:81-83`; `src/shell/dock.cpp:29-34,693` | Drive the alpha from `glass_panel 0.65` (`ui/_tokens.fui:102`) instead of a hardcoded 196/210; keep `r_lg` and `blur_glass`. | High |
| D3 | app tile `2:345` | `32×32`, radius `8`, solid `#AEAEAE`, `BACKGROUND_BLUR 13.8` | icon pixmap only, clipped `r_md 8`; no tile fill and no second blur | `src/shell/dock.cpp:55,160-177` | Paint the tile with `t.mute` (or `glass_tile` weight, `ui/_tokens.fui:102`) before the icon; keep `r_md`. | High |
| D4 | app/tile positions | tiles at y=`6`, gap `10`, 6 pins → `254` wide | `setFixedSize(32,38)` on the button; pill width formula `12 + n*32 + (n-1)*10` = 254 ✓ | `src/shell/dock.cpp:55,1043` | Keep width formula; make the button `32×32` and draw the running dot below, matching the Figma tile rect. | Low |
| D5 | hardware selected inset (Figma `11:204`) | `124×38`, radius `9` | `124×38`, radius literal `9` ✓ geometry, no token | `src/shell/dock.cpp:576-584` | No drift in value; add a comment citing `11:204`. If tokenised later, `r_bubble 10.4` is the nearest existing radius. | Low |
| D6 | time pill `2:286` | fixed `154×44` | `setFixedWidth(max(154, text_width+18))`; text font `16` px | `src/shell/dock.cpp:645-650,701-703` | Clamp desktop width to `154` (keep dynamic sizing only on phone/tablet); no 16 px `type` token exists — keep the literal with a node comment. | Medium |
| D7 | all pills | shadow `(0,4) blur 35 black 5%` | `paintLayerShadow(..., blur_glass, elev_popover)` with pad `14` | `src/shell/shell_layout.hpp:141,149-158,242-251`; `src/shell/dock.cpp:871-885` | **Aligned** — uses `blur_glass`, `elev_popover`. | — |

---

## 2. Home menu

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| H1 | panel `4:60` | `585×460`, radius `12`, `#FFFFFF @ 70%`, blur `35`; surface reserves shadow rim | visible card `585×460`, `r_lg`, blur via shadow pass; fill `shell_glass_fill` = glass at `220`/`228` (86%/89%) | `src/shell/popovers.cpp:280-299,546-568`; `src/shell/shell_layout.hpp:77-79` | Use `glass_panel 0.65` weight with `t.glass` for the panel fill; keep `r_lg`, `blur_glass`, `elev_popover`. | High |
| H2 | footer `4:219` | `444`-wide analogue here `585×50`, `#FFFFFF @ 40%`, shadow `(0,-1) blur 35 black 5%` | `paintGlassFooter` fills `shell_footer_fill` = `surface_canvas` at `242`/`236` (95%/93%) | `src/shell/popovers.cpp:96-105,795`; `src/shell/shell_layout.hpp:89-94` | Replace with `t.glass` at `glass_well 0.53` weight (`ui/_tokens.fui:102`) — closest existing weight; Figma is 0.40, so a documented literal may still be required. | High |
| H3 | section headers `4:109`, `4:69`, `4:369` | `16` px Regular | `popoverFont(14)` for "Your favorite apps", "All Apps", "Recent activity" | `src/shell/popovers.cpp:575,614,742` | No 16 px `type` token exists; either keep a Figma-commented `16`, or adopt `type: title 17` (`ui/_tokens.fui:88`) for all three. | Medium |
| H4 | favourite row `4:96` → text `4:98` | label `16` px at x=`42` relative to row | `popoverFont(13)` at `item_r.x() + 40` | `src/shell/popovers.cpp:599-605,726-733` | Move label to x=`42`; use `type: title 17` or a Figma-commented 16. | Medium |
| H5 | category tile `4:132` → `Folder` | `80×80`, radius `14`, white fill | `80×80` rounded `r_lg 12`, fill `shell_subcard_fill` (80%/86%) | `src/shell/popovers.cpp:634-638`; `src/shell/shell_layout.hpp:85-87` | No 14 px radius token: use literal `14` with node comment (or accept `r_lg 12`). Fill should be `tile_light`/`fill_light` opaque, not alpha-blended `tile`. | Medium |
| H6 | divider `4:100` | x=`293`, y=`96`, h=`100`, 1 px | x=`293`, y=`96`, h=`100`, `t.hair` | `src/shell/popovers.cpp:609` | **Aligned** (uses `hair`, `ui/_tokens.fui:18`). | — |
| H7 | favorites grid `4:196` | rows `130×32` at x=`15`,`155`, y=`46 + row*42` | `15 + col*140`, `46 + row*42`, `130×32` ✓ | `src/shell/popovers.cpp:398-400,588-589` | **Aligned.** | — |

---

## 3. Control Center

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| C1 | panel `11:216` | `444×300`, radius `12`, `#FFFFFF @ 70%`, blur `35` | paint draws `299` px tall card; layer content height is only `254` (`254 + 2*14` surface) | `src/shell/popovers.cpp:1573-1574,1593-1595,1811-1820` | Set the surface content height to `300` and paint `300`; keep `r_lg`, `blur_glass`, `elev_popover`. Current surface clips ~45 px of the designed card. | **Critical** |
| C2 | bottom bar `17:42` | y=`250`, h=`50`, `#FFFFFF @ 40%`, shadow `(0,-1) blur 35 black 5%` | footer painted at y=`204`, h=`50` | `src/shell/popovers.cpp:1978` | Move footer to y=`250`; same fill fix as H2 (`glass_well` on `t.glass`). | **Critical** |
| C3 | sub-tiles `11:250`, `11:258`, `11:229` | `211×42`, `211×42`, `211×94` at x=`6`/`227`; radius `6`; white `70%` | `half_w=(w-22)/2=211`, `col2_x=227`; radius `r_sm 6` ✓; fill `shell_subcard_fill` = tile at `204`/`220` (80%/86%) | `src/shell/popovers.cpp:1801-1803,1832-1835,1870-1873,1899-1901`; `src/shell/shell_layout.hpp:85-87` | Geometry/radius aligned; change fill to `glass_panel` weight with `t.glass`. | High |
| C4 | sliders `17:30`, `17:24` | `432×36` at (6,110) and (6,156); track `#000 @ 50%`; fill white with white `35%` glow blur `55` | geometry ✓; track `token_ink_alpha(0x80)` light / `0x99` dark; opaque fill, **no glow** | `src/shell/popovers.cpp:1576-1580`; `src/ui/finn/components.cpp:1094-1120` | Use existing `track_light #80000000` / `track_dark #3B4048` (`ui/_tokens.fui:31`) and `fill_light`/`fill_dark` (`ui/_tokens.fui:36`); the white glow has no token — add a Figma-commented local effect, do not invent a global shadow token. | High |
| C5 | album art `11:266` | `82×82`, radius `2`, `#CCCCCC` | `82×82`, radius `r_xs 4`, fill `shell_hover_fill` | `src/shell/popovers.cpp:1904-1908`; `ui/_tokens.fui:64` | No 2 px radius token; keep an explicit `2.0` with node comment or accept `r_xs`. Fill can use `t.mute` at `wash_hover` weight (`ui/_tokens.fui:149`). | Low |
| C6 | media progress (Figma `11:271`/`11:272`) | track black `20%`, fill black `100%`, stroke `4`, round caps, width `103` | track `t.ink @ 40`, fill `t.ink @ 175`, height `3`, radius `1.5` | `src/shell/popovers.cpp:1928-1935` | Use `track_light`/`ink` tokens with Figma-commented alpha `0.20`/`1.0` and height `4`. | Medium |
| C7 | footer icons `17:42` → `20:59` | `settings-03` at x=`407` | `settings-02` at `w-37` (=`407`) | `src/shell/popovers.cpp:1991-1992` | Swap the icon name to `settings-03`; geometry is aligned. | Low |
| C8 | Wi‑Fi/Bluetooth icon wells `11:254`, `11:262` | `32×32` circle, active `#007BFF` | `drawEllipse(11,11,32,32)` with `t.accent`/`t.track` | `src/shell/popovers.cpp:1839-1842,1877-1882`; `ui/_tokens.fui:29,31` | **Aligned** on active state; inactive `t.track` is translucent black in light mode whereas Figma uses solid grey — use `t.mute` for inactive. | Medium |

---

## 4. Spotlight / Search

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| S1 | search pill `5:16` | `300×44`, radius `100` (pill), white `70%`, blur `35` | painted `300×43`, radius `21.5`, fill `shell_glass_fill` 86%/89% | `src/shell/popovers.cpp:1296-1315` | Paint height `44` with `r_pill` (`ui/_tokens.fui:77`) and fill from `glass_panel`. | High |
| S2 | recommendations card `5:2` | `350×228`, radius `12`, white `70%`, blur `35` | painted `w×227` at y=`59.5`, radius `r_lg`, fill 86%/89% | `src/shell/popovers.cpp:1301-1323`; `src/shell/shell_layout.hpp:77-79` | Height `228` and `glass_panel` fill; keep `r_lg`. | High |
| S3 | placeholder `5:17` | `16` px, black `70%` | `configureBorderlessEdit(..., 16)` with `t.dim`/`t.ink` palette | `src/shell/popovers.cpp:961-964,107-118` | **Aligned on size**; placeholder colour uses `dim` (#525866) rather than Figma black@70 — acceptable token substitution. | — |

---

## 5. Settings (window chrome + sidebar)

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| G1 | window `20:98` | `800×500`, radius `12`, glass; **no 48 px title band** — window options float at y=`6` | `FinnWindow` inserts a `48` px full-width title bar above the body | `src/ui/finn/window.cpp:131-136,172-174` | For `ChromeMode::SplitSettings`, hide the title band and let `FinnWindowControls` float at (x≈706, y=6) inside the content; the existing `setChromeMode` split path (`window.cpp:250-268`) is the place. | High |
| G2 | frame radius | `12` (`20:98`) | `kRadiusXl = 12` used for frame path | `src/ui/finn/window.cpp:26,590,605`; `src/ui/finn/tokens.hpp:33` | **Aligned.** | — |
| G3 | window options `20:106` | `88×28`, radius `6`, white opaque, `#000 @5%` 0.5 px inside stroke | `88×28`, `kRadiusMd 6`, `t.tile` + `hair_soft` 0.5 px | `src/ui/finn/window.cpp:34,98-107`; `ui/_tokens.fui:21` | **Aligned** and already token-commented. | — |
| G4 | buttons `21:113`/`21:115`/`21:108` | `20×20` at x=`4`,`34`,`64`, radius `0` (bare glyphs) | `20×20` at x=`4`,`34`,`64`; `setCornerRadius(4.0)` on each | `src/ui/finn/window.cpp:36-58` | Radius is a hover-fill detail, not a Figma glyph rect; set to `r_xs 4` token or `0` to match glyph bounds. | Low |
| G5 | sidebar `21:131` | `250` wide, `12` inset, `5` gap, rows `36` high, radius `5`, text `14`; header `16`; search `226×30` pill | `250`, margins `12`, spacing `5`, `r_nav 5`, `finn_font_px(14)`; header `16`; search height `30` | `src/ui/finn/components.cpp:1562-1590,1619-1628,1471,1507-1509,1638-1643`; `ui/_tokens.fui:69` | **Aligned** — cite these lines as the reference implementation. | — |
| G6 | main card tiles `25:153` etc. | `258×70`, radius `6`, white | app-level `FinnStatCard` uses `r_sm`; outside the shell files in scope | Figma `21:119` children | Not audited here (app-level code outside the named files) — **[I]**, no drift claimed. | — |

---

## 6. Notifications

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| N1 | card `2:226`/`2:170` | `340×80`, radius `20` (`r_xl`), light `#FFF@70%`, dark `#414141@70%`, blur `15` | width max `340` ✓; min height `76`; radius `r_xl 20` ✓; fill `shell_glass_fill` = glass at 86%/89% with `glass_dark #161922` | `src/shell/notifications.cpp:175,267-275,336-342,350-357`; `src/shell/shell_layout.hpp:77-79`; `ui/_tokens.fui:40` | Set min height to `80`; use `glass_panel` weight with `t.glass`; for dark mode prefer Figma's `#414141` (currently `glass_dark #161922`) — no existing token holds that grey, so keep a Figma-commented literal if it matters. | High |
| N2 | title `2:183` / description `2:184` / time `2:180` | `16` / `14` / `14 @ 70%` | `15` / `13` / `12` | `src/shell/notifications.cpp:248-252` | Use `type: title 17` (or Figma-commented 16), `type: label 12` for description, and `t.mute` for time; no exact 14/16 tokens exist. | Medium |
| N3 | dismiss target `2:181` | `32×32`, radius `8` | `28×28`, radius `r_md 8` | `src/shell/notifications.cpp:101-105,148-149` | Make it `32×32`, keep `r_md`. | Medium |
| N4 | icon tile `2:177` | `40×40`, radius `10`, `#D9D9D9` | `40×40`, literal radius `10`, fill `shell_hover_fill` | `src/shell/notifications.cpp:58-95` | **Geometry aligned**; use `r_bubble 10.4` (`ui/_tokens.fui:70`) to remove the literal. | Low |
| N5 | stacked variants `2:178` | cards `340×80 r20`, `306×72 r18`, `275.4×64.8 r16.2`, offset behind | one card per row, equal width, `8` px vertical spacing; no recession/scale | `src/shell/notifications.cpp:302-307,327-340` | Feature drift: implement recession with `grid_step 22ms linear` + `grid_delay 60ms linear` (`ui/_tokens.fui:141-142`) and scale ratios `0.9/0.81` derived from Figma widths. | Medium |
| N6 | shadow | `blur 25`, offset `(0,4)`, black `5%`, backdrop blur `15` | `kNotificationShadowBlur 25`, `elev_notification 4`, alpha `13` (5%), `blur_notification 15` | `src/shell/shell_layout.hpp:141-144,242-251`; `src/shell/notifications.cpp:271-272` | **Aligned** and token-driven. | — |

---

## 7. OOBE / First Time Setup

These values live in `apps/oobe/main.cpp`, outside the primary shell files in
scope; they are included because the task requires the OOBE nodes to be
grounded. **[C]** references are still exact file:line reads.

| # | Node [G] | Figma value [G] | Current code [C] | file:line [C] | Recommended fix (existing tokens) | Sev. [I] |
|---|---|---|---|---|---|---|
| O1 | page background `36:3`, `39:7` | solid `#F5F5F5` | `p.fillRect(rect(), t.bone)` → `bone_light #FBFBFD` | `apps/oobe/main.cpp:1704`; `ui/_tokens.fui:15` | Use `surface_canvas_light #F2F4F8` (`ui/_tokens.fui:45`) or keep `bone` and accept a 6-level brightness difference. | Low |
| O2 | branding `36:45` | `103×20` | `112×24`, font `20` | `apps/oobe/main.cpp:198-204,228` | Resize lockup to `103×20`. | Medium |
| O3 | Next button `36:48` | `56×56`, radius `100` | `56×56` circular button | `apps/oobe/main.cpp:238-250` | **Aligned.** | — |
| O4 | shared bar `39:8`/`36:70` | `132×56` | `132×56` | `apps/oobe/main.cpp:1656-1659` | **Aligned.** | — |
| O5 | copy `70:2` | `18` px Regular | `oobe_font(18)` | `apps/oobe/main.cpp:507` | **Aligned.** | — |
| O6 | "hi." `36:37` | text `36` px | drawn as stroked vector paths (ribbon), not a font run | `apps/oobe/main.cpp:1415-1480` | No drift claim: different medium. Compare rendered screenshots if exact letterform matters. | — |

---

## 8. Motion drift (shell-wide)

| # | Behaviour | Figma / token target | Current code [C] | file:line [C] | Recommended fix | Sev. [I] |
|---|---|---|---|---|---|---|
| M1 | popover open | `motion: surface_open 280ms out_back` (`ui/_tokens.fui:131`) | `ShellSpringDriver(kSnappy)` plus a 10 px translate fade | `src/shell/popovers.cpp:268-272,305-321,908-920,1532-1544`; `src/shell/shell_layout.hpp:266-353` | Keep the spring for interactive gestures, but document the nearest token (`surface_open`) and cross-fade to a reduced-motion static state; no new token needed. | Medium |
| M2 | notifications | `motion: note 220ms out` (`ui/_tokens.fui:144`) | `ShellSpringDriver(kSnappy)` with 26 px/−4 px glide | `src/shell/notifications.cpp:179-195,263-265` | Same as M1; cite `note` as the declarative timing. | Low |
| M3 | dock hover/bounce | `hover_in 110ms out`, `press_in 80ms out` (`ui/_tokens.fui:135,137`) | `kSnappy`/`kBouncy` springs with `lift_y = −4.5`, `scale 1.12` | `src/shell/dock.cpp:60-74,151-157` | Springs are the implemented equivalent; document the mapping, do not add duplicate timing tokens. | Low |

---

## 9. Priority summary and caveats

1. **Fix C1/C2 first.** The Control Center surface is `254` px tall while its
   painter draws a `299` px card with a footer at y=`204`; Figma specifies
   `300`/`250`. This is the only row that looks like a clipping/layout defect
   rather than a styling delta **[I]**.
2. **Glass density is systematically too opaque.** Dock, Home, Spotlight,
   Control Center, sub-tiles, Settings window and notifications all use
   `shell_*_fill` helpers at 77–95% alpha, while Figma measures `70%` on glass
   surfaces and `40%` on footers **[G][C]**. The fix is to route these helpers
   through the existing `glass_panel`/`glass_well` weights
   (`ui/_tokens.fui:102`), not to add new colours.
3. **Type is consistently 1–3 px smaller than Figma** in Home rows (13 vs 16),
   notification text (15/13/12 vs 16/14/14) and Home section headers
   (14 vs 16). No exact 14/16 px `type` token exists; either use
   `type: title 17` / `type: label 12` or keep Figma-commented literals **[I]**.
4. **Aligned areas worth preserving:** dock pill sizes and app-tile radius,
   Control Center sub-tile/slider geometry, Home menu grid and divider,
   Settings sidebar (`r_nav 5`), window options pill, notification shadow,
   elevation/blur tokens, and OOBE navigation buttons **[G][C]**.
5. **Not verified:** I did not run `make`, `vm/deploy.sh`, or capture a live
   screenshot, so rows marked High/Critical are static-analysis findings; a
   compositor build or VM capture could reveal that a clipped region is
   covered by a different layer. No code was changed by this audit.
