# FinnSDK artifacts, languages and simulator story

This page describes the **native app** artifact pipeline: supported languages
and toolchains, the app manifest, the `.finnapp` package and its signature, and
how an app is simulated without hardware. It complements
[`docs/sdk.md`](../docs/sdk.md), [`docs/sdk/publishing.md`](../docs/sdk/publishing.md),
[`docs/sdk/audit.md`](../docs/sdk/audit.md),
[`docs/sdk/simulator.md`](../docs/sdk/simulator.md) and the host tool
[`tools/finn-emulator`](../tools/finn-emulator).

Status words: **shipped**, **partial**, **absent**.

---

## 1. Languages and toolchains (what is real)

| Language / runtime | Status | Detail |
|---|---|---|
| **C++20 + Qt 6 / QML (`FinnSDK`)** | **shipped** | The first-class native SDK. `find_package(FinnSDK)`, `FinnSDK::FinnUI` (`libfinnui.so.1.0.0`, `SOVERSION 1`) and `FinnSDK::FinnUIQml` (`libfinnui_qml.so.1.0.0`, `SOVERSION 1`, `FinnUI 1.0`), headers `<FinnSDK/*.hpp>`, CMake package under `${PREFIX}/lib/cmake/FinnSDK`. Requires C++20, AUTOMOC, Qt 6 Base/Declarative/Wayland/Svg (`docs/sdk.md`). |
| CMake + Ninja | **shipped** | `finn-sdk build` runs `cmake -G Ninja`; `finn-sdk check` verifies `cmake`, `ninja`, `c++`/`g++`, `pkg-config`, the FinnSDK CMake package, the Qt 6 plugins, openssl and zstd. |
| Rust / Go / Dart / Kotlin native | **absent** | No FinnSDK bindings, no package templates, no sysroot. They are not part of the SDK. |
| Swift / SwiftUI / Metal | **absent** | No Swift compiler, Apple SDK or Metal in either package list. The `src/darwin` "core Swift ABI" shims are not a toolchain and do not run ordinary compiled Swift binaries (`docs/darwin-framework-coverage.md`, `docs/platform-tooling.md`). |
| Linux Wayland/X11/Flatpak/AppImage apps | **shipped (compatibility tier)** | Discovered from XDG `.desktop` entries, Flatpak exports, and verified AppImages; run natively with FinnOS Qt 6 theme env (`docs/linux-app-coverage.md`). |
| Windows PE (`.exe`, `.msi`) | **shipped (via Wine/Proton/Hangover)** | `finn::inspect_pe` inspects headers and extracts icons; `finn-win` launches in per-app Wine/Proton/Hangover prefixes (`docs/windows-app-coverage.md`). |
| macOS Mach-O (`finn-darwin`), OpenHarmony `.hap` (`finn-ohos`), Android `.apk` (`finn-android`) | **partial / inspector (compatibility runtimes)** | Clean-room runtimes/inspectors, not FinnSDK toolchains; coverage is explicitly bounded (`docs/darwin-framework-coverage.md`, `docs/ohos-kit-coverage.md`, `docs/android-coverage.md`). |

There is no hosted `finnos-sdk` pacman package or binary sysroot: the generated
CI template clones and builds `src/ui` from source on every run
(`sdk/template/.github/workflows/ci.yml:37-47`, `docs/sdk/audit.md:66`).

---

## 2. App manifest (`finn-app.json`)

Schema: [`schema/finn-app.schema.json`](schema/finn-app.schema.json) (JSON Schema
Draft 2020-12). Validator: `finn-sdk validate [--strict] [--json] <dir|file>`
(`tools/finn-sdk:682-727`).

Required fields: `schema_version` (const `1`), `id`, `name`, `version` (SemVer),
`summary`, `executable`. Notable optional fields:

- `arch`: array of `x86_64`, `aarch64` (default both). `finn-sdk package --arch`
  refuses to package an architecture the manifest does not list
  (`tools/finn-sdk:835-847`).
- `form_factors`: `desktop`, `tablet`, `phone` (default `desktop`). The schema
  itself states that phone/tablet shell and touch support are not implemented.
- `permissions`: `filesystem:home-read|home-write`, `network:internet`,
  `session:launch`, `audio:playback|record`, `bluetooth`, `notifications`.
  **Declared, not enforced**: v1 has no `finnd` and no per-app sandbox; apps run
  as user `finn` with that user's full authority (`docs/sdk/audit.md:46`,
  `docs/security.md`).
- `package`: added by `finn-sdk package` inside the packaged `manifest.json`
  with `format: "finnapp-v1"`, `arch`, `payload: "payload.tar.zst"`,
  `payload_sha256`, `payload_size`, `signed`, `signature_alg`, `built_at_epoch`.

`finn-sdk new <id> --name "..."` scaffolds `CMakeLists.txt`, `Makefile`,
`main.cpp`, `finn-app.desktop`, `finn-app.json` and a CI workflow
(`tools/finn-sdk:337-392`).

---

## 3. `.finnapp` package format (shipped)

Built by `finn-sdk package <dir> [--key PRIV.pem | --unsigned] [--arch ...]
[--stage-bin PATH] [-o OUT]`. Two nested **uncompressed USTAR** tar archives;
no zip, no outer signature over the archive itself.

```
<id>-<version>-<arch>.finnapp            (USTAR tar, deterministic member order)
├── manifest.json                        canonical JSON (sort_keys, ",", ":"), + "\n"
├── manifest.sig                         present only when signed (raw 64-byte Ed25519)
└── payload.tar.zst                      zstd -3 of the inner USTAR tar
    └── bin/<executable-name>            mode 0755
        share/applications/<id>.desktop  mode 0644
        share/icons/<icon-name>          mode 0644 (only for file icons)
        share/finn/apps/<id>/<assets>    mode 0644
```

