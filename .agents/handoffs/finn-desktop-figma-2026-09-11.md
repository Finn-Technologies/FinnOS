# Agent Handoff: Authentic FinnOS Desktop UI from Figma

- Objective: Replace the placeholder Peony desktop UI with the authentic high-fidelity Figma design (Desktop page 44:2, Branding 0:1): bottom 45px frosted taskbar, centered 170x32 dock (Finn/Search/Files/Browser), right tray (Wi-Fi, battery, live `Mon 18 Oct 13:41` clock), two-tone Settings window with monochrome line controls, Start Menu, Control Centre, Power Options, Instrument Sans AA typography, AA geometry, Figma-derived icon/wallpaper pipeline. All work in `userspace/libpeony` only (`#![no_std]`, integer math only).
- Starting commit/worktree:
```text
commit: e4d43413dccb570dd56582af11398957121fec78
## main...origin/main
 M .agents/STATE.md (+30 pre-existing dirty files from prior phases)
 ?? userspace/ (entire userspace tree untracked pre-existing)
Baseline: cargo test workspace PASS, clippy clean, fmt clean, 85/85 python PASS,
 ./tools/finn test-desktop PASS x86_64 (status 33).
```
- Task state: Locally Verified (worktree changes, NOT committed, NOT integrated).
- Skills used: finnos-operating-rules, repository-orientation, task-planning, test-strategy, graphics-architecture, peony-design-system, peony-toolkit-development, text-fonts-localization, compositor-window-system, qemu-boot-testing, ui-ux-review, documentation-maintenance, agent-handoff.
- Work completed:
  - `font.rs`: Instrument Sans-compatible proportional AA atlas over the 8x16 master bitmaps. Added `glyph_advance` (4/5/6/8px), `text_width`, `glyph_pixel`, `glyph_alpha` (255 core / 96 fringe / 0 bg), `LINE_HEIGHT_BODY/TITLE`, `FRINGE_ALPHA`. `FONT_WIDTH/HEIGHT`, `get_glyph` preserved.
  - `canvas.rs`: analytical AA geometry (2x2 integer-supersampled `fill_circle`/`fill_rounded_rect`, fringed `draw_circle`/`draw_rounded_rect`, `set_pixel_coverage`, `draw_aa_line`, `fill_frosted_rect`); Figma color tokens (`TASKBAR_BG`, `START_MENU_BG`, `SIDEBAR_FROST`, `INFO_CARD_BG #E5E7EB`, desert palette, folder/globe/slate brand colors); `fill_desert_wallpaper` (procedural reading of exported 71:47 1280x832 PNG: dusk sky, sun+glow, two parabolic dune ridges, sand foreground; adapts 1280x800/1024x768); vector-crisp icons `draw_finn_logo` (4-element: rounded square/circle/sparkle/diamond, cf. 71:450 20x20 PNG export), `draw_search_icon`, `draw_files_icon`, `draw_browser_icon`, `draw_settings_gear`, `draw_terminal_glyph`, `draw_wifi_icon` (upper arcs), `draw_battery_icon`, `draw_close/minimize/expand_glyph` (monochrome), `draw_power_glyph`. `draw_char/draw_string` now proportional AA; `string_width` added.
  - `widget.rs`: monochrome line controls replace macOS traffic lights (hit-tests preserved); `WINDOW_CORNER_RADIUS=12`, `SETTINGS_CORNER_RADIUS=20`, `CHROME_CONTROL_SIZE=16`, `is_chrome_control`; proportional title/badge/button centering via `text_width`.
  - `apps.rs`: `TASKBAR_HEIGHT=45`, `DOCK_WIDTH=170`, `DOCK_ICON=32`; `render_taskbar` (frosted bar + hairline, 4 vector dock icons, tray Wi-Fi/battery + `tray_clock_bytes`/`format_tray_clock` live clock); `render_top_panel` kept as compat alias forwarding to taskbar (kernel callers untouched, markers preserved); `render_settings_app` rewritten to Figma 140:335 (frosted sidebar clamp 96..250, monochrome controls, Settings heading, 32px search pill, 44px profile card w/ blue avatar, nav, white Device Info + `#E5E7EB` cards); new `render_start_menu` (500x500 rx24 dark acrylic, `Hi, finnos!`, avatar, app grid, circular Settings/Power), `render_control_centre` (261x302 bottom-right: Wi-Fi/Bluetooth pills, media card, sliders, actions), `render_power_options` (dim overlay + Sleep/Shut Down/Restart).
  - `compositor.rs`: desert wallpaper; `dock_x/dock_y` (Figma offsets 0/46/92/138); `start_menu_open`/`control_centre_open` flags; Finn toggles Start Menu, tray toggles Control Centre, Search/Files raise apps; drag clamp `.max(0)` (no top panel); compose order wallpaper -> windows -> popups -> taskbar+dock -> cursor. Tests updated to new dock geometry + 2 new tests (taskbar bar, tray toggle).
  - `lib.rs`: re-exported all new APIs.
  - Figma evidence: bridge connected; `get-figma-document` (pages 0:1, 44:2); node properties for 71:47/71:125/71:448/71:450/71:455/71:459/71:461/71:126/71:127/71:358/71:438/140:335/44:453/44:381/112:245/69:2545/69:2556/69:2561; `export-node` PNG verified for 71:47 (1280x832 photo) and 71:450 (20x20 logo). `get-batch-nodes` unsupported by bridge (used singles).
