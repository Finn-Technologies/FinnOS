# Agent Handoff: FinnOS Desktop — Continue Toward Modern-OS Fidelity

- Objective: Keep iterating the Peony desktop (now genuinely modern: real
  Instrument Sans, smooth wallpaper, vector icons, taskbar/dock/tray, Start
  Menu, Control Centre) until it rivals macOS/Windows 11/Linux shells in
  blind screenshots. Live guest interaction already proven; remaining work
  is fidelity depth + input robustness + perf.
- Starting point: HEAD e4d4341, `userspace/` untracked (pre-existing), all
  work in `userspace/libpeony/src/{font,canvas,widget,apps,compositor,lib}.rs`.
  Prior handoffs: `finn-desktop-figma-2026-09-11.md` (round 1),
  `finn-desktop-figma-round2-2026-09-11.md` (round 2, font pipeline),
  this turn (round 3: semibold, vignette, LIVE guest proof).
- Task state: Locally Verified, uncommitted, NOT integrated. Nothing was
  committed/pushed in any round (not requested).

## What is proven (evidence)

- Host visual loop (session-only, NOT in repo): `/tmp/shot` (cargo project,
  path-dep on libpeony) renders `Compositor::compose` exactly like
  `kernel/src/bin/{x86_64,aarch64}.rs::run_desktop_test` + startmenu/control
  variants to BMP; `PIL` converts to PNG for viewing. Shots in
  `/tmp/peonywork/shots/` (desktop/startmenu/control + zoom crops).
- Font pipeline (session-only scripts, provenance in font.rs header):
  `/tmp/peonywork/gen_font.py` (Pillow + `/tmp/peonywork/InstrumentSans.ttf`,
  Google Fonts OFL, 4x SS + LANCZOS → 16px TEXT 3706 B + 20px TITLE 5631 B
  4-bit blobs) + `/tmp/peonywork/assemble_font.py` (rewrites font.rs).
  Regen: run both scripts. Consider vendoring into `tools/` + TTF.
- LIVE guest proof (this turn): booted a COPY of
  `build/out/x86_64-qemu/finnos-x86_64-uefi.img` in q35/OVMF with
  `-monitor unix:/tmp/peonywork/qmon,server=on,wait=off -display none`,
  drove the pointer via HMP, `screendump` to PPM. PROVEN in the real guest:
  pointer motion, Finn-icon click → Start Menu opens, tray click → Control
  Centre opens, titlebar grab → window drags-relocates, clock ticks
  (13:42→13:50), guest stable throughout. Captures:
  `/tmp/peonywork/live-{base,click2,startmenu,drag,corner,toggleoff}.png`.
- Gates (re-verified this turn): `cargo test --workspace` 240 pass;
  `cargo clippy --workspace --all-targets -- -D warnings` clean;
  `cargo fmt --all -- --check` clean; `./tools/finn test-python` 85/85;
  `./tools/finn test-desktop` status 33; `--target arm64-qemu` status 0.

## Live-QEMU cookbook (copy-paste)

```bash
cp build/out/x86_64-qemu/finnos-x86_64-uefi.img /tmp/peonywork/esp-copy.img
cp build/out/x86_64-qemu/finnos-x86_64-data.img /tmp/peonywork/data-copy.img
# NEVER boot build/out images directly if another QEMU may hold them; also
# NEVER kill PID 33009 (user's own QEMU window, running since 21:33).
/opt/homebrew/bin/qemu-system-x86_64 -machine q35,vmport=off -m 256M \
 -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
 -drive if=ide,format=raw,file=/tmp/peonywork/esp-copy.img \
 -drive if=none,format=raw,file=/tmp/peonywork/data-copy.img,id=finnos-data \
 -device virtio-blk-pci,drive=finnos-data \
 -serial file:/tmp/peonywork/qserial.log \
 -monitor unix:/tmp/peonywork/qmon,server=on,wait=off \
 -display none -no-reboot -net none &
# wait for FINNOS:KERNEL:FIRST_BOOT_COMPLETE in qserial.log (~10 s)
```

