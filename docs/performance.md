# Performance: measured changes, not guesses

This page records the hot paths that were identified and changed in the
shell, how they were measured, and what is still unverified. The host is an
editor machine, not the build machine (`AGENTS.md`): Qt6 and wlroots cannot be
built here, so a claim about a full shell frame is **not** made on host
evidence. Every number below says what it actually measures.

## 1. Layer-surface shadow blur (redraw / first popover paint)

`src/shell/shell_layout.hpp::layer_shadow_pixmap` rasterises the elevation
shadow once per distinct (size, corner, blur) key and caches the pixmap. The
old `box_blur_argb` cost **O(w·h·r)** per pass: for each output pixel it
rescanned up to `2r+1` neighbours, and its vertical pass used `QImage::pixel()`
per neighbour. For a Control Center shadow, `blur_glass = 35` is reduced to
`radius = 35/3 = 11`, the padded mask is `472×328`, and three separable pass
pairs meant roughly **21 million neighbour reads** before the first glass
popover could paint.

The fix moves the pixel work into a Qt-free kernel,
`src/shell/blur_kernel.hpp::box_blur_argb_u32`, which keeps sliding-window
channel sums as the window advances one pixel at a time. Each pass is now
**O(w·h)**; the six passes are ~0.9M operations for the same mask. The kernel
is bit-for-bit identical to the neighbour-scanning algorithm (integer sums
and integer division per channel, three passes), so no visual value was
traded for speed. `box_blur_argb` copies in/out of contiguous buffers because
`QImage` rows can be padded.

The cache key also changed from a `QByteArray` assembled through `QDataStream`
to a small typed `ShadowCacheKey` with a `qHash` overload — no stream is built
on a cache lookup.

### Measurement

`make bench` builds and runs `bench/blur_bench.cpp` on the host without Qt. It
includes the **same kernel the product uses** and times it against a faithful
neighbour-scanning implementation of the old algorithm on identical random
`0xAARRGGBB` buffers, best of five runs. Representative output:

```
compiler: clang Apple LLVM 21.0.0 (clang-2100.4.1.1)
measurement: pixel kernel only, best of 5 runs, packed 0xAARRGGBB
ControlCenter 444x300, blur_glass 35         r=11  old=  13.579 ms  new=   3.866 ms  speedup=  3.51x  exact=yes
HomeMenu 585x420, blur_glass 35              r=11  old=  24.393 ms  new=   6.909 ms  speedup=  3.53x  exact=yes
Notification 380x96, blur 25                 r= 8  old=   2.898 ms  new=   1.138 ms  speedup=  2.55x  exact=yes
Dock pill 220x56, blur_glass 35              r=11  old=   1.441 ms  new=   0.497 ms  speedup=  2.90x  exact=yes
result: kernels agree bit-for-bit
```

The shared host varies by roughly 20–30% between runs; the stable result is
the **2.5–3.6× kernel speedup** and the bit-exact comparison, not any single
millisecond value. On the aarch64 VM (`gcc 16.1.1`, same source) the same
benchmark produced a smaller but still real gain — `1.14–1.37x`, bit-exact:

```
compiler: gcc 16.1.1 20260430
ControlCenter 444x300, blur_glass 35         r=11  old=  23.370 ms  new=  19.323 ms  speedup=  1.21x  exact=yes
HomeMenu 585x420, blur_glass 35              r=11  old=  53.761 ms  new=  43.655 ms  speedup=  1.23x  exact=yes
Notification 380x96, blur 25                 r= 8  old=   5.054 ms  new=   4.452 ms  speedup=  1.14x  exact=yes
Dock pill 220x56, blur_glass 35              r=11  old=   2.666 ms  new=   1.950 ms  speedup=  1.37x  exact=yes
result: kernels agree bit-for-bit
```

The difference between the two machines is a compiler/CPU result, not a
change in the algorithm. The old baseline here does *not* include the
`QImage::pixel()` call overhead of the shipped vertical pass, so the real
before/after delta in the running shell is expected to be larger — that
expectation is not reported as a measurement.

**Scope:** this times the pixel kernel only. QImage row copies, `QPainter`
rounded-rect mask generation and compositor presentation are unchanged.
End-to-end first-popover latency still has to be measured on the VM.

## 2. Registry rebuilds during app install (startup / redraw)

`finn-shell` watches `~/.local/share/applications` and the other XDG
`.desktop` directories. One package installation writes several files in
quick succession; each `directoryChanged` previously triggered a full
`AppRegistry::load_all`, `HomeMenuPopover::refreshApps()` and
`DockWindow::rebuildAppsPill()` synchronously — repeated work and repeated
dock redraws during a single install.

`src/shell/main.cpp` now funnels those events through a 200 ms single-shot
`QTimer`: the registry, Home grid and dock are rebuilt once after the burst.
No event is dropped (the timer restarts on each event) and the app data comes
from the same `load_all` path as before.

This change is not claimed as a measured speedup: the host cannot run the
shell. It removes N redundant rebuilds per install by construction; the
per-rebuild cost should be read from the VM instrumentation below.

## 3. Startup instrumentation (VM measurement facility)

`finn-shell` now accepts `FINN_SHELL_TIMINGS=1`, which prints elapsed wall
time for four checkpoints to stderr:

Measured on the Lima aarch64 dev VM (Release build, Qt 6.12, gcc 16.1),
`FINN_SHELL_TIMINGS=1` with `FINN_FORM_FACTOR=tablet FINN_SIM_WIDTH=800
FINN_SIM_HEIGHT=1280`:

```
finn-shell startup: qt application ready          0 ms
finn-shell layout: tablet 800x1280 dp (margin 24, row 44, pill 134x4)
finn-shell startup: app registry loaded          25 ms
finn-shell startup: shell surfaces constructed   46 ms
finn-shell startup: entering event loop          46 ms
```

These are one run, not an average; they include registry parsing and all
four popover constructions, and they exclude the compositor's own boot.
The 0 ms first mark is the timer starting at the app checkpoint, not a claim
that Qt starts instantly. To reproduce:

```sh
FINN_SHELL_TIMINGS=1 finn-shell 2>&1 | grep 'finn-shell startup'
```

The remaining startup work that this instrumentation is meant to quantify:
D-Bus notification registration, `ForeignWindowsManager` discovery, four
popover constructions, `DockWindow` construction, and the filesystem watcher
setup. No further optimisation is claimed until those numbers exist.

## 4. What is deliberately *not* changed here

- The compositor (`src/comp`) is a separate owner's scope. Its scene order,
  listener lifetimes and DRM path are not touched by this work; no compositor
  performance number is claimed.
- `finn-oobe`'s `box_blur_image` progressive fade and its 3D lamp are not part
  of the shadow-cache path. The OOBE form-factor work is described in
  `docs/roadmap.md`.
- Theme reload cadence (compositor 1 s, Qt every few frames) is a product
  requirement, not a cache to remove (`AGENTS.md`).
