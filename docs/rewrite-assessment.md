# FinnOS rewrite assessment (read-only)

> **Status:** analysis only. No source, build-system or commit changes were made.
> One document was added: this file.
>
> **Method:** read-only inspection of the tree at `/Users/abhi/Desktop/FinnOS`
> on 2026-11-19, following the reading order in `AGENTS.md`: `AGENTS.md`,
> `CONTRIBUTING.md`, `NAMING.md`, `docs/design/contract.md`,
> `docs/architecture.md`, `docs/roadmap.md`, `docs/code-map.md`, top-level
> `CMakeLists.txt`, `Makefile`, then the code and tests named below.
>
> **Snapshot caveat:** the working tree has concurrent, uncommitted edits in
> `src/darwin/`, `src/ohos/`, `src/ui/`, `src/qtplugin/`, `src/session/`,
> `src/compat/`, `assets/` and several docs (per `git status`). This assessment
> describes the tree as found. Nothing in those areas was touched. Line counts
> are approximate (`wc -l` over source/tests/scripts, excluding `.git` and
> `build/`): about **74k lines**, dominated by hand-written C++20.

## Verdict up front

**A full rewrite is not worth it.** The parts of FinnOS that are hard to get
right — the wlroots scene/protocol work, the "no fake capability" honesty
discipline, the token-driven live theme, the signed A/B OTA path and the two
clean-room compatibility runtimes — are working or at least honestly scoped.
The parts that are weak — a root compositor with no `finnd`, ad-hoc line IPC,
duplicated settings files and units, a host that cannot build, thin test
coverage and drifting docs — are **cheaper to fix in place** than to recreate.
A rewrite would have to re-earn roughly 13k lines of `finn-darwin`, 6k lines of
`finn-ohos`, 5k lines of custom Qt widgets and the entire OTA/boot chain with
no user-visible gain. Rewrite the *boundaries*, not the *product*.

