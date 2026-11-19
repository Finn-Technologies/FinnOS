# Windows app coverage

FinnOS runs Windows programs through `finn-win` / Wine on `x86_64`. There is no
Windows compatibility layer in the compositor: once Wine starts a program its
window is an ordinary Xwayland `xdg_toplevel` like any other app. This page
records what the launcher code actually inspects and plans, and what it does
not do. The implementation lives in `src/compat/wine_runner.*`,
`src/compat/wine_prefix.*`, `src/compat/msi_installer.*`,
`src/compat/windows_registry.*` and `src/compat/desktop_entry.*`, and is shared
by `finn-session`, Finn Store and Finn Files.

Everything in these modules is pure C++20 and headless. A "plan" is data — an
`argv` vector plus environment pairs for `posix_spawn`. Building a plan never
executes a process, never edits the environment and never touches a shell. The
only mutating function is `create_wine_prefix_layout()`, which creates
directories and says so in its name.

## Capability matrix

| Capability | Code | Status | What it actually does |
|---|---|---|---|
| PE magic/header parse | `inspect_pe_bytes`, `inspect_pe_file` | Implemented | Bounds-checked `MZ`/`PE` parse: machine (`x86`, `x86_64`, `arm64`), GUI/console subsystem, .NET CLR directory. |
| Wine runtime probe | `probe_wine_runtime` | Implemented | Looks for Proton/Wine, DXVK/VKD3D, `finn-win`, emulator and Xwayland. Absent pieces stay absent; `ready=false` with a reason. |
| `.exe` launch plan | `build_wine_launch`, `build_msi_install_plan` | Implemented | argv/env vectors, separate arguments, no shell. Missing engine ⇒ error, not a guessed command. |
| Per-app prefix id/path | `prefix_id_for`, `prefix_path_for`, `wine_prefix_root` | Implemented | Lowercased, separator-safe, length-capped ids under `$HOME/.local/share/finn/wine/prefixes`. Traversal (`..`, absolute paths) is rejected. |
| Prefix layout | `wine_prefix_layout`, `create_wine_prefix_layout` | Implemented | `drive_c/windows/system32`, `syswow64` for win64, `system.reg`, `user.reg`, stamp path. The mutating call creates **directories only**; Wine still performs registry initialization. |
| PE → prefix architecture | `wine_arch_from_pe`, `wine_arch_from_pe_file`, `recommend_arch` | Implemented | `x86` ⇒ win32, `x86_64` ⇒ win64. ARM64/unknown are reported as unsupported/unknown, never silently mapped to win64. |
| DLL override planning | `plan_dll_overrides`, `format_dll_overrides` | Implemented (plan only) | Builds `WINEDLLOVERRIDES`: disables `mscoree,mshtml` by default, adds DXVK (`d3d9,d3d10core,d3d11,dxgi=n,b`) and VKD3D (`d3d12,d3d12core=n,b`) **only** when the caller detected them. It does not install those libraries. |
| Prefix health | `check_wine_prefix`, `PrefixInfo`, `prefix_info`, `list_prefixes` | Implemented | Reads `#arch=` from `system.reg`; reports missing `drive_c`, `system.reg`, `user.reg`, stamp and architecture mismatch. An unreadable architecture stays `Unknown`. |
| Preflight/unsupported diagnostics | `build_wine_prefix_plan`, `wine_prefix_unsupported_apis` | Implemented | Plans `wine wineboot -u` when a prefix is absent and lists known gaps (MSIX, `.lnk`/`.ico`, Direct3D, binary hives, .NET/Mono) as `unsupported` strings. |
| `.msi` detection | `detect_windows_package_kind` (extension), `inspect_windows_installer_bytes`, `inspect_windows_installer` (magic) | Implemented | OLE compound magic `D0 CF 11 E0 A1 B1 1A E1`; for readable files the CFB directory is walked and MSI streams such as `Property`/`Feature` are checked (`streams_verified`). A file with only the magic is reported as *unverified*, not as a proven MSI. |
| MSI install/uninstall plan | `build_install_plan`, `build_uninstall_plan`, `build_msi_install_plan`, `build_msi_uninstall_plan` | Implemented | Real `msiexec /i` / `/x` argv with `/qn /norestart` (or `/qb`, `/passive`) and an optional verbose log path as its own argv element. Product codes braced/bare are validated. No shell is involved. |
| Silent switches for arbitrary `.exe` installers | `exe_silent_guess` | Heuristic only | Filename-based guesses for a few known families; `InstallPlan::notes` says when no family was identified. There is no universal silent-switch database and no switch is invented for an unknown installer. |
| Uninstall lookup | `msi_product_code_from_key`, `msi_product_code_from_uninstall`, `parse_uninstall_command` | Implemented | Pulls a GUID out of an Uninstall key or string; tokenizes an `UninstallString` into argv and rejects shell control characters (`|`, `&`, `;`, `` ` ``, `$`, `<`, `>`, newlines). |
| `.msix` / `.appx` | `detect_windows_installer_kind`, `inspect_windows_installer_bytes` | **Detected only** | ZIP magic plus extension is recognized; both install-plan builders return `ok=false` with an explicit "not implemented" error. No command is fabricated. |
| Registry discovery | `parse_wine_reg_file`, `collect_uninstall_entries_from_prefix` | Implemented | Reads text `.reg` files (`user.reg`, `system.reg`, `userdef.reg`, other `.reg` in the prefix root): `DisplayName`, `Publisher`, `DisplayVersion`, `DisplayIcon`, `InstallLocation`, `UninstallString`, `QuietUninstallString`. Always sets a human-readable `reason`. |
| Icon/exe path helpers | `uninstall_icon_path`, `uninstall_exe_path` | Implemented (paths only) | Strips the `,index` suffix from `DisplayIcon` and returns Windows-style paths. Does **not** resolve them into the prefix or extract image data. |
| `.lnk` shortcut parse | `parse_windows_shortcut_bytes`, `parse_windows_shortcut_file` | Minimal | CLSID, LinkFlags, `LinkInfo` local base path, RELATIVE_PATH/WORKING_DIR/ARGUMENTS/ICON_LOCATION, UTF-16LE→UTF-8. Reports every limitation in `warnings`. |
| `.bat` / `.cmd` | `detect_windows_package_kind`, `build_prefix_init_plan` | Detected only | Recognized as package kinds; no dedicated batch launch plan here — batch files run through Wine `cmd` and get no per-binary architecture detection. |

## The default prefix is one fact

`finn::kWinePrefixRel` in `src/common/paths.hpp` is `~/.local/share/finn/wine`,
the directory `finn-win init` actually initialises (`drive_c`, `system.reg`
and `user.reg` are written there directly — there is no `default/` child).
`wine_runner`, `finn-session` and Finn Files derive their paths from that
constant, and `tools/finn-win` uses the same literal;
`tests/test_wine_prefix_path.py` fails if the C++ and Python copies drift.
`COMPAT.STATUS` reports `prefix_ready` from the files on disk, not from the
presence of a Wine binary.

Per-app prefixes live separately under
`~/.local/share/finn/wine/prefixes/<slug>` and are only planned by C++.

## Runtime requirements

- **x86_64 host with Wine or Proton.** `probe_wine_runtime()` is the authority.
  If no engine exists, `build_wine_launch()` returns an error; launch buttons
  stay disabled. Optional DXVK/VKD3D and the native Wayland driver are detected
  and reflected in env/override planning, not assumed.
- **aarch64 host.** `base/packages.aarch64` does not invent upstream Wine
  packages. `probe_wine_runtime()` reports the missing engine/translator
  honestly (`box64`/FEX may be present, but no x86 Wine is manufactured).
- **ARM64 PE binaries.** `inspect_pe_bytes` recognises `IMAGE_FILE_MACHINE_ARM64`,
  but x86_64 Wine/Proton cannot execute them. A plan using such a binary carries
  an explicit unsupported diagnostic; there is no native ARM64 Wine in this
  release.
- **Direct3D.** FinnOS ships no D3D implementation. Acceleration depends on
  host DXVK/VKD3D detection by the caller; otherwise Wine's built-in D3D is
  used, with no promise of performance or feature parity.

## Discovery and installation flow

1. `.desktop` files (including Wine `winemenubuilder` exports) are scanned
   recursively and registered as ordinary launchers; portable Windows apps can
   be registered in `~/.local/share/finn/windows.conf`.
2. `finn-session` also reads Uninstall keys from each prefix. A registry entry
   is a **claim**, not proof of installation: the referenced icon/executable is
   resolved inside `drive_*` and checked against the filesystem before a
   launcher is marked installed.
3. An MSI chosen in Finn Store or Finn Files gets an `msiexec /i … /qn
   /norestart` plan; uninstall by product code or original MSIX/MSI path gets
   `msiexec /x …`. The caller (Store/session) performs the actual process
   launch; these modules only produce data.

## Not implemented — reported, never faked

- **MSIX/AppX installation.** Detected but not installable; no msiexec plan is
  produced for a `.msix`/`.appx`.
- **Full `.lnk` resolution.** Target ID Lists (PIDLs), network shortcuts,
  distributed-link tracks and environment-variable expansion are not resolved.
  If a target exists only in an ID List, the parser returns an error plus a
  warning instead of inventing a path. ANSI strings are decoded byte-for-byte
  (no system code page), and ExtraData blocks are ignored.
- **`.ico` extraction.** No icon resource extraction from `.exe`, `.dll` or
  `.ico`. Uninstall entries without a real icon keep the generic Windows icon.
- **Binary registry hives.** `NTUSER.DAT`, `SOFTWARE`, `SYSTEM` and other binary
  hives are not parsed — only text `.reg` files.
- **Executing plans.** None of the plan builders run `wine`, `msiexec`,
  `wineboot` or `regedit`; the caller owns execution and error reporting.
- **Wine/DLL provisioning.** No installation of Wine, Proton, DXVK, VKD3D,
  Mono, Gecko or .NET Framework. Overrides are planned, libraries are not
  installed.
- **Windows DRM and anti-cheat.** Kernel-level DRM, Easy Anti-Cheat, BattlEye
  and driver-level protections are out of scope; such programs may refuse to
  start even with a working Wine engine.
- **Verified Windows GUI behaviour.** Headless plan/parse tests and the ARM dev
  VM (where Wine is unavailable) cannot prove real GUI behaviour on x86_64
  hardware; see [testing.md](testing.md).
- **Windows sandbox, signed-installer verification and an enforced per-app
  permission model** remain unimplemented in v1.

### APIs deliberately absent

Callers will not find these in the headers, and should not synthesize them from
the plan structs:

- No `install_msix()` / AppX deployment API — only `detect_windows_installer_kind()`
  and the `ok=false` plan builders.
- No ID List/PIDL resolver, network-shortcut resolver or `.lnk` target
  environment expansion; `parse_windows_shortcut_bytes()` exposes `warnings`
  where those paths would have been resolved.
- No `.ico`/DLL resource extractor; `uninstall_icon_path()` returns a path
  string and nothing more.
- No binary registry hive reader; `parse_wine_reg_file()` accepts text only.
- No execution API: no `run_plan()`, `install_now()` or `launch_wine()` in these
  modules. The caller owns `posix_spawn` and its error reporting.
- No Wine/Proton/DXVK installer, no Mono/Gecko provisioning, no .NET runtime
  shim, and no native ARM64 Windows engine.

## Tests

`tests/unit/test_windows_modules.cpp` calls the four headless self-tests
(`wine_prefix_self_test`, `msi_installer_self_test`,
`windows_package_self_test`, `windows_registry_self_test`) under
AddressSanitizer and UndefinedBehaviorSanitizer. They cover slug/path safety,
PE→arch mapping, override formatting, magic detection, quiet flags,
uninstall-command tokenization, `.reg` parsing and a synthetic Unicode `.lnk`.
The wider Windows assertions live in `tests/unit/test_compat.cpp`.
