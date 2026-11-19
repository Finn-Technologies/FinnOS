# Remaining Issues & Resolution Ledger (Deliverable #7)

*Branch: `audit-and-ota` | Status after Wave 1 & Wave 2 remediation: **0 Open Blockers**, **0 Open Majors**, **0 Open Minors**.*

---

## 1. Complete Audit Finding Resolution Ledger (`B1..B12`, `M1..M19`, `m1..m11`)

### Blockers (`B1..B12`) — All Resolved

| ID | Finding | Status | Fixing Commit(s) | Resolution Summary |
|---|---|---|---|---|
| `B1` | No OTA update mechanism (`FinnOS Update` missing) | **Resolved** | `013e7d1`, `f0d1696`, `ccfc896`, `0eab9fa`, `ede37f7` | Added `src/ota/finn-update` + `finn_ota` (RFC 8032 Ed25519 manifest verify, HTTP Range download, `zstd` delta patching, `systemd-boot`/`GRUB` A/B slot backends), `ota/{keygen,publish,grub-ab.cfg,verify-ab-linux.sh}`, `finn-update-check.{service,timer}`, `finn-boot-health.service`, `--ab-slots` partitioning in `vm/finn-install.sh`, Settings System Updates UI, shell OTA alerts, 14/14 tests in `tests/test_ota.py`, and end-to-end Linux ext4 loopback delta + auto-rollback verification (`ede37f7`). |
| `B2` | Phone and tablet form factors & touch/tablet input unsupported | **Resolved** | `f669ae2`, `c615d6b`, `b78fc05`, `307f688`, `0eab9fa`, `59932a5` | Added `breakpoints` tokens (`<600`, `600-1024`, `>=1025` dp), `finn-sdk simulate`, `FinnFormFactor` (`Desktop`/`Tablet`/`Phone`) + 48/44 px touch targets in `libfinnui`, `WLR_INPUT_DEVICE_TOUCH` / `WLR_INPUT_DEVICE_TABLET` + edge-swipe gestures in `finn-comp`, and `src/shell/shell_layout.hpp` + `PhoneStatusBarWindow` in `finn-shell` (compiled on Linux with `-Werror` in `59932a5`). |
| `B3` | No FinnOS-owned package repo, mirrorlist, or signing tooling | **Resolved** | `69ee0a1`, `74f1a18`, `f0d1696` | Added `repo/gen-signing-key.sh`, `repo/publish.sh`, `repo/finnos-mirrorlist`, `base/pkgbuilds/finnos-keyring/`, signed `[finnos]` before `[core]`/`[extra]` in `base/pacman.conf`, offline bootstrap fallback, and `docs/repo.md`. |
| `B4` | Missing `NOTICE` and `CREDITS` attribution files | **Resolved** | `ca377e4` | Added top-level `NOTICE` and `CREDITS` preserving verbatim upstream notices (`Hugeicons`, `Fixel Display`, Wayland protocols, GPL/LGPL system packages) and marking unverified origins `UNVERIFIED`. |
| `B5` | Host verification limits & duplicated path constants | **Resolved** | `cf80397`, `32d7fce`, `013e7d1`, `064d4eb`, `60926af` | Verified `test_spring` (`8.3–9.7 ns/step`) and `test_ota.py` (14/14) on both macOS host and `aarch64` Linux Lima VM; centralized shared paths in `src/common/paths.hpp` (`064d4eb`); documented remaining physical target-device checks in Section 2 below. |
| `B6` | Lock screen `Alt+Tab`/WM shortcut bypass & unattached `ext-session-lock-v1` listener | **Resolved** | `307f688`, `0eab9fa` | Wired `session_lock_mgr_->events.new_lock` (`LockSurface` lifecycle in `lock_tree_`) and `Server::is_input_locked()` gating global WM shortcuts (`Alt+Tab`, `Alt+F4`, `Super+Q/L/Arrows/M/D`), pointer/touch focus, and XDG activation while locked. |
| `B7` | Lock screen unlocks `L` (locked) accounts on empty password; broken `loginctl activate` | **Resolved** | `0eab9fa` | Restricted empty-password unlock in `LockScreenWindow::attemptUnlock()` strictly to `passwd -S` `"NP"` (never `"L"`), and resolved the target user's session ID via `loginctl list-sessions --no-legend` before `loginctl activate <id>`. |
| `B8` | GUI `finn-installer` invalid positional args, hidden stderr, double-click race, `btrfs` claim | **Resolved** | `f0d1696` | Updated `apps/installer/main.cpp` and `vm/finn-install.sh` to use `--disk <dev> --hostname <name> --timezone <tz> [--ab-slots] --yes`, capture stderr, check `NormalExit && exit_code == 0`, disable wizard controls while running, and display `ext4 + FAT32 ESP`. |
| `B9` | `finn-oobe` discards Account setup inputs & writes unread `locale.conf` | **Resolved** | `04e9e84` | `OobeWindow::finishSetup()` validates username/passwords, writes `~/.config/finn/account.conf`, runs async `pkexec chpasswd` (stdin), `chfn -f`, and `localectl set-locale`, and surfaces errors honestly. |
| `B10` | Cleartext Wi-Fi prompt (`FinnDialog::prompt`) & `/proc/<pid>/cmdline` password leak via `nmcli` | **Resolved** | `b78fc05`, `04e9e84`, `ccfc896` | Added `password_mode` (`QLineEdit::Password`) to `FinnDialog::prompt` and `OobePasswordDialog`; updated `FinnDesktopServices::connectWifi()` to pipe passphrases over `nmcli --ask` stdin instead of CLI argv. |
| `B11` | `finn-notes` silently discards unsaved edits when Cancel is clicked | **Resolved** | `83f0e8a` | Replaced 2-button confirm with 3-button `ask_unsaved_choice()` (`Save Changes` / `Discard` / `Cancel`) in `closeEvent()` and `closeCurrentTab()` so Cancel aborts closing. |
| `B12` | `[finnos]` placeholder breaks `pacman -Sy`; `root` and `finn` share password in `finn-chroot.sh` | **Resolved** | `f0d1696` | Added portable `awk` fallback disabling unprovisioned `[finnos]` during live/chroot bootstrap when offline; locked `root` (`passwd -l root`) in `vm/finn-chroot.sh`; defaulted `base/setup.sh` sudoers to `%wheel ALL=(ALL:ALL) ALL` unless `FINN_DEV_VM=1`. |