The only conditions under which a rewrite becomes rational are given in
[§2.6](#26-only-if-a-full-rewrite-is-mandated).

---

# Part 1 — As-is assessment

## 1.1 What is genuinely implemented vs what is aspirational

The repository's "what is not here yet" sections
(`docs/architecture.md:139-149`, `docs/roadmap.md:37-44`,
`docs/design/contract.md:102-104`) are the **floor**, not the ceiling. The
table below is deliberately blunt and cites file evidence.

| OS function | Genuinely implemented (evidence) | Aspirational / absent (evidence) |
|---|---|---|
| Display / compositor | wlroots 0.20, DRM + headless backends, xdg-shell, layer-shell, foreign-toplevel, `ext-session-lock-v1`, fractional scale, screencopy, Xwayland on demand, overview/tiling, fullscreen tree (`src/comp/server.cpp` ~2090 lines, `window.cpp`, `layer.cpp`, `wlroots.hpp`, `src/comp/Makefile:1-43`; `docs/architecture.md:91-111`) | Multi-monitor arrangement editor and mixed per-output scale (`outputs_.front()` global config; `docs/architecture.md:145`), compositor GPU shader blur (`docs/testing.md:67-69`), dropping compositor root (`roadmap.md:34-35`) |
| Input | Keyboard (XKB layout, repeat), pointer, touch, tablet-tool listeners and seat capabilities (`src/comp/server.cpp:812-1012`, `server.hpp:194-203`), edge-swipe gestures, WM shortcuts | No automated input-driver tests; no IME (`zwp_text_input_v3`); sticky/filter keys parsed but not enforced (`docs/architecture.md:145-146`; `docs/testing.md:53-55`) |
| Scene order | One fixed stack in `src/comp/server.hpp:150-156`: background → layer0 → layer1 → toplevels → layer2(top) → fullscreen → overlay → lock, with strict input masks (`docs/design/contract.md:82-84`) | `AGENTS.md:81-87` shows an older, shorter order — stale relative to code and `architecture.md:98` |
| Session / app launch | C++20 `finn-session`, one line-oriented socket, `SO_PEERCRED`, mtime-cached registry, `posix_spawn`, 6 app sources; verbs `PING`, `QUIT`, `COMPAT.STATUS`, `WIN.RUN`, `POWER.ACTION`, `APP.LIST`, `APP.LAUNCH`, `APP.OPEN` (`src/session/main.cpp:448-558`; `docs/session-protocol.md`) | Per-app sandbox, transient systemd units, seccomp (`roadmap.md:30-35`), typed/structured IPC (current is `\|`-delimited text with 4096-byte line cap, `main.cpp:192-209`) |
| Shell / UI | Qt6 **Widgets** C++20, custom-painted `FinnWindow` CSD, spring physics (`src/ui/finn/spring.hpp`), two Qt plugins (`QT_QPA_PLATFORMTHEME=finn`, `QT_STYLE_OVERRIDE=finn`), dock/Home/Spotlight/Control Center/notifications/lock (`src/ui/`, `src/qtplugin/`, `src/shell/popovers.cpp` ~2810 lines) | No QML anywhere (`find . -name '*.qml'` → none), no translations (no `.ts`/`.qm`), partial accessibility enforcement (`docs/architecture.md:146`) |
| Native apps | 10 first-party apps in `apps/` (settings, files, terminal, notes, store, viewer, monitor, installer, oobe, wallpaper) + optional QtWebEngine `flourish` (`apps/CMakeLists.txt`, `Makefile:40-43`) | No "real downloadable-app matrix" in tests; Store install flows tested only as parsers/unit logic |
| Notifications | `NotificationServer` + freedesktop D-Bus adaptor + masked layer-shell toasts, with `replaces_id` timer fix (`src/shell/notifications.cpp`, `docs/audit/remaining-issues.md` m6) | Persistence across shell restart, DND and priority policy are not specified in docs |
| Power / lock | Lock on Super+L, idle and suspend via `/run/finn/state/lock-request`, real `/etc/passwd` + `su` check, `passwd -S NP` guard, `ext-session-lock-v1` and WM shortcut gating (`docs/architecture.md:54-74`; `src/comp/server.hpp:78-79,157-158`) | PAM-backed unlock, `finnd`-mediated power, per-app inhibit policy; `CHANGELOG.md:58-61` still says `ext-session-lock-v1` is future work — stale |
| Networking / devices | Async `nmcli`, `bluetoothctl`, `wpctl`, UPower, backlight/battery `/sys`, MPRIS via Qt D-Bus, honest "absent" reporting (`src/system/desktop_services.cpp`, `src/system/sys_files.cpp`, `docs/architecture.md:16`) | CLI-output scraping instead of D-Bus APIs; no ModemManager/cellular (`roadmap.md:43`); brightness written directly to sysfs by user process (`desktop_services.cpp:332-351`) |
| Updates | Python stdlib OTA: Ed25519 manifest verify, HTTP Range resume, zstd deltas, A/B slots, boot counting, health-check commit/rollback (`src/ota/finn_ota/*`, `src/ota/finn-update`, `docs/ota.md:1-230`), 14/14 host tests plus loopback ext4 end-to-end (`docs/ota.md:257-296`) | Secure Boot, dm-verity, TPM2, real hosted repo, real-disk power-cycle rollback (`docs/architecture.md:144`, `docs/ota.md:304-308`), `finnos.gpg` is an empty placeholder (`docs/repo.md:9-11`) |
| Package install | `flatpak --user`, Wine/`.exe` launch, `.finnapp` verify, `finn-store` GUI; system-wide pacman guided to Terminal (`docs/architecture.md:141`) | Privileged package operations behind `finnd`; no live `[finnos]` repo |
| Settings | Flat key=value file at user path mirrored to `/var/lib/finn/settings`, atomic writes, live reload in compositor (1 s) and Qt plugins (`src/system/sys_files.cpp:52-76,174-280`; `docs/code-map.md:112-121`) | Two copies of the settings store with different authority (`paths.hpp:14,34`), no schema/versioning, `text_scale`/`color_filter`/sticky/filter keys persisted but not fully enforced (`docs/architecture.md:146`) |
| Install / boot | Plymouth `finnos` theme in both initramfs, three menus with silent kernel params, 3 s waits, `finn-install.sh` GUI wizard, `--ab-slots`, branding guard (`docs/architecture.md:18-52`, `base/mkfinniso`, `tools/check-branding.sh`) | Visual correctness on real GPU/firmware unverified (`docs/testing.md:60-66`) |
| Compatibility | `finn-darwin` clean-room Mach-O/libSystem/objc/Foundation/CoreGraphics/AppKit-on-Qt6 + installers (~13k lines, `src/darwin/`); `finn-ohos` Panda VM/ArkUI-on-Qt6/NAPI (~6k lines, `src/ohos/`); `diagnose` reports gaps honestly (`framework_coverage.cpp:8-55`) | Swift type metadata/SwiftUI/Metal/WebKit/CoreData/Keychain absent (`framework_coverage.cpp:10-27`), OHOS hardware/distributed kits and signature enforcement absent (`docs/architecture.md:149`) |
| Security | Two-authority model, `SO_PEERCRED`, 0660 display socket, no TCP listener (`docs/security.md:3-21`) | No sandbox, no `finnd`, compositor root; **`docs/security.md:37-38` still claims no A/B slots/package signatures — contradicted by `docs/architecture.md:144` and `docs/ota.md` (stale)** |
| Telemetry / AI | None by design (`AGENTS.md:95-99`, `docs/security.md:41`) | Nothing to build; keep none |
| Testing | `make test` (Python), `make test-unit` (9 ASan/UBSan binaries), `make lint`, `base/mkfinniso --check`, OTA loopback E2E (`docs/testing.md:11-30`) | No compositor/input integration harness, no real app-install matrix, no hardware DRM/input, QEMU ISO skipped unless `FINN_TEST_ISO` (`docs/testing.md:15,51-71`) |
| Docs/tooling | Docs-as-product, lint guards, code map, audit ledgers (`docs/`, `scripts/lint.sh`, `tools/check-branding.sh`) | Multiple stale pages (§1.4) and two build systems (§1.3) |

## 1.2 Architectural strengths worth preserving

| Strength | Why it matters | Evidence |
|---|---|---|
| **One authority model** | `finn-comp` root via seatd, `finn-session` as `finn`, no second door. The privilege boundary is explicit and documented. | `AGENTS.md:68-77`; `docs/design/contract.md:45-54`; `docs/security.md:3-21` |
| **Compositor knows no app ids** | Prevents the old switchboard from growing back; enforced mechanically. | `AGENTS.md:50-55`; `scripts/lint.sh:17-23`; `src/comp/server.hpp` has no app constants |
| **One scene order** | A single documented stack avoids z-order bugs (menu behind app, fullscreen under dock). | `src/comp/server.hpp:150-156`; `docs/design/contract.md:82-84`; audit fix M10 |
| **Line-oriented protocol** | Simple, inspectable, testable; JSON is confined to the host/MCP boundary. A reporting verb cannot mutate. | `AGENTS.md:89-93`; `src/session/main.cpp:448-558` |
| **Token source of truth** | Palette generated from `ui/_tokens.fui`, checked in, staleness fails lint; live reload without restart. | `ui/_tokens.fui:1-5`; `tools/tokens.py`; `scripts/lint.sh:7-15`; `docs/code-map.md:112-121` |
| **"Never fake a capability"** | Missing hardware/runtime is reported absent; buttons disabled; `diagnose` enumerates gaps. This is the project's hardest-won property. | `AGENTS.md:43-48`; `src/darwin/framework_coverage.cpp`; `docs/design/contract.md:91-95` |
| **VM-as-build-machine** | Acknowledges that the macOS host cannot build wlroots/Qt targets; deploy is explicit and repeatable. | `AGENTS.md:118-133`; `vm/deploy.sh:6-9,59-71`; `docs/development.md:1-40` |
| **Honest product docs** | Roadmap/architecture keep explicit "not here yet" sections; audits list resolutions and remaining non-goals. | `docs/architecture.md:139-149`; `docs/roadmap.md:20-53`; `docs/audit/remaining-issues.md:68-81` |
| **OTA with real rollback logic** | Signed manifest, hash checks, boot counting and simulated E2E rollback — rare in a project this size. | `docs/ota.md:91-125,257-308`; `src/ota/finn_ota/` |
| **Listener lifetime discipline** | Every wlroots listener lives in a struct with a destructor; documented after a real assertion. | `AGENTS.md:160-162`; `docs/code-map.md:152` |

## 1.3 Real liabilities

| Liability | File evidence / mechanism | Consequence for a rewrite decision |
|---|---|---|
| **Two build systems** | Top `CMakeLists.txt:24-42` builds ui/qtplugin/shell/apps/tools (incl. darwin/ohos under `FINN_BUILD_TOOLS`) but **omits `src/comp`, `src/session`, `src/ota`**; `Makefile:10-46` invokes `make -C src/comp` and `make -C src/session` with separate hand-written flags (`src/comp/Makefile:1-20`, `src/session/Makefile:1-14`). | A rewrite must first unify the build; otherwise a "clean" CMake build silently produces an incomplete OS. |
| **Host cannot build** | `AGENTS.md:120`, `CONTRIBUTING.md:17-27`, `vm/deploy.sh:6-9` (host is editor only). | No fast local feedback; no reproducible CI on the host; every change waits on the VM. |
| **Deploy fragility / pacman 404** | `vm/deploy.sh:66-70` runs `pacman -S --needed` against live mirrors, already needing `--ignore=systemd --ignore=systemd-libs`; `base/pacman.conf:60-66` points `[finnos]` at a placeholder URL with `SigLevel = Required` (audit B12). A 404 during `vm/deploy.sh` was observed in the brief; the script's unpinned `-S --needed` against that config is the mechanism. | Build reliability, not architecture, is the current bottleneck. Fix with a containerised, pinned toolchain before any rewrite. |
| **Duplicated facts** | Units exist in `base/` and `base/airootfs/` and are compared by `base/mkfinniso --check:331-341`; settings are double-written to `~/.config/finn/settings` and `/var/lib/finn/settings` (`src/system/sys_files.cpp:271-280`) with two constants (`src/common/paths.hpp:14,34`); lock requests are a filesystem protocol (`paths.hpp:29`). | The codebase already knows "one copy of a fact" is a rule; violations are concentrated at privilege boundaries. |
| **Language/runtime seam count** | C++20 core; one C shim + one C helper in compositor (`src/comp/wlroots.hpp`, `xwayland_shim.c`); one assembly file in darwin; **Python** OTA CLI (`src/ota/`); **shell** installers/boot scripts (`vm/finn-install.sh`, `base/setup.sh`); line IPC + D-Bus + filesystem flags + `pkexec` + CLI-output scraping as five IPC idioms. | The "C vs Qt" boundary is actually small; the expensive seams are *tooling and IPC diversity*, not compiler language. |
| **Clean-room compatibility ceiling** | `src/darwin/framework_coverage.cpp:10-27` lists 13 unimplemented frameworks and 11 missing symbols; ordinary Swift binaries are explicitly blocked by missing mangled-name metadata (`framework_coverage.cpp:33-41`); `src/ohos/abc_runtime.cpp:532-588` tracks unsupported kits. ~19k lines already written for a narrow app set. | Reimplementing full guest frameworks is unbounded work with ABI/API churn. Freeze scope or isolate guests instead. |
| **Test gaps** | `docs/testing.md:51-71` admits no automated pointer/keyboard driving, no physical hardware, no multi-monitor, no blur, no sandbox/finnd; `tests/` has no compositor or input harness; QEMU ISO gated behind `FINN_TEST_ISO` (`docs/testing.md:15`); no real downloadable-app matrix. | Any rewrite must budget for a headless compositor/input test harness or it will repeat unverified changes. |
| **Security model still aspirational** | `docs/security.md:31-40`, `roadmap.md:20-35`: no sandbox, no `finnd`, compositor root; `finn-session` runs `systemctl reboot/poweroff/suspend` as `finn` (`src/session/main.cpp:489-508`) and user process writes backlight sysfs (`desktop_services.cpp:332-351`) — authority currently delegated to polkit/udev rather than an audited Finn control plane. | This is the strongest case for *targeted* replacement: add `finnd`, do not rewrite the UI. |
| **Partial accessibility / i18n** | No `.ts`/`.qm` files; settings persist `text_scale`, `color_filter`, `sticky_keys`, `filter_keys` (`src/system/sys_files.cpp:200-209`) while runtime enforcement remains incomplete (`docs/architecture.md:146`); `orca` not packaged (`docs/audit/code-audit.md:160`). | A rewrite that claims accessibility must implement enforcement, not just settings rows. |
| **Docs drift** | See §1.4. | Documentation is the product (`AGENTS.md:112-116`); drift is a real defect, not cosmetics. |

## 1.4 Existing docs that are stale or in tension

Per the brief, these are referenced rather than corrected. The right-hand column
states which source to trust.

| Page | Stale claim | Trust instead |
|---|---|---|
| `docs/security.md:37-38` | "No package signatures / A/B slots" | `docs/architecture.md:144`, `docs/ota.md:91-125` — A/B OTA and Ed25519 manifests are implemented; only Secure Boot/dm-verity/TPM2 and hosted infra are missing. |
| `docs/security.md:11,35-40` | Presents `finnd` and sandbox as the only missing security pieces; omits that power actions already go through `systemctl` and backlight through sysfs | `src/session/main.cpp:489-508`, `src/system/desktop_services.cpp:332-351`; `docs/roadmap.md:20-35` |
| `docs/session-protocol.md:19-34` | `PING → OK finn-session 1`, `APP.LAUNCH → OK pid=<pid>`, 4-field list lines, `QT_QPA_PLATFORM=wayland` | `src/session/main.cpp:456-520` actually replies `OK pong`, `OK launched`, 7 fields incl. source/category/note; env is `wayland;xcb` (`main.cpp:224-233`). Code wins. |
| `CHANGELOG.md:58-61` | "`ext-session-lock-v1` is still future work" | `src/comp/server.hpp:169,233`, `docs/architecture.md:70-72`, `docs/audit/remaining-issues.md` B6 — it is wired. |
| `AGENTS.md:81-87` | Scene order without `fullscreen` and `lock` | `src/comp/server.hpp:150-156`, `docs/design/contract.md:82-84`, `docs/architecture.md:98` — code has eight layers. |
| `docs/architecture.md:139-149` vs `docs/audit/remaining-issues.md:3` | Audit says "0 open blockers/majors/minors" while architecture lists many gaps | They are different scopes: the audit ledger covers *audited findings fixed*, not product non-goals. Both can be true; do not read the ledger as feature completeness. |
| `NAMING.md:32-53` / `docs/architecture.md:14` | Lists a broad binary family (`finn-terminal`, `finn-notes`, `finn-store`, …) as program roles | Accurate as roles; note `Makefile:40-43` builds only 8 app targets in the `apps` target (installer/oobe are separate), and `flourish` is optional. |

## 1.5 What cannot be known without running hardware

No claim below is made in this assessment.

| Unknown | Why a read-only assessment cannot settle it |
|---|---|
| DRM scanout, mode setting, real GPU frame pacing and 60/120 fps | No physical x86_64 target; only headless/pixman and VM paths. `docs/testing.md:56-66`. |
| Plymouth mark centring, flicker and handoff on real firmware | The theme files and kernel params are checked statically; the first painted frame is not. `docs/architecture.md:89`, `docs/testing.md:60-66`. |
| Real keyboard layout, touch, tablet-tool and edge gestures | Compositor listeners exist (`server.cpp:812-1012`) but no driver-level test; manual VM check only. |
| Suspend/resume + lock behaviour, `PrepareForSuspend`, logind/polkit prompts | Depends on real logind/polkit/udev policy on a machine. |
| A/B power-cycle rollback on real UEFI/BIOS | Verified only via loopback devices and boot-backend simulation. `docs/ota.md:304-308`. |
| Native Wine execution and the downloadable-app matrix (Flatpak/Wine/`.app`/`.hap`) | ARM64 VM has no native Wine; installs in `CHANGELOG.md:12` were manual, not automated. |
| Screen-reader, colour-filter and XKB accessibility behaviour on real hardware | Settings exist; runtime enforcement is incomplete. |
| Secure Boot / dm-verity / TPM2 and hosted `repo.finnos.org` | Infrastructure and signing keys are placeholders. `docs/repo.md:9-11`. |

---

# Part 2 — Rewrite recommendation

## 2.1 Verdict and principle

**Do not rewrite the OS.** Do a *strangler* replacement of the four structurally
wrong seams: (1) build/deploy reproducibility, (2) the privilege boundary
(`finnd`), (3) IPC typing between session/shell/apps, (4) duplicated settings
and unit facts. Keep the compositor, Qt Widgets UI, token pipeline, OTA format
and compat runtimes.

If a future product decision changes the base (e.g. abandon Arch/pacman, or
become a phone-first OS), re-run this assessment; that is the only scenario in
which the cost of a full rewrite can be justified.

## 2.2 Stack chosen per layer (concrete, with justification)

| Layer | Chosen stack for any future rewrite | Why not the alternatives |
|---|---|---|
| Compositor | **Keep wlroots 0.20 + C++20 wrappers**; if a language change is ever required, evaluate **Rust + smithay** as a spike, not a port | Smithay means reimplementing ~3k lines of already-correct protocol/scene code, Xwayland, screencopy and lock; no user-visible gain. Qt/Wayland compositing loses layer-shell/foreign-toplevel maturity. The C boundary is only `wlroots.hpp` + `xwayland_shim.c`, so "C vs C++" is not the real problem. |
| Shell & apps | **Keep Qt6 Widgets C++20**, custom-painted controls, `FinnWindow`, Qt plugins, token header | There is no QML in the tree and no translation catalogue; rewriting 5k lines of hand-tuned widgets into QML would lose Figma-exact control. Flutter/engine adds a second runtime and breaks native Qt plugin integration; webview adds a browser engine and attack surface; GTK4/libadwaita discards the entire token/system-theme machinery. |
| Session daemon | **Rust or Go, single typed length-prefixed IPC over the existing Unix socket**, with a compatibility adapter for the current line verbs | The current protocol is stringly typed and mixes verbs with authority (`POWER.ACTION` in a user broker). Typed IPC gives schema evolution and testability; keep `SO_PEERCRED`. Keep C++ as fallback if a second language is unacceptable — the win is *typing*, not the language. |
| Privileged authority | **`finnd` in Rust**: sole root daemon; D-Bus + polkit for user-facing actions; owns DRM device handoff, power, brightness, OTA apply, privileged package ops | Matches `roadmap.md:20-28` and `docs/design/contract.md:40,50`. Compose root can then be dropped. Do not grow app launch into it. |
| Devices / auth | **seatd + logind + polkit + udev**, as today; `finnd` mediates only sysfs actuators | Already correct direction (`AGENTS.md:68-77`). Avoid bespoke device enumeration. |
| Display / input | wlroots as above; **headless integration harness** plus virtual-input injection for CI; keep XKB via libxkbcommon | Adds the missing verification without touching the implementation. |
| Installers / OTA | **Keep A/B ext4 + Ed25519 signed manifests + boot counting**; move the Python CLI into the same Rust binary as `finnd` *only if* supply-chain hardening warrants it; keep the manifest JSON format | OTA logic is the most security-sensitive and best-tested code (`docs/ota.md:257-296`); a language change must preserve the format and rollback semantics byte-for-byte. |
| Package install | **pacman + `flatpak --user` + Wine on x86_64**, privileged operations via polkit/`finnd`; no clean-room package manager | Replacing pacman means replacing the base image, repo signing and the installer — a product decision, not a refactor. |
| Settings persistence | **One schema-versioned store** (systemd-style keyfile or small typed D-Bus settings service) with the current flat file kept as a migration source; one canonical path, no mirrored write | Removes the `/var/lib/finn/settings` vs `~/.config/finn/settings` duality (`sys_files.cpp:271-280`) while preserving live-reload reads by compositor and Qt plugin. |
| Shell/UI, themes, fonts, i18n | Qt6 Widgets + `ui/_tokens.fui` → generated header; add Qt Linguist/gettext catalogues and fontconfig fallback (already has Fixel + DejaVu/Noto) | Keeps the one source of truth and live theming; i18n is additive. |
| Accessibility | Qt accessibility names + AT-SPI/ORCA package + compositor colour-filter shader + XKB sticky/filter keys + text scale applied in fonts | `roadmap.md:41` and `architecture.md:146`; enforce before claiming. |
| Compatibility runtimes | **Keep clean-room code but freeze the supported set**; classify apps into a published tier list; for untrusted/unsupported guests prefer **container/VM isolation** (bubblewrap/Flatpak or a lightweight VM) over expanding frameworks; Wine/Darling-style full reimplementation is *not* recommended | `framework_coverage.cpp` is an honest but unbounded backlog; SwiftUI/Metal/WebKit/CoreData would be years of work. Isolation gives a security boundary the clean-room shims cannot. |
| Security/auth | `finnd` + polkit + per-app transient systemd scopes (`NoNewPrivileges`, `ProtectSystem=strict`), seccomp; keep `SO_PEERCRED` user broker for launch | Implements `roadmap.md:30-35` without disturbing the compositor. |
| Telemetry/errors | **None by default**; local structured journal logs only; any crash reporting strictly opt-in and offline-first | Preserves `AGENTS.md:95-99` and `docs/security.md:41`. |
| Boot | Arch initramfs + Plymouth + systemd-boot/GRUB as today; add UKI, Secure Boot, dm-verity and TPM2 sealing only with real key custody | Keeps branding and silent-boot guarantees (`docs/design/contract.md:18-27`); verified boot is infrastructure work, not code churn. |
| Testing | Keep `make test`/`test-unit`/`lint`; add containerised Linux CI, headless compositor protocol tests, synthetic input injection, and a real downloadable-app matrix in the VM | Closes `docs/testing.md:51-71` without changing product code. |
| Docs/tooling | Keep docs-as-product + lint guards; add a doc-drift check (at minimum, run the protocol doc against `finn-session` output); one CMake build with a pinned toolchain container | Fix the five stale pages in §1.4 and the two-build-system split. |

## 2.3 Per-OS-function matrix

"Biggest risk" is the single risk that should stop a migration if it materialises.

### Core system

| Function | Current implementation | Recommended rewrite approach | Migration path | Single biggest risk |
|---|---|---|---|---|
| Devices | seatd-backed root compositor, `/sys` reads/writes in `src/system/sys_files.cpp`, logind/UPower/bluetoothctl probes | Keep seatd/logind/udev; move sysfs actuators behind `finnd` with polkit | Land `finnd` first; leave read-only probes in place; then flip compositor/user writes to `finnd` | Privilege regression: backlight/battery/power stop working on machines with unusual udev rules |
| Display/compositor | wlroots 0.20 C++ in `src/comp/`; eight-layer scene; DRM/headless | Keep; add per-output config via `wlr_output_manager_v1` (partly present) and GPU blur only if desired | No rewrite; extend in place; optional Rust/smithay spike behind a feature flag | Losing Xwayland/screencopy/lock correctness or the fixed scene order |
| Input | Keyboard/pointer/touch/tablet listeners, XKB layout, gestures | Keep wlroots input; add virtual-input integration tests and `zwp_text_input_v3` IME | Test harness first; IME as additive protocol | No automated driver-level verification; IME changes can break existing clients |
| Session / IPC | Line protocol, `SO_PEERCRED`, `posix_spawn`, registry cache (`src/session/main.cpp`) | Typed IPC with schema versioning; move `POWER.ACTION` out to `finnd`; keep launch in user broker | Ship adapter that accepts old and new verbs; migrate `client.cpp`, shell, apps; then remove old verbs | Breaking every client at once — mitigate by dual-stack protocol for one release |
| App launching | Registry + 6 sources, `.desktop`/Flatpak/Wine/macOS/OHOS launch plans | Keep the launch broker model; express launch plans as typed descriptors instead of `\|` strings | Replace `spawn_app` plan construction incrementally, one source at a time | Divergent app registry semantics between Store, Shell and session |
| Notifications | `NotificationServer` + freedesktop D-Bus adaptor + layer-shell toasts in shell | Keep; optionally extract to a small user daemon with D-Bus persistence | Extract only if shell restart loses notifications | Losing masked input regions or `replaces_id` semantics; overlay eating clicks (`AGENTS.md:155-156`) |
| Power / lock | `systemctl` verbs in user session, `su`-backed lock overlay, lock-request file, `ext-session-lock-v1` | `finnd` + logind D-Bus; PAM-backed unlock; keep `ext-session-lock-v1` and shortcut gating | Introduce `finnd` verbs, switch shell to logind signals, then delete `POWER.ACTION` from session | Auth bypass or lock failing to gate WM shortcuts (audit B6/B7) |
| Networking | CLI scraping of `nmcli`/`bluetoothctl`, Qt D-Bus MPRIS (`src/system/desktop_services.cpp`) | Native NetworkManager/BlueZ D-Bus (Qt DBus or zbus); keep honest absent-state reporting | Replace one probe at a time behind `FinnDesktopServices` interface | D-Bus permission prompts and policy churn; Wi-Fi secret handling regression (audit B10) |

### User-facing system

| Function | Current implementation | Recommended rewrite approach | Migration path | Single biggest risk |
|---|---|---|---|---|
| Updates | Python OTA, signed manifest, A/B ext4, boot counting, health-check units | Keep format/rollback; optional Rust single-binary port; add dm-verity/Secure Boot later | Port module-by-module with `tests/test_ota.py` as an oracle; keep CLI flags | Breaking rollback correctness; without hardware power-cycle tests this is unverifiable (`docs/ota.md:304-308`) |
| Package install | `flatpak --user`, Wine, `.finnapp` verify, pacman guidance | Keep; privileged pacman via polkit/`finnd`; no in-house package manager | Wire privileged verbs into `finnd`; Store calls them | Placeholder `[finnos]` repo / empty keyring breaks `pacman -Sy` (audit B12) |
| Settings | Two flat files (`~/.config/finn/settings`, `/var/lib/finn/settings`) | One schema-versioned typed store with file migration | Write new store, read old as fallback, then cut over compositor/plugin readers | Breaking live theme reload (no restart allowed — `AGENTS.md:163-164`) |
| Shell / UI | Qt6 Widgets, custom CSD, spring physics, plugins | Keep Qt Widgets and tokens; add i18n; no engine swap | Additive only | Pixel drift from Figma-verified tokens |
| User accounts | OOBE `useradd`/`chpasswd` via `pkexec`, `account.conf`, real `/etc/passwd` lock list | Keep, but route through `finnd`/accounts D-Bus with typed requests | Move the `pkexec` calls behind `finnd` verbs | Credential exposure in argv/logs; regression of audit B9 |
| Themes / fonts / i18n | `ui/_tokens.fui` → generated header, Fixel + Noto fallback, live reload; no translations | Keep token pipeline; add Qt Linguist catalogues and fontconfig checks | Add translator support without changing token generation | Generated header drift (`make lint` is the guard) |
| Accessibility | Names/focus rings, high contrast, reduced motion; other settings persisted but not enforced | Enforce all persisted settings; package ORCA; compositor colour filter + XKB sticky/filter keys | Enable one setting at a time with a UI test | Claiming support that is not enforced ("never fake a capability") |
| Security / auth | Two-authority model, no sandbox, compositor root | `finnd` + polkit + per-app systemd scopes + seccomp | Land authority daemon, then scopes behind an opt-in flag | App compatibility / unexpected privilege prompts |
| Compatibility runtimes | Clean-room `finn-darwin` + `finn-ohos`, `diagnose` gap reporting | Freeze framework scope; publish supported tier list; use container/VM isolation for untrusted guests | Keep current launchers; add isolation as an optional runner | Endless ABI/API chase (SwiftUI/Metal/OHOS kits) and untrusted guest code escaping the shim |
| Telemetry / errors | None; honest absent-state UI | Keep none; journal logs; opt-in, offline-first crash reports only | Add local log schema first; never default to network | Violating the no-AI/no-telemetry promise |
| Boot | Plymouth mark, silent kernel params, GRUB/systemd-boot/syslinux, 3 s menus, two-root bootstrap | Keep; add UKI + Secure Boot + dm-verity when keys and hardware lab exist | Add verification as an optional signed boot path | No key custody or real-hardware test — signing work without infra is theatre |
| Testing | Python host suite, 9 ASan/UBSan unit binaries, VM screenshots, OTA loopback E2E | Containerised CI + headless compositor/input harness + real app matrix + HIL lab | Add CI first, then harness, then matrix; keep all existing suites | Host cannot build today (`AGENTS.md:120`) — CI must supply a pinned Linux toolchain |
| Docs / tooling | Docs-as-product, lint/branding guards, code map, audit ledgers | Fix stale pages in §1.4; add protocol drift check and one CMake build | Docs changes are real changes (`AGENTS.md:151-152`); do them before any refactor | Docs describing a system that no longer matches the code |

## 2.4 Effort / risk ordering

| Order | Action | Relative effort | Relative risk | Rationale |
|---|---|---|---|---|
| 1 | **Keep unchanged:** compositor, Qt Widgets UI, token pipeline, line protocol (as public compat surface), OTA manifest/rollback, boot chain, no-telemetry stance | None | None | These are the working, differentiating parts. |
| 2 | **Fix first:** stale docs (§1.4), unified CMake + pinned Linux CI container, `vm/deploy.sh` dependency pinning, headless compositor/input tests, real app-install matrix, one settings store | Low–medium | Low | Highest value per unit risk; removes the reasons a rewrite is tempting. |
| 3 | **Add next:** `finnd` with polkit, typed session IPC (dual-stack), D-Bus-native network/bluetooth, per-app systemd scopes | Medium | Medium | Corrects the only structurally wrong boundary without touching the UI. |
| 4 | **Consider with evidence:** Rust port of OTA/daemon; optional smithay spike; optional QML proof-of-concept; container isolation for guest runtimes | High | High | Justify with measured reliability or security gains, not language preference. |
| 5 | **Do not do:** full OS rewrite; GTK4/Flutter/webview shell; replacing pacman; expanding clean-room frameworks to SwiftUI/Metal/OHOS hardware kits | Very high | Very high | Cost is dominated by re-creating already-working behaviour and honesty machinery. |

## 2.5 Staged plan and stop conditions

| Stage | Outcome | Evidence to proceed | Stop condition |
|---|---|---|---|
| 0 — Evidence (2–4 weeks) | Containerised Linux CI that builds all targets; headless compositor protocol test; four stale docs corrected | `make`, `make test`, `make test-unit`, `make lint`, `mkfinniso --check` all green in CI from a clean container | CI cannot reproduce the VM build with pinned packages — fix the toolchain before any refactor |
| 1 — One copy of a fact (4–8 weeks) | Single CMake build; single settings store; dual-stack typed session protocol | Live theme reload still works with no restart; old clients still launch apps | Theme/live reload regression or app-launch regression → revert the cutover |
| 2 — Privilege boundary (8–12 weeks) | `finnd` root daemon, polkit policy, logind-native power/lock, compositor root drop | Lock/power/backlight/Wi-Fi verified on the VM; `diagnose` still honest | Any auth bypass, lock bypass or hardware control failure → keep compositor root and iterate |
| 3 — Hardening (ongoing) | Per-app scopes, verified boot, hosted repo, real A/B power-cycle test | Production signing keys and a hardware lab exist | No key custody/hardware → keep the documented honest gap; do not fake verified boot |
| 4 — Optional language migration | Rust OTA/daemon binary if measurable gain | Parity with `tests/test_ota.py` and rollback E2E | Rollback or manifest-compat regression → stop |
| Full rewrite | Only after a product decision to abandon the Arch/pacman + Qt base | Budget for parity of both compat runtimes, UI widgets and OTA | No such decision → **stop here; incremental work is the answer** |

## 2.6 Only if a full rewrite is mandated

This is the minimum defensible stack, not a recommendation:

| Layer | Minimum viable choice | Unavoidable cost |
|---|---|---|
| Kernel/base | Keep Linux + systemd + pacman; a non-Linux kernel invalidates every compatibility runtime and all device work | Repo/infrastructure replacement |
| Compositor | Rust + smithay (or keep wlroots C); do not write a compositor from scratch | ~3k lines of scene/protocol parity, Xwayland, screencopy, lock |
| Shell/apps | Qt6 Widgets (or QML with a full widget rewrite) | 5k+ lines of Figma-exact controls and two Qt plugins |
| Session/authority | One Rust binary set: user broker + root `finnd`, typed IPC | Re-implement registry/compat launch plans |
| OTA/install | Keep A/B ext4 + signed manifest format; port logic | Rollback parity proof required |
| Compat | Keep or isolate; do not expand clean-room frameworks | SwiftUI/Metal/OHOS kits remain out of reach regardless |
| Testing/docs | Build CI and harnesses **before** code; port the doc guards | Without this, the rewrite repeats today's blind spots |

---

# Part 3 — Reference map

Foundational documents consulted (none modified):
`AGENTS.md`, `CONTRIBUTING.md`, `NAMING.md`, `docs/design/contract.md`,
`docs/architecture.md`, `docs/roadmap.md`, `docs/code-map.md`,
`docs/security.md`, `docs/testing.md`, `docs/session-protocol.md`,
`docs/ota.md`, `docs/repo.md`, `docs/development.md`,
`docs/audit/code-audit.md`, `docs/audit/remaining-issues.md`,
`CHANGELOG.md`, top-level `CMakeLists.txt` and `Makefile`.

Primary code evidence:
`src/comp/{server.cpp,server.hpp,main.cpp,wlroots.hpp,Makefile}`,
`src/session/{main.cpp,client.cpp,app_registry.cpp,Makefile}`,
`src/common/paths.hpp`, `src/ui/finn/*`, `src/ui/generated_tokens.hpp`,
`ui/_tokens.fui`, `tools/tokens.py`, `src/qtplugin/`, `src/shell/{popovers.cpp,dock.cpp,notifications.cpp}`,
`src/system/{sys_files.cpp,desktop_services.cpp}`,
`src/ota/{finn-update,finn_ota/*}`, `src/darwin/`, `src/ohos/`,
`src/compat/`, `apps/`, `vm/{deploy.sh,finn-install.sh}`,
`base/{mkfinniso,setup.sh,packages.x86_64,packages.aarch64,pacman.conf}`,
`scripts/lint.sh`, `tests/`, `tests/unit/`, `sdk/tests/`.

This document intentionally adds no new capability claim: every "implemented"
statement above is backed by a file in the tree, and every "unknown" is left
unknown pending hardware verification.
