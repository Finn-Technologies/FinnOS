# FinnSDK Completeness Audit

This audit records what existed in FinnSDK at the Phase 1 baseline, what Phase 6 filled in pure-Python/docs/schema tooling without a Qt build host, and what remains missing for third-party developers to build, test, and ship FinnOS apps end-to-end.

## 1. Baseline Inventory (Before Phase 6)

### Headers & C++ Library (`src/ui/`)
- **Umbrella & forwarding headers (`src/ui/FinnSDK/`)**:
  - `FinnSDK.hpp`, `Animation.hpp`, `Application.hpp`, `Components.hpp`, `SessionClient.hpp`, `Theme.hpp`, `Tokens.hpp`, `Window.hpp` (plus `Spring.hpp` added by the Phase 3 motion track).
- **Implementation headers (`src/ui/finn/`)**:
  - `sdk.hpp`, `application.hpp`, `window.hpp`, `theme.hpp`, `tokens.hpp`, `animation.hpp`, `components.hpp` (plus `spring.hpp` in Phase 3).
- **Installed shared headers (`src/ui/CMakeLists.txt:64-89`)**:
  - Installed under `${CMAKE_INSTALL_INCLUDEDIR}/finnos/`: `FinnSDK/*.hpp`, `ui/finn/*.hpp`, `ui/generated_tokens.hpp`, `common/paths.hpp`, `system/sys_files.hpp`, `session/client.hpp`.
- **Shared library**: `libfinnui.so.0.2.0` (`SOVERSION 0`, CMake target `FinnSDK::FinnUI`).

### CMake & pkg-config Packaging
- `src/ui/CMakeLists.txt`: exports `FinnSDKTargets.cmake` (`NAMESPACE FinnSDK::`), `FinnSDKConfig.cmake` (`src/ui/FinnSDKConfig.cmake.in`), `FinnSDKConfigVersion.cmake` (`VERSION 0.2.0`, `COMPATIBILITY SameMajorVersion`).
- `src/ui/finnsdk.pc.in`: generates `finnsdk.pc` and `finnui.pc` (note: `prefix=/usr` is hardcoded in `finnsdk.pc.in:1` rather than `@CMAKE_INSTALL_PREFIX@`, which assumes `/usr` installs even when `Makefile` defaults `PREFIX=/usr/local`).

### Qt6 Platform Integration (`src/qtplugin/`)
- `libfinnplatformtheme.so` (`QT_QPA_PLATFORMTHEME=finn`): applies Finn palette, Fixel Display fonts, semantic icon resolution, and live settings reload.
- `libfinnstyle.so` (`QT_STYLE_OVERRIDE=finn`): `QProxyStyle` styling standard Qt widgets.

### Project Template (`sdk/template/`)
- Baseline contained 4 files: `CMakeLists.txt`, `Makefile`, `finn-app.desktop`, `main.cpp` (a Counter demo using `FinnApplication`, `FinnWindow`, `FinnSidebar`, `FinnBadge`, `FinnCard`, `FinnLabel`, `FinnProgressBar`, `FinnButton`, `FinnToggle`, `FinnSettingRow`, `FinnThemeManager`).
- Missing at baseline: `finn-app.json` manifest and CI workflow template.

### CLI (`tools/finn-sdk`)
- Baseline was a 256-line POSIX shell script supporting 5 commands: `new`, `build`, `run`, `register`, `check`.
- **Known defect observed in `apps/store/main.cpp:887-889`**: Finn Store invokes `finn-sdk new /home/.../Projects/<slug>` passing an absolute directory path as the positional argument, whereas the baseline `cmd_new` treated `$1` strictly as `<id>` and stripped slashes (`sanitize_id`), creating `./home...Projects<slug>` in the working directory instead of `$HOME/Projects/<slug>`. (Fixed in `tools/finn-sdk` Phase 6 by treating a path containing `/` as destination dir + basename id when `--dir` is omitted; `apps/store/main.cpp` was not modified.)
- `cmd_register` writes `$XDG_DATA_HOME/applications/finn-<id>.desktop`, which `finn-session` discovers via `AppRegistry::merge_desktop_entries()` (`src/session/app_registry.cpp:171-193`), rather than writing `~/.local/share/finn/apps/<id>.manifest` (`AppRegistry::merge_user_manifests()`).

### Documentation (Baseline)
- `docs/sdk.md` (90 lines): brief overview, 5 CLI commands, CMake snippet, 20-line minimal example.
- `sdk/README.md` (71 lines): similar quickstart and 3 compatibility tiers.
- `docs/apps.md`, `docs/session-protocol.md`, `docs/security.md`: peripheral context.

---

## 2. Third-Party Shipping Requirements vs. Status

