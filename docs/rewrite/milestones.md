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
- [ ] The DRM backend and a logind/seatd session start it on tty1.
- [x] The headless backend starts and serves the expected globals.
- [x] An `xdg-shell` client maps, gets focus and closes without a protocol
      assertion.
- [ ] Pointer and keyboard input reach the focused toplevel.
- [ ] The compositor survives a client crash and a backend restart.
- [x] `wayland-info` sees the expected globals.
- [ ] The session runtime directory is owned and exported by the unit, not
      manufactured by the compositor.

Acceptance: on the VM, `WLR_BACKENDS=headless WLR_RENDERER=pixman finn-comp`
starts and serves `wayland-info`; on tty1 it paints a GTK window and the
session still switches VT.

## M2 — the runtime and the dock

- [ ] `libfinnui` is a library with layout, widgets, text, vector and tokens.
- [ ] The dock is a `layer-shell` client drawn with it.
- [ ] Tokens are generated from one source, not hand-copied into C++.
- [ ] The dock matches the Figma frame in light and dark.

Acceptance: a capture on the VM, read and compared, plus a token test that
fails when a token has no generated counterpart.

## M3 — the session and the Start Menu

- [ ] `finn-session` owns the registry, the installed list and launch.
- [ ] Apps start in transient user units under the sandbox baseline.
- [ ] The Start Menu lists only installed apps and launches one.
- [ ] A script and the shell use the same session socket.

Acceptance: `finn-session` launches a GTK app and a first-party app, and
`systemd-cgls` shows each in its own unit.

## M4 — windows and the rest of the shell

- [ ] Move, resize, snap, maximise and restore; popups and decorations.
- [ ] Control Center, clock page and notifications as layer-shell surfaces.
- [ ] Focus and stacking are one policy, not per-surface code.

Acceptance: the window behaviour matrix runs on the VM with a GTK client and
a first-party app, and every row is a screenshot.

## M5 — first-party apps

- [ ] Settings, Finn Files and Finn Store are programs under `apps/`.
- [ ] Each links `libfinnui` and none appears under `src/compositor`.
- [ ] The app-contract test fails if a first-party id leaks into the
      compositor.

Acceptance: the three apps launch and quit from the Start Menu, and the
contract test is green.

## M6 — packages and updates

- [ ] `finnpkg` and OTA keep the same verbs and the same audit trail.
- [ ] The runtime package builds from the new tree in CI.

Acceptance: a clean package build and an install on the VM.

## M7 — compatibility, then optional

- [ ] Flatpak and Wine are launch policies in `finn-session`.
- [ ] Each runs in the same sandbox baseline as a native app.
- [ ] A browser may be considered only after M5, as a sandboxed foreign app.