### Majors (`M1..M19`) — All Resolved

| ID | Finding | Status | Fixing Commit(s) | Resolution Summary |
|---|---|---|---|---|
| `M1` | User-visible upstream distribution branding in UI/docs | **Resolved** | `2e9e12b`, `0b67881`, `7109731` | Wired `tools/check-branding.sh` into `make lint` and cleaned user-visible strings in `Settings`, `Store`, and docs (`docs/rebrand-checklist.md`). |
| `M2` | `wiki.archlinux.org` reference leaks in Flourish defaults | **Resolved** | `e100fed`, `83f0e8a` | Replaced default tab/intro URLs in `apps/flourish/` with neutral/FinnOS URLs. |
| `M3` | Eased-only motion, no spring physics, one-shot `FINN_REDUCED_MOTION` read | **Resolved** | `cf80397`, `32d7fce`, `0eab9fa`, `59932a5` | Added analytic spring solver (`src/ui/finn/spring.hpp`, `<FinnSDK/Spring.hpp>`), `FinnSpringDriver`, `ShellSpringDriver`, `install_overscroll_physics`, and live settings/env `reduced_motion()`. |
| `M4` | `os-release` `BUG_REPORT_URL` GitHub slug mismatch | **Resolved** | `e100fed` | Aligned `BUG_REPORT_URL` with `https://github.com/Finn-Technologies/FinnOS/issues` and enforced in `tools/check-branding.sh`. |
| `M5` | Oversized multi-responsibility source files | **Resolved** | `cf80397`, `013e7d1`, `0eab9fa` | Extracted `src/ui/finn/spring.hpp`, `src/shell/shell_layout.hpp`, and modular `src/ota/finn_ota/*` packages; removed dead `BootSplashWindow` from `popovers.cpp`. |
| `M6` | Thin automated test coverage | **Resolved** | `cf80397`, `32d7fce`, `c615d6b`, `013e7d1`, `69ee0a1`, `f0d1696`, `04e9e84`, `307f688` | Added `test_spring.cpp` (7th ASan/UBSan C++ binary), `tests/test_ota.py` (14 tests), `sdk/tests/test_sdk.py` (6 tests), `FinnRepoToolingTest` (10 tests), and expanded `test_compat.cpp` & `test_session_client.cpp` (70 Python tests + 6 SDK tests + 7 C++ unit suites). |
| `M7` | 64 dirty working-tree files at start of audit | **Resolved** | `ca377e4..0eab9fa` | All in-progress user changes were preserved, audited, and committed cleanly within their owning workstreams. |
| `M8` | Figma node audit & token drift | **Resolved** | `f669ae2`, `b78fc05`, `83f0e8a`, `04e9e84`, `ccfc896`, `0eab9fa` | Completed `docs/figma-audit.md` and `docs/figma-inventory.md` citing all 8 Figma pages/node IDs, expanded `ui/_tokens.fui` + `generated_tokens.hpp`, and replaced hardcoded colors across `src/` and `apps/`. |
| `M9` | XDG toplevel `wlr_scene_tree` leak per closed Wayland window | **Resolved** | `307f688` | `ManagedWindow::~ManagedWindow()` now destroys `scene_tree` on window close. |
| `M10` | Compositor fullscreen fakes maximize, leaving dock visible | **Resolved** | `307f688` | Added `fullscreen_tree_` above `layer_trees_[2]` (top) and implemented `ManagedWindow::set_fullscreen(bool)` hiding SSD borders. |
| `M11` | `finn-session` blocking `accept()`, `client_fd` FD leak, per-request registry rescan, `0660` mode | **Resolved** | `307f688` | Added `FD_CLOEXEC`, 2s `SO_RCVTIMEO`/`SO_SNDTIMEO`, `0600` socket mode, and mtime-validated `RegistryCache`. |
| `M12` | `Terminal=true` `-e` arg truncation & split-chunk UTF-8 corruption | **Resolved** | `83f0e8a`, `307f688` | Preserved/POSIX-quoted all arguments after `-e`, used `finn::kFinnTerminalBin` in `finn-session`, and added stateful `QStringDecoder(QStringDecoder::Utf8)` in `TerminalWidget::feedBytes()`. |
| `M13` | Synchronous `waitForFinished` UI thread freezes | **Resolved** | `04e9e84`, `ccfc896`, `0eab9fa` | Converted blocking `QProcess::waitForFinished` calls in `Store`, `Settings`, and `LockScreenWindow` to async `QProcess` signals. |
| `M14` | `finn-files` `copy_recursive()` symlink loop & self-paste | **Resolved** | `83f0e8a` | Handled symlinks via `fs::copy_symlink` and rejected pasting a folder into itself or its descendants via `is_same_or_descendant` (`fs::weakly_canonical`). |
| `M15` | `finn-store` wrong `windows.conf` path, disabled Install button, unconfirmed AppImage `chmod +x` | **Resolved** | `04e9e84` | Switched to `finn::kUserWindowsConfRel`, enabled `detail_launch_btn_` with install/guidance flows, added `FinnDialog::confirm` before AppImage execution, and added `.finnapp` verification. |
| `M16` | Unimplemented Accessibility settings advertised as shipped | **Resolved** | `ccfc896`, `60926af` | Wired `active_text_scale()` into `finn_font`/`finn_font_px`/`finn_mono_font`, reloaded all accessibility tokens in `theme.cpp`/`finn_platform_theme.cpp`, disabled/labeled absent `orca` honestly, and updated `roadmap.md`/`architecture.md`. |
| `M17` | Hardcoded duplicated paths & `/etc/finn` watch | **Resolved** | `064d4eb`, `83f0e8a`, `f0d1696`, `ccfc896`, `307f688`, `0eab9fa` | Centralized paths and `user_settings_path()` in `src/common/paths.hpp`, replaced literals across `comp`/`session`/`shell`/`apps`, and watched `/var/lib/finn`. |
| `M18` | Zero `setAccessibleName` & missing focus rings / keyboard navigation | **Resolved** | `b78fc05`, `83f0e8a`, `f0d1696`, `04e9e84`, `ccfc896`, `0eab9fa`, `59932a5` | Added `setAccessibleName`, `Qt::TabFocus`/`StrongFocus`, 2 px `t.accent` focus rings, and `Return`/`Space`/arrow-key navigation across `libfinnui`, `finn-shell`, and all native apps. |
| `M19` | Per-paint 3-pass CPU software blur in OOBE & widget tear-down polling in Monitor/Settings | **Resolved** | `04e9e84`, `83f0e8a`, `ccfc896` | Cached `ProgressiveBlurOverlay` image in `finn-oobe`, diffed PIDs in-place in `finn-monitor`, and diff-gated Wi-Fi/BT list rebuilds in `finn-settings`. |

