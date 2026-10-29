# Testing

## Host

```sh
make test
make test-rewrite
make lint
```

`make test` runs the Python tests that need no display: the base-image wiring,
the catalogue and the generated tokens. `make test-rewrite` builds
`tests/rewrite/test_app_registry.cpp` with AddressSanitizer and
UndefinedBehaviorSanitizer and checks the registry rules. `make lint` fails on
trailing whitespace, a stale generated token header and an app id under
`src/comp`.

The base tests that survived the rewrite are about the image, not the
userland: the package list, the units, the installer helpers. Old protocol,
compiler and SDK tests were removed with the old tree.

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

The repository cannot automate the last four without a seat, a GPU and an
input device; they were run by hand on the VM and the captures are the
evidence. `vm/shot.sh` is the tool.

## What is not tested

- Pointer and keyboard input: the paths are implemented (`wlr_seat`,
  `libinput`, xkbcommon and the canvas widget state) but no automated input
  injection exists yet. Treat them as unverified until a test drives them.
- Launch failure races: the session checks the command before `posix_spawn`,
  but a file removed between the check and the exec becomes an app that exits.
- Multi-output, HiDPI, suspend/resume, screen lock and audio.
- Package installs and updates: there is no package manager in v1.
- The compositor's root attack surface under a hostile client. The protocol
  set is deliberately small, and wlroots owns the parsers, but no fuzzing has
  been run.

A test that cannot be written without a display is not a reason to leave the
behaviour implicit; say so in the change, as this page does.