HMP gotchas discovered the hard way:
- Run `mouse_set 2` first (`info mice` shows `Mouse #2: QEMU PS/2 Mouse`;
  without it, motion is silently ignored).
- `mouse_move dx dy` is RELATIVE (e.g. corner slam `mouse_move 2000 2000`
  clamps to (1279,799); Finn icon center = (571,777); tray ≈ (1200,777)).
- Open one monitor connection PER command (greeting handshake each time);
  sleeps: 1–2 s after button events (guest polls PS/2 in its idle loop).
- Rapid-fire HMP sessions can coalesce/lose press-release edges in the
  i8042 stream: symptoms are "click didn't fire" or "drag kept going".
  This is a RIG artifact, but it exposes a REAL robustness gap (below).
  `/tmp/peonywork/live.py` has base/finn/tray/drag helpers.

## Known gaps / backlog (priority order)

1. Stuck-drag watchdog (robustness, small): if a release byte is ever lost
   (KVM switches do this on real hardware too), `dragging_window` sticks
   and every motion drags a window until the next click. Add a guard in
   `Compositor` (e.g. drop the drag if N consecutive motions arrive with no
   button, or re-confirm button state on grab). Keep host tests green;
   add a unit test (press → motions → release-then-motion asserts static).
2. Compose perf: `update_mouse_position` recomposes the FULL frame per
   packet, including the ~1M-iteration wallpaper painter. Fine at smoke
   scale, likely laggy under real mouse load. Options: dirty-region
   compose (cursor layer only on pure moves), or a cached wallpaper
   (note: 1280x800x4 = 4 MB > 1 MiB kernel heap — needs a dedicated
   framebuffer-side region or cheaper wallpaper math). Measure first
   (tick counts around compose in the QEMU run).
3. Wallpaper richness: current hazy gradient + vignette reads clean but
   flat next to photographic macOS/Win11 walls. Ideas within `no_std`:
   layered translucent rock silhouettes (darker, sharper than current
   haze), subtle horizontal strata noise, a soft sun disc. Iterate via
   `/tmp/shot` screenshots; keep the gapless single-pass structure.
4. Icons: dock set is good at 32px; compare 2x crops
   (`shots/sm_top.png` method) against Figma Branding 69:25xx and refine
   Finn glyph geometry; consider 40px dock on 1280x800.
5. Type: single Regular + double-strike faux semibold. A real Medium
   weight (second variable-font instance → third blob ≈ +6 KB) would lift
   headings; generator already supports it (add size/instance).
6. Vendor the font toolchain: move scripts + TTF into `tools/` (or
   `userspace/libpeony/gen/`), document regen, so tables aren't
   write-only artifacts.
7. Screenshot regression: extend `test-desktop` (or a host test) with
   region hashes (e.g. taskbar strip, titlebar) to catch visual
   regressions without eyeballs.

## Pre-existing issues (NOT ours, do not "fix" silently)

- `kernel/src/task.rs::saturating_counters…` flakes under parallel
  `cargo test --workspace` (global `INTERRUPT_DEPTH` race with
  `scheduler_rejects_interrupt_context_mutation`); passes alone/rerun.
  Owned by scheduler phase.
- STATUS/ROADMAP/STATE percentages are maintainer-locked; rustdoc carries
  the Figma spec instead. Do not invent dates/percentages.

## Exact gate commands

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo fmt --all -- --check
./tools/finn test-python
./tools/finn test-desktop
./tools/finn test-desktop --target arm64-qemu
```

## Skills for the next agent

finnos-operating-rules, repository-orientation, task-planning,
test-strategy, graphics-architecture, peony-design-system,
peony-toolkit-development, text-fonts-localization,
compositor-window-system, qemu-boot-testing, ui-ux-review,
documentation-maintenance, agent-handoff.