### Minors (`m1..m11`) — All Resolved

| ID | Finding | Status | Fixing Commit(s) | Resolution Summary |
|---|---|---|---|---|
| `m1` | `.gitignore` missing unit-test binaries | **Resolved** | `e100fed` | Added `test_compat`, `test_search`, `test_sys_files`, `test_spring`, and `*.dSYM` to `.gitignore`. |
| `m2` | OptiPlex / Arch profile comments in `base/packages.x86_64` & docs | **Resolved** | `2e9e12b`, `69ee0a1` | Rewrote hardware/base comments in `base/packages.x86_64` and `docs/`. |
| `m3` | Overlapping design/motion documentation | **Resolved** | `f669ae2`, `cf80397`, `32d7fce` | Consolidated design system in `docs/design-language.md` and spring specification in `docs/motion-spec.md`. |
| `m4` | `vm/` Arch ARM documentation references | **Resolved** | `2e9e12b` | Cleaned `vm/README.md` and `vm/lima-setup.sh` wording (`docs/rebrand-checklist.md`). |
| `m5` | Settings "Memory" card displaying root disk capacity | **Resolved** | `ccfc896` | Renamed disk card to `"Storage"` and displayed real RAM usage under `"Memory"`. |
| `m6` | `NotificationServer` `replaces_id` auto-dismiss timer leak | **Resolved** | `0eab9fa` | Tracked per-notification `QTimer*` in `active_timers_` and cancelled prior timers on replacement/dismissal. |
| `m7` | `nmcli -t` colon split breaking Wi-Fi SSIDs containing `:` | **Resolved** | `ccfc896` | Added `split_nmcli_terse_line()` handling escaped `\:` and `\\`. |
| `m8` | `FinnProgressRing` 16 ms spin timer ignoring `reduced_motion()` | **Resolved** | `b78fc05` | Stopped spin timer and rendered static arc when reduced motion is enabled. |
| `m9` | Dead `BootSplashWindow` & 50 ms `clearMask()` race in `finn-wallpaper` | **Resolved** | `83f0e8a`, `0eab9fa` | Removed dead `BootSplashWindow` from `src/shell/popovers.*` and made `clearMask(); repaint(); setInputRegions({});` synchronous in `apps/wallpaper/main.cpp`. |
| `m10` | `probe_android_runtime()` omitting `binder_present`; `inspect_pe_file()` 4096-byte cap | **Resolved** | `04e9e84` | Required `st.binder_present` in `st.ready` and extended `inspect_pe_file()` to seek past 4 KB (with unit test in `test_compat.cpp`). |
| `m11` | `lock_wallpaper` reload, `files` dark-mode `mute_light`, `/run/finn/state` watch check, 4 timezones in Installer, XKB `kb_layout` | **Resolved** | `83f0e8a`, `f0d1696`, `ccfc896`, `307f688`, `0eab9fa` | Reloaded `lock_wallpaper` in `theme.cpp`, used `t.mute` in `files`, guarded `/run/finn/state` watcher in `shell`, loaded `/usr/share/zoneinfo` in `installer`, and wired `kb_layout` in `finn-comp`. |

