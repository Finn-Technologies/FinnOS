# System Design

Read in this order:

1. [contract.md](contract.md) — the programs, authority, the app contract,
   the current sandbox position and what v1 does not do.
2. [milestones.md](milestones.md) — what each step must boot and pass.
3. [cpp-guide.md](cpp-guide.md) — the small C++ subset, ownership rules and
   hands-on tasks for a first contribution.

## Status

- M0–M5 (the platform scope): done and verified on the VM. The compositor
  runs on DRM through seatd; the session, shell and apps run as `finn`; the
  session launches apps; the shell draws a dock, clock and menu; Finn Files
  and Settings are native apps on `libfinnui`.
- Open from M1: automated pointer/keyboard input, and a Figma comparison for
  the new dock. The input paths are implemented; they were not driven by a
  test.
- Open from M4: popups (xdg-shell popups are not handled), a Control Center,
  notifications, snap and restore, and server-side decoration. v1 is
  client-side chrome.
- M6 (packages/OTA) and M7 (finnd and the sandbox) are not done.
  [docs/roadmap.md](../roadmap.md) says what replaces them.
- `src/comp` is the compositor's directory.
