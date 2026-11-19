# Native Linux app coverage

This page describes what FinnOS actually checks before and during the launch of
a native Linux application. It is deliberately explicit about what is *not*
checked; an app that fails a check is reported as failing, never given a
plausible default.

The code lives under `src/compat/` and is shared by `finn-session`, Finn Store,
Finn Files and the headless unit suite (`tests/unit/test_compat.cpp`).

## What is detected

### Desktop entries (`desktop_entry.{hpp,cpp}`)

- Freedesktop `.desktop` files are discovered recursively from the user,
  Flatpak export, `/usr/local/share` and `/usr/share` application directories.
  Earlier directories win.
- `Type=Application`, `NoDisplay`, `Hidden`, `OnlyShowIn` and `NotShowIn` are
  honored; a hidden or foreign-desktop entry is not registered.
- `TryExec` is checked against `PATH` when discovery is asked to do so. A
  missing `TryExec` removes the entry rather than leaving an unlaunchable row.
- `Exec` is tokenized into an argv vector. No shell is invoked. A target file
  or URL is inserted as a single argv element, so a filename containing quotes,
  spaces or shell metacharacters cannot become extra commands. Control
  characters are removed; unknown field codes (`%n`, `%v`, …) are dropped.
- `Actions=` and `[Desktop Action <id>]` groups are parsed. An action's `Exec`
  goes through the same no-shell expansion via `expand_desktop_action_exec()`.
- Icon names are validated before lookup: relative names must be plain names
  (no path separators or `..`), and absolute paths are accepted only under
  system icon roots. An unsafe name resolves to nothing instead of pointing the
  UI at an arbitrary file.

### Flatpak files (`flatpak_installer.{hpp,cpp}`)

- `.flatpakref`, `.flatpak` and `.flatpakrepo` are identified by extension and
  their text descriptors are parsed. Install and remote-add plans use
  `flatpak --user`; no privileged package manager is involved.
- A plan is only returned when the `flatpak` binary exists. Otherwise the error
  says so.

### AppImages (`appimage_runner.{hpp,cpp}`)

- The container type is read from magic bytes, not from the filename:
  - **Type 2** — an ELF header whose bytes 8–10 are `AI\x02`.
  - **Type 1** — an ISO9660 payload with an ELF header at offset `0x8000`.
  - A plain ELF executable without either signature is reported as exactly that.
- The ELF class and `e_machine` yield an architecture name. It is compared with
  the host architecture. A foreign-architecture image is refused with an
  explicit error; there is no emulator assumption.
- Launch planning is honest about FUSE:
  - `/dev/fuse` **and** a `fusermount` helper must exist before a FUSE launch is
    planned.
  - A Type 2 image without FUSE uses `--appimage-extract-and-run` with the
    matching environment variable, and the plan explains the disk/time cost.
  - A Type 1 image without FUSE is **not** claimed to run. It has no supported
    non-FUSE path here.
  - `--appimage-extract` is a separate plan for the side-effecting extract verb;
    it never starts the application.
- `bwrap` presence is reported as diagnostics only. FinnOS v1 does not use it
  to enforce permissions.

### Native binaries (`linux_app_probe.{hpp,cpp}`)

- ELF32/ELF64, little/big endian headers are parsed with bounds checks.
- `PT_INTERP` gives the dynamic loader path; `PT_DYNAMIC` plus `DT_NEEDED`,
  `DT_STRTAB` and `DT_STRSZ` give the shared-library sonames.
- Each soname is resolved on `LD_LIBRARY_PATH` (if set) and the standard
  `/lib`, `/usr/lib`, `/usr/lib64` and multiarch library directories. A library
  that is not found is reported by name; the summary says how many were
  missing and the diagnostics name each one.
- Desktop entries that share an id, or whose filename stem resolves to more
  than one launcher id, are reported as collisions instead of being silently
  deduplicated.

### Launch environment (`linux_sandbox.{hpp,cpp}`)

- Every third-party app receives a merged environment for `posix_spawn`:
  `WAYLAND_DISPLAY`, `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`,
  `XDG_CURRENT_DESKTOP=FinnOS`, `GDK_BACKEND` and `SDL_VIDEODRIVER` are set;
  Xwayland's `DISPLAY` is added when the compositor has published one.
- `LD_*` variables and loader/plugin hijack paths (`QT_PLUGIN_PATH`,
  `GCONV_PATH`, `GTK_PATH`, shell startup hooks) are stripped before launch.
- The builder and the exact merge are headless-testable through
  `build_linux_launch_env()` and `apply_linux_launch_env()`.

## What is not here

- **No signature or checksum verification for AppImages.** An AppImage is
  executed as an arbitrary user program. The magic-byte and architecture checks
  validate the container, not its provenance.
- **No enforced per-app sandbox.** v1 has no namespace isolation, no seccomp
  profile, no permission enforcement and no `finnd`. `bwrap` is detected but not
  used. Apps run with the `finn` user's authority.
- **No GTK theme.** FinnOS ships a Qt6 platform theme and widget style. GTK
  apps receive `GDK_BACKEND` and `XDG_CURRENT_DESKTOP` and otherwise render with
  their own toolkit theme; there is no GTK theme to apply.
- **No runtime dependency installation.** The probe reports a missing library;
  it never installs one. Privileged package installs go through `Finn Terminal`
  or `finn-update`, not through the launcher.
- **No emulation.** A native binary or AppImage for a different CPU
  architecture is refused, not run through an emulator.
