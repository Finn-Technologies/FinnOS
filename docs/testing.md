# Testing

## Host

```sh
make test
make test-unit
make lint
```

`make test` runs the Python tests that need no display: the base-image wiring,
the catalogue and the generated tokens. `make test-unit` builds
`tests/unit/test_app_registry.cpp` with AddressSanitizer and
UndefinedBehaviorSanitizer and checks the registry rules. `make lint` fails on
trailing whitespace, a stale generated token header and an app id under
`src/comp`.

The base tests focus on the image and deployment: the package list,
the units, and the installer helpers.

## The VM

These are the checks that prove the session, not just the build:

1. Headless compositor starts, `wayland-info` sees the expected globals.
2. A real xdg-shell client (`weston-simple-shm` or a Finn app) maps and
   unmaps without a protocol assertion.
3. The session advertises the catalogue and launches an app.
4. The shell's layer surface is above app windows and its menu is drawn above
   both.
5. `grim` captures the frame in light and dark.
6. On DRM, the compositor runs as root through seatd while the session, shell
   and apps run as `finn` and connect through `/run/finn/wayland-0`.

The repository cannot automate the display steps without a seat, a GPU and
an input device; they were run by hand on the VM and the captures are the
evidence. `vm/shot.sh` is the tool.

Pointer input is now exercised on the VM with a virtual pointer
(`ydotool` + `uinput`): the launcher click opens the app menu, a menu row
launches Finn Files, and the window's maximize and restore controls resize
it. Keyboard input is still not driven by a test; the keymap, modifiers and
key forwarding are implemented but unverified. The window entrance was
captured as a frame sequence (top edge 43 → 42 → 41 px), so the 220 ms slide
is moving, not just declared.

## What is not tested

- Keyboard input: keymap, modifiers, text and the Escape menu close.
- Launch failure races: the session checks the command before `posix_spawn`,
  but a file removed between the check and the exec becomes an app that exits.
- Multi-output, HiDPI, suspend/resume, screen lock and audio.
- Package installs and updates: there is no package manager in v1.
- The compositor's root attack surface under a hostile client. The protocol
  set is deliberately small, and wlroots owns the parsers, but no fuzzing has
  been run.
- Backdrop blur: `libfinnui` implements client-side wallpaper backdrop blur
  (`Canvas::glass()`) with a 3-pass box blur. Limitations: only the wallpaper
  behind the surface is blurred (not underlying windows), and for xdg windows
  whose screen origin is not known to the client under Wayland, surface origin
  defaults to (0, 0). Compositor-level scene backdrop blur is not yet written.

A test that cannot be written without a display is not a reason to leave the
behaviour implicit; say so in the change, as this page does.