| Requirement | Baseline | Phase 6 Status | Details & Evidence |
|---|---|---|---|
| **App manifest + metadata schema** | Missing | **Filled** | Added JSON Schema Draft 2020-12 at `sdk/schema/finn-app.schema.json`, template manifest `sdk/template/finn-app.json`, and pure-Python validator `finn-sdk validate`. |
| **Packaging format + signing** | Missing | **Filled** | Added deterministic `.finnapp` archive format (`manifest.json` + optional Ed25519 `manifest.sig` + `payload.tar.zst`) with `finn-sdk keygen`, `finn-sdk package`, `finn-sdk verify`. Uses `openssl` Ed25519 + `zstd` + `SOURCE_DATE_EPOCH` determinism. |
| **Permission model** | Missing | **Spec only (declared-not-enforced)** | Schema defines `permissions` enum (`filesystem:home-read`, `filesystem:home-write`, `network:internet`, `session:launch`, `audio:playback`, `audio:record`, `bluetooth`, `notifications`). **In v1 there is no `finnd` and no per-app OS sandbox (`docs/security.md:33-36`, `docs/design/contract.md:85-86`); apps run with user `finn`'s full authority. Permissions are advisory metadata only.** |
| **Store submission & update path** | Missing | **Documented (local-only)** | `docs/sdk/publishing.md` documents local packaging, signing, verification, and manual registration (`finn-sdk register` / `~/.local/share/finn/apps/*.manifest`). Remote Finn Store submission, `.finnapp` GUI install in `finn-store`, and app auto-updates do not exist yet. |
| **API reference** | Missing (90-line summary) | **Filled** | `docs/sdk/api-overview.md` documents the real classes, enums, methods, signals, tokens, and session IPC from `src/ui/FinnSDK/*.hpp`, `src/ui/finn/*.hpp`, `src/session/client.hpp`, `src/system/sys_files.hpp`, and `src/common/paths.hpp`. |
| **ABI & versioning policy** | Missing | **Documented (policy + gaps)** | `docs/sdk/abi-versioning.md` documents `0.2.0` / `SOVERSION 0` / `SameMajorVersion`, C++20/Qt6 coupling, and notes missing symbol version scripts and ABI CI checks. |
| **Simulator (desktop/tablet/phone)** | Missing | **CLI + Spec filled (`libfinnui` proposal)** | `finn-sdk simulate --form-factor phone|tablet|desktop` sets `FINN_FORM_FACTOR`, `FINN_SIM_*`, and `FINN_OUTPUT_*` (already supported by `finn-comp` in `src/comp/server.cpp:152-160`), with `--headless` support for isolated `finn-comp` runs. Responsive `libfinnui` adaptation proposal is in `docs/sdk/simulator.md` (requires `src/ui` edits). |
| **Touch / gesture API** | Missing | **Missing (Audit B2)** | No `wl_touch` / `QTouchEvent` / gesture handling in `src/comp`, `src/shell`, or `src/ui`. |
| **Test harness** | Missing | **Partial (CLI/packaging only)** | Added pure-Python host test suite `sdk/tests/test_sdk.py` covering manifest validation, deterministic packaging, Ed25519 signing/tamper rejection, and simulator dry-run. C++ Qt widget / offscreen UI test harness for third-party apps is still missing. |
| **CI template** | Missing | **Filled** | Added `sdk/template/.github/workflows/ci.yml` (generated by `finn-sdk new` too). Note: builds `FinnSDK` (`src/ui`) from source in CI until a hosted `finnos-sdk` pacman package exists. |
| **Sample apps** | 1 minimal template | **Partial** | `sdk/template/` provides the Counter sample with manifest + CI. Additional samples (multi-page settings app, `SessionClient` launcher, responsive phone/tablet layout) are still missing. |

---

## 3. What Is Still Missing

These items cannot be completed from pure-Python/documentation on a macOS host without Qt6/Wayland builds, compositor/library changes, or server infrastructure:

1. **Runtime permission enforcement & app sandboxing**: No `finnd`, no seccomp/Landlock/namespace isolation, and no transient per-app systemd unit isolation in `finn-session`. Native apps run with user `finn`'s full permissions (`docs/security.md`).
2. **Finn Store `.finnapp` install & remote submission backend**: `apps/store/main.cpp` has no `.finnapp` installer, signature trust store (`/etc/finn/app-keys/`), or remote catalog/update feed. There is no developer portal or submission API.
3. **Responsive form-factor support in `libfinnui` & `finn-shell`**: `FinnWindow` uses a fixed desktop title bar and sidebar split (`src/ui/finn/window.cpp`); it does not read `FINN_FORM_FACTOR` or switch to phone/tablet chrome. Phone/tablet shell surfaces do not exist (Audit B2).
4. **Touch & gesture APIs**: Neither `finn-comp` nor `libfinnui` exposes touch-point tracking, pinch/swipe gesture recognizers, or virtual keyboard (`text-input-v3`) integration (Audit B2).
5. **Hosted `finnos-sdk` package & sysroot**: Third-party CI currently clones and compiles `src/ui` from `FinnOS` source (`sdk/template/.github/workflows/ci.yml`) because no binary `finnos-sdk` pacman package is published yet.
6. **`finnsdk.pc.in` prefix bug**: `src/ui/finnsdk.pc.in:1` hardcodes `prefix=/usr` instead of `@CMAKE_INSTALL_PREFIX@`, breaking `pkg-config` when installed to `/usr/local` (outside `sdk/*` ownership).
7. **C++ / Qt offscreen UI test harness**: No `FinnTest::Harness` or `QT_QPA_PLATFORM=offscreen` helper library is shipped with `FinnSDK` for testing third-party widgets without a display.
8. **ABI enforcement in CI**: No `.map` symbol visibility script or `abidiff` / `abi-compliance-checker` check in CI; `libfinnui` is pre-1.0 (`0.2.0`, `SOVERSION 0`).
9. **Multi-sample gallery**: Only the `sdk/template/` Counter app exists; no sample apps demonstrate `SessionClient`, `FinnLayerWindow`, or `FinnPageStack` workflows.
10. **Host-only verification limit**: All Phase 6 `finn-sdk` tests (`sdk/tests/test_sdk.py`) ran on macOS with pure Python, `zstd`, and `openssl`. `finn-sdk build`, `finn-sdk run`, and `finn-sdk simulate --headless` with live `finn-comp` / Qt6 binaries were **not** executed on this macOS host and remain unverified until run on the build VM or target hardware.