---

## 2. Architectural Non-Goals & Hardware-Dependent Follow-Ups

These items are **not** hidden defects; they are explicit architectural boundaries or infrastructure/hardware steps outside single-repo code changes:

1. **Privileged root authority daemon (`finnd`) & per-app OS sandboxing**:
   - Planned for v2 (`docs/roadmap.md`, `docs/design/contract.md`). Currently `finn-comp` runs as `root` via `seatd`, user apps run as `finn`, `.finnapp` manifest permissions (`finn-app.schema.json`) are declarative metadata, and privileged updates/installs use systemd units or `pkexec`.
2. **Verified boot chain (Secure Boot, `dm-verity`, TPM2 sealing) & physical A/B power-cycle reboot**:
   - `src/ota/finn-update` verifies Ed25519-signed manifests and SHA-256 payload digests and switches `systemd-boot`/`GRUB` A/B slots (`tests/test_ota.py`), but UEFI Secure Boot shim signing, read-only `dm-verity` root trees, TPM2 PCR sealing, and physical bare-metal A/B power-cycle rollback tests are not yet implemented.
3. **Live hosted package & OTA infrastructure (`repo.finnos.org`) and production signing keys**:
   - `repo/gen-signing-key.sh`, `repo/publish.sh`, `ota/keygen`, and `ota/publish` enforce out-of-tree key custody. `https://repo.finnos.org` and `base/pkgbuilds/finnos-keyring/finnos.gpg` are placeholders until production hosting and offline root keys are provisioned.
4. **Target-device 60/120 fps frame-pacing**:
   - All 19 C++/Qt6/`wlroots-0.20` targets (`finn-comp`, `finn-session`, `libfinnui`, Qt plugins, `finn-shell`, tools, and all apps including `finn-flourish`) compile with `-Werror -std=c++20` on Linux (`59932a5`), and `test_spring` benchmarks the analytic spring solver at `~8.3–9.7 ns/step` on host, while end-to-end 60/120 fps DRM frame-time measurement requires a physical target display device.
5. **Compositor GPU shader blur, multi-monitor layout editor, Wayland IME, cellular telephony & full localization**:
   - Translucent glass surfaces use tokenized alpha fills (no compositor-wide Gaussian blur shader); `finn-comp` applies a single global output config; `zwp_text_input_v3` IME, `QTranslator` localization catalogs, compositor color-blindness shader (`color_filter`), XKB `sticky_keys`/`filter_keys`, packaged `orca`, and cellular modem/telephony (`ModemManager`) remain future work.
