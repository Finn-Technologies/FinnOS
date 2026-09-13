# Agent Handoff: FinnOS Desktop Full Rewrite (Round 2, Screenshot-Driven)

- Objective: User rejected round-1 UI (low-res look, fake font, crude icons,
  banded wallpaper). Fully remake font/wallpaper/theme via a real visual
  loop: host harness renders the actual `Compositor::compose` path to BMP,
  screenshots viewed + iterated, then QEMU gates + a TRUE emulator
  framebuffer screendump as final proof.
- Starting commit/worktree: e4d4341 + round-1 libpeony changes (uncommitted,
  `userspace/` untracked pre-existing). Baseline round-1 screenshot showed:
  black wallpaper gap band, tofu box prompt glyphs, overlapping text,
  muddy shadows, duplicate Settings controls.
- Task state: Locally Verified (uncommitted worktree; NOT integrated).
- Skills used: finnos-operating-rules, repository-orientation, task-planning,
  test-strategy, graphics-architecture, peony-design-system,
  peony-toolkit-development, text-fonts-localization,
  compositor-window-system, qemu-boot-testing, ui-ux-review,
  documentation-maintenance, agent-handoff.
- Work completed:
  - Font pipeline (NEW): downloaded real Instrument Sans TTF (Google Fonts,
    OFL) + Pillow; `/tmp/peonywork/gen_font.py` rasterizes ASCII 32..126 +
    … • at 4x supersample + LANCZOS downsample to 16px TEXT (97 glyphs,
    3706 B blob) and 20px TITLE (97 glyphs, 5631 B blob), 4-bit alpha,
    bearings/advances; `/tmp/peonywork/assemble_font.py` writes
    `userspace/libpeony/src/font.rs` (969 lines, ~81 KB). Canvas
    `draw_char/draw_string` (16px baseline layout) + `draw_title_char` /
    `draw_title` (20px); `draw_string_max` ellipsis clip; `glyph_coverage`
    nibble decode. Legacy 8x16 bitmaps + fringe hack REMOVED.
  - Wallpaper rewrite: single-pass gapless painter (4-stop hazy ramp,
    quadratic warm glow, two low-contrast ridge darkenings, u32 hash grain
    dither). Old banded/gapped painter deleted. Shadows tightened to 2
    restrained layers.
  - Terminal: ASCII-only script (no box-drawing tofu), 20px pitch, 12-line
    fit, clipped tab label + status bar.
  - Settings: removed duplicate control row (chrome already has it), 20px
    Settings/Device Info titles, tightened search(30)/profile/nav(22h/26
    pitch) so all 5 nav items fit 300px windows, stacked label/value cards
    (card1 h100, card2 h86 — Security Controls now fits), ellipsis clips,
    short RAM label.
  - Files: measured columns (118/188/248), short statuses, ellipsis clips,
    adaptive breadcrumb width.
  - Popups: tinted gear glyph, brighter Start Menu discs with outlines,
    Control Centre action row now power/Wi-Fi/gear/Bluetooth (replaced
    uncarvable moon with Bluetooth rune).
  - Harness (session-only, NOT in repo): `/tmp/shot` (path-dep on libpeony)
    renders desktop/startmenu/control BMPs; viewed via PNG conversion.
    5 screenshot iterations to convergence.
- Files changed (repo): `userspace/libpeony/src/{font,canvas,widget,apps,compositor,lib}.rs`
  (see round-1 handoff for prior content; this round rewrites font.rs fully,
  reworks canvas text/wallpaper/shadow/icon-tint, apps terminal/settings/
  files/popups, lib exports). Session-only: `/tmp/shot/*`,
  `/tmp/peonywork/{gen_font,assemble_font}.py`, `shots/`, `qemu.*`
  (generator + TTF provenance documented in font.rs header).
- Tests/commands run (final, all green):
  - `cargo test --workspace` -> 8+10+181+26+15 = 240 passed, 0 failed.
  - `cargo clippy --workspace --all-targets -- -D warnings` -> clean.
  - `cargo fmt --all -- --check` -> clean.
  - `./tools/finn test-python` -> 85/85 OK.
  - `./tools/finn test-desktop` -> PASS, status 33.
  - `./tools/finn test-desktop --target arm64-qemu` -> PASS, status 0.
  - TRUE emulator proof: booted a COPY of the NORMAL image in QEMU q35
    (left the user's own QEMU window untouched), `screendump` via monitor
    socket -> `/tmp/peonywork/qemu.png`: real 1280x800 guest framebuffer
    shows the modern desktop (matches harness pixel-layout). QEMU quit
    afterwards.
- Results/evidence: Implemented-verified (QEMU x86_64+ARM64 smoke +
  framebuffer photo). Remaining Figma deltas (honest): procedural hazy
  wallpaper vs photographic rocks; single Regular weight (no Medium);
  420px test windows stack cards that Figma shows side-by-side at 750px
  (responsive behavior, correct).
- Docs/status: rustdoc updated; STATUS/ROADMAP/STATE untouched (percentages
  locked to maintainers).
- Unverified: manual mouse/drag feel in `./tools/finn run` (covered by
  drag/click unit tests + smoke render only).
- Blockers/risks: pre-existing flake `task::tests::saturating_counters…`
  (global INTERRUPT_DEPTH race under parallel load; passes alone/rerun;
  untouched by this change — no new globals added). Font tables are
  generated artifacts checked in as source; regen needs the two /tmp
  scripts + TTF (consider vendoring generator into tools/ later).
- Git state: HEAD e4d4341 main...origin/main; prior dirty files unchanged;
  userspace/ untracked (pre-existing); NOTHING committed/pushed.
- Next action: user visual review of qemu.png/desktop.png; optional
  follow-ups: vendor font generator into tools/, screenshot-regression test,
  Medium weight, photo wallpaper decoder.
- Next skills: finnos-operating-rules, repository-orientation,
  ui-ux-review, qemu-boot-testing, agent-handoff.