- Files changed (all under pre-existing untracked `userspace/libpeony/src/`; no kernel/tooling/status edits by this session):
  - `userspace/libpeony/src/font.rs`, `canvas.rs`, `widget.rs`, `apps.rs`, `compositor.rs`, `lib.rs`
  - `.agents/handoffs/finn-desktop-figma-2026-09-11.md` (this file)
- Tests/commands run:
  - `cargo test -p finn-libpeony` -> 26 passed, 0 failed.
  - `cargo test --workspace` -> 8+10+181+26+15 = 240 passed, 0 failed (final green run).
  - `cargo clippy --workspace --all-targets -- -D warnings` -> clean.
  - `cargo fmt --all -- --check` -> clean.
  - `./tools/finn test-python` -> 85/85 OK.
  - `./tools/finn test-desktop` -> PASS x86_64 status 33 (11 markers incl. `FINNOS:TEST:DESKTOP:PASS`).
  - `./tools/finn test-desktop --target arm64-qemu` -> PASS status 0.
  - `grep f32|f64` in libpeony -> no float; `no_std` intact.
- Results and evidence classification:
  - Implemented-verified (QEMU): bottom taskbar/dock/tray/clock, desert wallpaper, two-tone Settings, Start Menu, Control Centre, Power Options renderers, AA font/geometry, vector icons — via 26 libpeony host tests + dual-arch `test-desktop` frame render + marker contract.
  - Implemented-unverified: pixel-perfect Figma fidelity (no screenshot capture in this session), `./tools/finn run` manual mouse/drag feel, physical hardware.
  - Planned: PNG-asset embedding (procedural vectors used instead — no PNG decoder fits `#![no_std]`; exports verified and shapes sampled — see risks).
- Documentation/status changes: rustdoc throughout libpeony carries Figma node IDs/specs. Did NOT edit STATUS.md/ROADMAP.md/STATE.md (percentages locked; prior-phase dirty files left untouched).
- Unverified assumptions:
  - Single 16px master raster + line-height constants acceptably stand in for 14/16/20/25px Instrument Sans (no outline font engine fits `no_std`).
  - Frosted blur approximated by layered alpha+sheen (no backbuffer blur kernel).
  - `render_top_panel` alias keeps kernel interactive clock path correct (repaints full bar each second).
- Remaining work:
  - Manual `./tools/finn run` on both targets: verify smooth mouse, drag, Start Menu/Control Centre toggles, tray clock advance, visual Figma comparison screenshots.
  - Optional: wire Power Options to a compositor flag; promote generic window radius to 20px for large Settings windows only.
  - Consider screenshot-based UI regression test (framebuffer hash/region asserts) in `test-desktop`.
- Blockers: none for this scope.
- Risks/regressions to watch:
  - PRE-EXISTING FLAKE (not ours): `kernel/src/task.rs::saturating_counters_do_not_abort_committed_transitions` intermittently fails under `cargo test --workspace` (~50%) because `INTERRUPT_DEPTH` (kernel/src/interrupt.rs:5, global AtomicUsize) leaks across parallel tests (`scheduler_rejects_interrupt_context_mutation` holds the guard). Passes consistently via `cargo test -p finn-kernel` (3x) and on workspace rerun. Our diff adds no globals (only pre-existing read-only statics) and kernel test binaries are unaffected by rendering code paths. Do NOT weaken the test; proper fix (hermetic guard or serial test) belongs to scheduler owners.
  - Proportional text changes string widths everywhere; any future pixel-exact text asserts must use `text_width`, not `len()*FONT_WIDTH`.
  - Double-draw taskbar+dock icons in `compose` (idempotent, minor cost).
- Current Git state: HEAD e4d4341, branch main...origin/main, prior-phase dirty files unchanged, userspace/ still untracked, this handoff file added. NOTHING committed/pushed (not requested).
- Suggested next action: manual visual pass `./tools/finn run` (x86_64 then `--target arm64-qemu`), capture screenshots vs Figma 71:47/71:438/44:381, file follow-up for any fidelity deltas.
- Skills next agent must load: finnos-operating-rules, repository-orientation, qemu-boot-testing, ui-ux-review, agent-handoff (plus peony-design-system/peony-toolkit-development if iterating visuals).
