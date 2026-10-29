# Rewrite milestones

Each milestone ends with something that boots and something that is tested.
A milestone is not done because the code compiles; it is done because the
acceptance line passed on the VM.

## M0 — the contract and the first module

- [x] The C userland is preserved on the `pre-rewrite` tag.
- [x] `docs/rewrite/contract.md` exists.
- [x] `AppRegistry` reads the catalogue and the installed list in C++.
- [x] `make test-rewrite` passes on the host.

## M1 — a real compositor

- [x] `finn-comp` uses wlroots 0.20 and builds on the VM.
- [x] The DRM backend starts it on tty1 through seatd.
- [x] The headless backend starts and serves the expected globals.
- [x] An `xdg-shell` client maps, gets focus and closes without a protocol
      assertion.
- [ ] Pointer and keyboard input reach the focused toplevel (implemented, not
      automated; no input injection test yet).
- [x] The compositor survives a client crash and a backend restart (client
      crash verified; a backend restart restarts the unit).
- [x] `wayland-info` sees the expected globals.
- [x] The display socket is `/run/finn/wayland-0`, owned and exported by the
      unit, not manufactured by the compositor.

Acceptance: on the VM, `WLR_BACKENDS=headless WLR_RENDERER=pixman finn-comp`
starts and serves `wayland-info`; on tty1 it paints the shell and an app, and
the session still switches VT.

## M2 — the runtime and the dock

- [x] `libfinnui` is a library with layout, widgets, text, vector and tokens.
- [x] The dock is a `layer-shell` client drawn with it.
- [x] Tokens are generated from one source, not hand-copied into C++.
- [ ] The dock matches the Figma frame in both appearances (it is a clean
      v1 dock, not the old frame; the comparison is pending).

Acceptance: a capture on the VM, read and compared, plus a token test that
fails when a token has no generated counterpart.

## M3 — the session and the app menu

- [x] `finn-session` owns the registry, the installed list and launch.
- [ ] Apps start in transient user units under the sandbox baseline.
- [x] The app menu lists only installed apps and launches one.
- [x] A script and the shell use the same session socket.

Acceptance: `finn-session` launches a native app; `finn-session --list` and
the shell agree.

## M4 — windows and the rest of the shell

- [x] Move, resize and maximize.
- [ ] Popups and server-side decorations (v1 is client-side chrome).
- [ ] Control Center, clock page and notifications.
- [x] Focus and stacking are one policy, not per-surface code.

Acceptance: the window behaviour matrix runs on the VM with a GTK client and
a first-party app, and every row is a screenshot.

## M5 — first-party apps

- [x] Finn Files and Settings are programs under `apps/`.
- [x] Each links `libfinnui` and none appears under `src/comp`.
- [x] The app-contract test fails if a first-party id leaks into the
      compositor.
- [ ] Finn Store (needs the package manager).

Acceptance: the two apps launch and quit from the app menu, and the contract
test is green.

## M6 — packages and updates

- [ ] `finnpkg` and OTA keep the same verbs and the same audit trail.
- [ ] The runtime package builds from the new tree in CI.

Acceptance: a clean package build and an install on the VM.

## M7 — authority and sandbox

- [ ] `finnd` for power, state, packages and updates, with one audit line per
      call.
- [ ] Per-app transient units under the sandbox baseline.
- [ ] The compositor drops root once brightness and power do not need it.