Determinism: members are sorted by archive name, `SOURCE_DATE_EPOCH` (default
`0`) sets every member mtime, and all uids/gids are zero
(`tools/finn-sdk:859-979`, `_add_bytes_to_tar` at `:776`). The command prints
both the archive SHA-256 and the payload SHA-256.

Signing uses `openssl pkeyutl -sign -rawin` over the exact bytes of the inner
`manifest.json`; verification uses `openssl pkeyutl -verify -pubin -rawin`
(`tools/finn-sdk:923-952`, `:1071-1093`). Private keys are generated with
`finn-sdk keygen <prefix>` and must live outside the repository.

Verification (shipped):

```sh
finn-sdk package myapp --key ~/.config/finn-dev/pub.key.pem --arch x86_64
finn-sdk verify myapp/dist/myapp-0.1.0-x86_64.finnapp \
  --pubkey ~/.config/finn-dev/pub.pub.pem --json
# local dev only:
finn-sdk package myapp --unsigned && finn-sdk verify dist/*.finnapp --allow-unsigned
```

`finn-sdk verify` checks safe member paths, manifest schema (including the
`package` block), payload SHA-256/size, zstd integrity and inner tar paths, and
the Ed25519 signature when present. An unsigned archive is rejected unless
`--allow-unsigned` is passed (`tools/finn-sdk:997-1120`).

**Gaps (absent):** `.finnapp` installation from `finn-store`, a device trust
store under `/etc/finn/app-keys/`, a remote catalogue/submission API and app
auto-updates (`docs/sdk/audit.md:47,63`). The permission field is metadata only.

---

## 4. Artifact matrix today

| Artifact | Built today? | Where |
|---|---|---|
| Unsigned `x86_64` `.finnapp` in third-party app CI | **shipped** | `sdk/template/.github/workflows/ci.yml:48-57` (`finn-sdk package . --unsigned --arch x86_64`, `finn-sdk verify … --allow-unsigned`) |
| Signed `.finnapp` with a release key | **possible locally, not automated in CI** | `finn-sdk package --key …`; no CI secret/flow in the template |
| `aarch64` `.finnapp` in CI | **absent** | SDK CI template has a single x86_64 job; add the `matrix.arch` target in `docs/platform-tooling.md:1.3` |
| Hosted `finnos-sdk` package + sysroot | **absent** | CI clones and builds FinnSDK from source |
| Deterministic release attestation (rebuild twice, compare hash) | **absent** | applies to `.finnapp` and OS images alike |
| Qt offscreen test harness for third-party widgets | **absent** | `docs/sdk/audit.md:68` |
| ABI symbol map / `abidiff` CI | **absent** | `docs/sdk/audit.md:69` |

---

## 5. Simulator and emulator story

### 5.1 `finn-sdk simulate` (shipped)

Presets (`docs/sdk/simulator.md:9-13`):

| Form factor | Portrait | Landscape | Default scale |
|---|---|---|---|
| `phone` | 390×844 | 844×390 | 2.0 |
| `tablet` | 768×1024 | 1024×768 | 2.0 |
| `desktop` | 900×1440 | 1440×900 | 1.0 |

```sh
finn-sdk simulate --form-factor phone .
finn-sdk simulate --form-factor tablet --orientation landscape --headless .
finn-sdk simulate --form-factor desktop --dry-run --json .
```

It exports `FINN_FORM_FACTOR`, `FINN_SIM_ORIENTATION`, `FINN_SIM_WIDTH`,
`FINN_SIM_HEIGHT`, `FINN_SIM_SCALE`, `FINN_OUTPUT_*`, and the Finn theme
variables. `--headless` runs `finn-comp` in offscreen/headless mode on a
private socket.

**Honest limits:** While `FinnApplicationWindow` (`FinnUI 1.0`), `finn-oobe`,
and `recovery-mode` adapt to `FINN_FORM_FACTOR`, `finn-shell`'s `TabletShell`,
`MobileShell`, and `TvShell` are currently empty stubs, and there is no
hardware touch/gesture synthesis or virtual keyboard (`docs/sdk/simulator.md`).
A simulator run is not device testing.

### 5.2 `tools/finn-emulator` (shipped helper)

The probe tool complements `finn-sdk simulate`: it reports which emulator
backends actually exist and runs an app against Qt's **offscreen** platform
without needing a compositor.

```sh
tools/finn-emulator --help
tools/finn-emulator check                     # Lima/QEMU/headless finn-comp/Qt6 plugin + tools
tools/finn-emulator backends                  # capability matrix: can / cannot emulate
tools/finn-emulator validate                  # exit 0 only if a real backend is usable
tools/finn-emulator boot --arch x86_64 --iso finnos.iso   # prints a runnable QEMU command
tools/finn-emulator simulate --app sdk/template --form-factor phone --dry-run
tools/finn-emulator simulate --app myapp --form-factor tablet        # QT_QPA_PLATFORM=offscreen
tools/finn-emulator simulate --app myapp --form-factor phone --headless  # headless finn-comp
```

For the default (non-`--headless`) path it sets `QT_QPA_PLATFORM=offscreen`
plus `QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn` and `FINN_SIM_*`,
so widget layout and Finn theming can be exercised without DRM or a Wayland
socket. With `--headless` it starts `finn-comp` itself and runs the app as a
Wayland client. Both paths print the limitations rather than pretend to be a
phone. See [`vm/emulators.md`](../vm/emulators.md) for the full host/guest
matrix and the exact commands for booting FinnOS.
