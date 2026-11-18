# FinnOS Phase 1 Code Audit

*Audited on branch `audit-and-ota` (host macOS, read-only static inspection; continuing IDs from the first-pass audit `B1..B5`, `M1..M8`, `m1..m4`).*

## Continuity With First-Pass Findings (`B1..B5`, `M1..M8`, `m1..m4`) & Full Resolution Table

| ID | Summary | Status | Fixing Commit(s) |
|---|---|---|---|
| `B1` | No OTA update mechanism (`FinnOS Update` unimplemented) | **Resolved** | `013e7d1`, `f0d1696`, `ccfc896`, `0eab9fa`, `ede37f7` |
| `B2` | Phone and tablet form factors & touch/tablet input unsupported | **Resolved** | `f669ae2`, `c615d6b`, `b78fc05`, `307f688`, `0eab9fa`, `59932a5` |
| `B3` | `[finnos]` repo URL `https://repo.finnos.org` placeholder / no repo tooling | **Resolved** | `69ee0a1`, `74f1a18`, `f0d1696` |
| `B4` | Missing `NOTICE` and `CREDITS` legal attribution files | **Resolved** | `ca377e4` |
| `B5` | Host verification limits & hardcoded path constants | **Resolved** | `cf80397`, `32d7fce`, `013e7d1`, `064d4eb`, `60926af` |
| `B6` | Lock screen `Alt+Tab`/WM shortcut bypass & unattached `ext-session-lock-v1` listener | **Resolved** | `307f688`, `0eab9fa` |
| `B7` | Lock screen unlocks `L` (locked) accounts on empty password & broken `loginctl activate` | **Resolved** | `0eab9fa` |
| `B8` | GUI `finn-installer` invalid positional args, hidden stderr, double-click race, `btrfs` claim | **Resolved** | `f0d1696` |
| `B9` | `finn-oobe` discards Account setup inputs & writes unread `locale.conf` | **Resolved** | `04e9e84` |
| `B10` | Plaintext Wi-Fi prompt (`FinnDialog::prompt`) & `/proc/<pid>/cmdline` password leak via `nmcli` | **Resolved** | `b78fc05`, `04e9e84`, `ccfc896` |
| `B11` | `finn-notes` silently discards unsaved edits on Cancel | **Resolved** | `83f0e8a` |
| `B12` | `[finnos]` `SigLevel = Required` offline failure & `root`/`finn` shared password | **Resolved** | `f0d1696` |
| `M1` | User-visible upstream distribution branding in UI/docs | **Resolved** | `2e9e12b`, `0b67881`, `7109731` |
| `M2` | `wiki.archlinux.org` reference leaks in Flourish defaults | **Resolved** | `e100fed`, `83f0e8a` |
| `M3` | Eased-only motion, no spring physics, one-shot `FINN_REDUCED_MOTION` read | **Resolved** | `cf80397`, `32d7fce`, `0eab9fa`, `59932a5` |
| `M4` | `os-release` `BUG_REPORT_URL` GitHub slug mismatch | **Resolved** | `e100fed` |
| `M5` | Oversized multi-responsibility source files | **Resolved** | `cf80397`, `013e7d1`, `0eab9fa` |
| `M6` | Thin automated test coverage | **Resolved** | `cf80397`, `32d7fce`, `c615d6b`, `013e7d1`, `69ee0a1`, `f0d1696`, `04e9e84`, `307f688` |
| `M7` | 64 dirty working-tree files at start of audit | **Resolved** | `ca377e4..0eab9fa` |
| `M8` | Figma node audit & token drift | **Resolved** | `f669ae2`, `b78fc05`, `83f0e8a`, `04e9e84`, `ccfc896`, `0eab9fa` |
| `M9` | XDG toplevel `wlr_scene_tree` leak per closed Wayland window | **Resolved** | `307f688` |
| `M10` | Compositor fullscreen fakes maximize, leaving dock visible | **Resolved** | `307f688` |
| `M11` | `finn-session` blocking `accept()`, `client_fd` FD leak, per-request registry rescan, `0660` mode | **Resolved** | `307f688` |
| `M12` | `Terminal=true` `-e` arg truncation & split-chunk UTF-8 corruption | **Resolved** | `83f0e8a`, `307f688` |
| `M13` | Synchronous `waitForFinished` UI thread freezes | **Resolved** | `04e9e84`, `ccfc896`, `0eab9fa` |
| `M14` | `finn-files` `copy_recursive()` symlink loop & self-paste | **Resolved** | `83f0e8a` |
| `M15` | `finn-store` wrong `windows.conf` path, disabled Install button, unconfirmed AppImage `chmod +x` | **Resolved** | `04e9e84` |
| `M16` | Unimplemented Accessibility settings advertised as shipped | **Resolved** | `ccfc896`, `60926af` |
| `M17` | Hardcoded duplicated paths & `/etc/finn` watch | **Resolved** | `064d4eb`, `83f0e8a`, `f0d1696`, `ccfc896`, `307f688`, `0eab9fa` |
| `M18` | Zero `setAccessibleName` & missing focus rings / keyboard navigation | **Resolved** | `b78fc05`, `83f0e8a`, `f0d1696`, `04e9e84`, `ccfc896`, `0eab9fa`, `59932a5` |
| `M19` | Per-paint 3-pass CPU software blur in OOBE & widget tear-down polling in Monitor/Settings | **Resolved** | `04e9e84`, `83f0e8a`, `ccfc896` |
| `m1` | `.gitignore` missing unit-test binaries | **Resolved** | `e100fed` |
| `m2` | OptiPlex / Arch profile comments in `base/packages.x86_64` & docs | **Resolved** | `2e9e12b`, `69ee0a1` |
| `m3` | Overlapping design/motion documentation | **Resolved** | `f669ae2`, `cf80397`, `32d7fce` |
| `m4` | `vm/` Arch ARM documentation references | **Resolved** | `2e9e12b` |
| `m5` | Settings "Memory" card displaying root disk capacity | **Resolved** | `ccfc896` |
| `m6` | `NotificationServer` `replaces_id` auto-dismiss timer leak | **Resolved** | `0eab9fa` |
| `m7` | `nmcli -t` colon split breaking Wi-Fi SSIDs containing `:` | **Resolved** | `ccfc896` |
| `m8` | `FinnProgressRing` 16 ms spin timer ignoring `reduced_motion()` | **Resolved** | `b78fc05` |
| `m9` | Dead `BootSplashWindow` & 50 ms `clearMask()` race in `finn-wallpaper` | **Resolved** | `83f0e8a`, `0eab9fa` |
| `m10` | `probe_android_runtime()` omitting `binder_present`; `inspect_pe_file()` 4096-byte cap | **Resolved** | `04e9e84` |
| `m11` | `lock_wallpaper` reload, `files` dark-mode `mute_light`, `/run/finn/state` watch check, 4 timezones in Installer, XKB `kb_layout` | **Resolved** | `83f0e8a`, `f0d1696`, `ccfc896`, `307f688`, `0eab9fa` |

---

## Blockers (`B6..B12`)

### `B6` — Lock screen `Alt+Tab` / WM shortcut bypass & unattached `ext-session-lock-v1` listener
- **Severity**: Blocker (Authentication / Screen Lock Bypass)
- **Where**: `src/comp/server.cpp:655-705, 1313`; `src/shell/popovers.cpp:1916`
- **Evidence**:
  - `src/comp/server.cpp:1313` creates `session_lock_mgr_ = wlr_session_lock_manager_v1_create(display_)` and `lock_tree_` (`server.cpp:1288`), but attaches **no listener** to `session_lock_mgr_->events.new_lock`. Any Wayland session-lock client is ignored.
  - Meanwhile, `LockScreenWindow` (`src/shell/popovers.cpp:1916`) is a `FinnLayerWindow` overlay. In `Server::on_new_input` (`src/comp/server.cpp:655-705`), global compositor shortcuts — `Alt+Tab` (`cycle_focus` -> `focus_window` -> `wlr_seat_keyboard_notify_enter`), `Alt+F4`/`Super+Q` (`focused_window_->close()`), `Super+D` (`minimize_all_toggle()`), and `Super+Arrows/M` — execute **before** forwarding keys to the focused overlay surface.
- **Impact**: Pressing `Alt+Tab` while the screen is locked moves keyboard focus onto underlying app windows and allows closing/minimizing/controlling windows behind the lock screen; `Super+Q`/`Alt+F4` can close windows behind the lock.

### `B7` — Lock screen unlocks locked (`L`) accounts on empty password & broken user-switching
- **Severity**: Blocker (Authentication Bypass)
- **Where**: `src/shell/popovers.cpp:2149-2209`
- **Evidence**:
  - In `LockScreenWindow::attemptUnlock()` (`src/shell/popovers.cpp:2188-2197`), when an empty password is submitted, it executes `passwd -S <user>` and unlocks immediately if output contains `" NP "` **or `" L "`**:
    `if (out.contains(QStringLiteral(" NP ")) || out.contains(QStringLiteral(" L ")))`
  - In `passwd(1)`, `L` means the account password is **locked** (`vm/deploy.sh:77` explicitly runs `passwd -l finn`), not passwordless (`NP`). Pressing Enter unlocks a locked account with no password.
  - On user switch (`src/shell/popovers.cpp:2155-2156`), after authenticating via `su`, it runs `QProcess::startDetached("loginctl", {"activate"})` with **no session ID argument**, failing to switch VT/session while hiding the lock overlay.

### `B8` — GUI Installer (`finn-installer`) always fails on launch + double-click race + wrong FS claim
- **Severity**: Blocker (Broken OS Installer / Data-Destruction Risk)
- **Where**: `apps/installer/main.cpp:499, 569-570, 735-758`; `vm/finn-install.sh:143-167, 407, 455`
- **Evidence**:
  - `InstallerWindow::startInstall()` (`apps/installer/main.cpp:755-757`) invokes `pkexec /usr/local/bin/finn-install` with 3 positional arguments (`{installer, selected_disk_, hostname_edit_->text().trimmed(), tz_combo_->currentText()}`).
  - `/usr/local/bin/finn-install` execs `/opt/finnos/vm/finn-install.sh` (`base/airootfs/usr/local/bin/finn-install:9`), whose parser (`vm/finn-install.sh:143-167`) only accepts `--disk <device>`, `--yes`, `-h|--help` and exits immediately via `die "unknown argument: $1"` on positional arguments.
  - `startInstall()` connects `readyReadStandardOutput` (`750`) but **ignores `readyReadStandardError`** (so `die`'s stderr message is hidden), ignores exit status in `finished` (`753-754`), and **never disables the Install button** while `pkexec` runs, allowing concurrent double-click launches.
  - Step 1 (`apps/installer/main.cpp:499`) advertises `btrfs + FAT32 ESP` while `vm/finn-install.sh:407, 455` formats `ext4`.

### `B9` — OOBE Account Setup page collects credentials & discards them; locale config unread
- **Severity**: Blocker (Fake Capability / Security Expectation Mismatch)
- **Where**: `apps/oobe/main.cpp:1899-1940, 2186-2194, 2281-2335`
- **Evidence**:
  - `OobeWindow::buildAccountPage()` (`1899-1940`) presents `fullname_edit_`, `username_edit_`, `pw_edit_`, and `pw_verify_edit_`, and `advanceStep()` (`2186-2194`) validates password equality.
  - `OobeWindow::finishSetup()` (`2318-2335`) **never reads** `fullname_edit_`, `username_edit_`, `pw_edit_`, or `pw_verify_edit_`, never calls `passwd`/`chpasswd`/`useradd`/`usermod`, and closes after writing `~/.local/share/finn/oobe_complete`.
  - `applyLanguageSelection()` (`2305-2315`) writes `~/.config/finn/locale.conf` (unread by systemd/PAM/login) and `finishSetup()` (`2321-2324`) calls `localectl set-locale` via `startDetached` as unprivileged user `finn` without polkit/error feedback.

### `B10` — Plaintext Wi-Fi password exposed in visible prompt AND `/proc/<pid>/cmdline`
- **Severity**: Blocker (Credential Exposure)
- **Where**: `src/ui/finn/components.cpp:2120`; `apps/settings/main.cpp:1131-1136`; `apps/oobe/main.cpp:2147-2152`; `src/system/desktop_services.cpp:342-351`
- **Evidence**:
  - Both `SettingsWindow` (`apps/settings/main.cpp:1131`) and `OobeWindow` (`apps/oobe/main.cpp:2147`) prompt for Wi-Fi passphrases with `FinnDialog::prompt()`, which instantiates a plain `FinnTextField` (`src/ui/finn/components.cpp:2120`) without `QLineEdit::Password` echo masking.
  - `FinnDesktopServices::connectWifi()` (`src/system/desktop_services.cpp:345-350`) executes `QProcess::startDetached("nmcli", {"dev", "wifi", "connect", ssid, "password", password})`, exposing the Wi-Fi passphrase in `/proc/<pid>/cmdline` to any local user/process and returning no authentication error feedback.

### `B11` — Finn Notes silently discards unsaved edits on Cancel
- **Severity**: Blocker (Data Loss)
- **Where**: `apps/notes/main.cpp:122-137, 591-603`
- **Evidence**:
  - Both `closeEvent()` (`127-135`) and `closeCurrentTab()` (`596-602`) call the 2-button `FinnDialog::confirm(..., "Save Changes")` (`bool save_it`).
  - Only `if (save_it && !saveCurrent(false))` aborts closing (`e->ignore(); return;`). Clicking **Cancel** (`save_it == false`) falls straight through, closing the window/tab and destroying unsaved changes without saving or keeping the tab open.

### `B12` — Unprovisioned `[finnos]` repo with `SigLevel = Required` ahead of `[core]`/`[extra]` + dev/live account defaults
- **Severity**: Blocker (Package Sync Failure / Security Defaults)
- **Where**: `base/pacman.conf:64-66`; `vm/finn-install.sh:329`; `base/airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf:6`; `vm/finn-chroot.sh:125`
- **Evidence**:
  - `base/pacman.conf:64-66` defines `[finnos]` with `SigLevel = Required` and placeholder URL `Server = https://repo.finnos.org/$repo/os/$arch` before `[core]` and `[extra]`. When applied to an installed machine (`vm/finn-chroot.sh:97` / `base/setup.sh:53`), `pacman -Sy` / `pacman -Syu` fails on unprovisioned `repo.finnos.org` (also blocks `Store`'s `pacman -Qu` updates flow; noted alongside `B3`).
  - `vm/finn-chroot.sh:125` sets the exact same password for both `root` and `finn` (`printf 'root:%s\nfinn:%s\n' "$PW" "$PW" | chpasswd`). `base/setup.sh:83` installs passwordless `finn ALL=(ALL) NOPASSWD: ALL` (appropriate only for dev VM, dangerous if `setup.sh` is run on a user machine).

---

## Majors (`M9..M19`)

### `M9` — XDG toplevel scene-graph node leak on every closed Wayland window
- **Where**: `src/comp/server.cpp:762, 874-886` vs `src/comp/server.cpp:929`; `src/comp/window.cpp:31-60, 90-101`
- **Evidence**: `on_new_xdg_toplevel` allocates `w->scene_tree = wlr_scene_tree_create(server->toplevel_tree_)` (`762`) and 13 `ssd_*` `wlr_scene_rect`s (`window.cpp:90-101`). Xwayland `destroy_l` explicitly calls `if (w->scene_tree) wlr_scene_node_destroy(&w->scene_tree->node)` (`929`), whereas XDG `destroy_l` (`874-886`) and `ManagedWindow::~ManagedWindow()` (`window.cpp:31-60`) **never destroy `w->scene_tree`**, leaking scene nodes for every closed Wayland toplevel.

### `M10` — Compositor fullscreen fakes maximize, leaving top-layer dock visible
- **Where**: `src/comp/server.cpp:856-860`
- **Evidence**: `xdg_toplevel->events.request_fullscreen` calls `w->set_maximized(w->xdg_toplevel->requested.fullscreen)` instead of hiding SSD borders and stacking above `layer_trees_[2]` (`top` layer), so `finn-shell`'s dock continues covering the bottom pixels during fullscreen video/apps/games.

### `M11` — `finn-session` blocking `accept()`, `client_fd` FD leak to launched apps, per-request full registry scan, and `0660` socket mode drift
- **Where**: `src/session/main.cpp:181, 332, 483, 502, 510-519`; `docs/design/contract.md:48`
- **Evidence**:
  - `accept(server_fd, nullptr, nullptr)` (`512`) creates `client_fd` without `SOCK_CLOEXEC` and without `SO_RCVTIMEO`.
  - `handle_client(client_fd)` (`517`) runs synchronously on the main loop (`read_line` from a stalled local client blocks all app launches), calls `posix_spawnp` (`181`) while `client_fd` is still open without `FD_CLOEXEC` (leaking the socket fd into every launched app), and re-runs `AppRegistry::load_all` (`332`) on every command (including `POWER.ACTION`).
  - `::chmod(sock_path.c_str(), 0660)` (`502`) differs from `docs/design/contract.md:48` (`0600 finn`).

### `M12` — Terminal `.desktop` argument truncation & split-chunk UTF-8 corruption
- **Where**: `src/session/main.cpp:217-219`; `apps/terminal/main.cpp:438, 605-607, 1028-1030`
- **Evidence**:
  - `spawn_app` wraps `Terminal=true` `.desktop` entries as `{"/usr/local/bin/finn-terminal", "-e", argv[0], argv[1], ...}` (`src/session/main.cpp:217-219`), while `finn-terminal` (`apps/terminal/main.cpp:1028-1030`) parses only `argv[++i]` after `-e` and passes `exec_cmd` as a single string, silently dropping all remaining arguments.
  - `TerminalWidget::feedBytes()` (`606-607`) calls `QString::fromUtf8(QByteArray(data, len))` independently on each PTY `read()` chunk (`438`), corrupting multi-byte UTF-8 sequences split across buffer boundaries.

### `M13` — Synchronous `waitForFinished`/`popen`/D-Bus calls on UI threads + 3-second process-spawning poll
- **Where**: `src/system/desktop_services.cpp:39-43, 86-286`; `apps/store/main.cpp:848-863, 890`; `src/compat/android_runner.cpp:61`; `src/shell/popovers.cpp:2154, 2174, 2190, 2202`; `apps/settings/main.cpp:77, 919`; `apps/installer/main.cpp:212, 248, 673`
- **Evidence**:
  - `FinnDesktopServices` (`desktop_services.cpp:39-43`) runs a 3s `QTimer` spawning up to 6 external binaries (`wpctl`, `nmcli` x3, `bluetoothctl` x2) plus synchronous `QDBusInterface` calls (`269`) in the shell/apps process.
  - `StoreWindow` ctor -> `reloadCatalogue()` -> `refreshUpdatesList()` runs blocking `pacman -Qu` (`waitForFinished(2500)`) and `flatpak remote-ls --updates` (`waitForFinished(2500)`) = up to 5s startup UI freeze, plus synchronous `popen("waydroid app list 2>/dev/null", "r")` (`android_runner.cpp:61`) and `finn-sdk new` `waitForFinished(5000)` (`store/main.cpp:890`).
  - `LockScreenWindow::attemptUnlock()` blocks `finn-shell` (and therefore Dock & Notifications) with `waitForFinished(5000)` / `waitForFinished(1000)` (`popovers.cpp:2154, 2174, 2190, 2202`).
  - `SettingsWindow` blocks on `lscpu` (`1200ms`) and `pacman -Qu` (`4000ms`); `InstallerWindow` blocks on `lscpu` (`1200ms`) and `lsblk` (`2500ms`, `2000ms`).

### `M14` — Finn Files `copy_recursive()` blocks UI thread, loops on directory symlinks, and allows self-paste
- **Where**: `apps/files/main.cpp:62-76, 927-937`
- **Evidence**: `copy_recursive(const QFileInfo& src, const QString& dst_path)` runs synchronously on the GUI thread, treats symlinked directories as `src.isDir() == true` (`63`) without checking `isSymLink()` (infinite recursion / cycle), and `pasteClipboard()` (`919-937`) has no check preventing pasting a parent directory into its own descendant (`dst` inside `src`), causing recursive self-copy until ENAMETOOLONG / disk exhaustion.

### `M15` — Finn Store reads wrong `windows.conf` path, disables Install button for native catalogue apps, and `chmod +x` runs AppImages
- **Where**: `apps/store/main.cpp:429, 659-660, 723`; `src/common/paths.hpp:31`
- **Evidence**:
  - `StoreWindow` reads `~/.config/finn/windows.conf` (`apps/store/main.cpp:723`) while `AppRegistry` uses `kUserWindowsConfRel` (`".local/share/finn/windows.conf"`, `src/common/paths.hpp:31`), so pinned Windows shortcuts diverge from the launcher.
  - On the app detail page, `detail_launch_btn_->setEnabled(app.installed)` (`429`) disables the `"Install"` button whenever an app is not installed.
  - The AppImage runner (`659-660`) unconditionally sets `QFile::ExeUser` (`chmod +x`) on user-picked files and runs them with `QProcess::startDetached`.

### `M16` — Unimplemented Accessibility settings advertised as shipped (`text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, unpackaged `orca`)
- **Where**: `apps/settings/main.cpp:722, 745-790`; `src/ui/finn/theme.cpp:181-188`; `src/qtplugin/finn_platform_theme.cpp:190-193`; `base/packages.x86_64`; `docs/roadmap.md:20-21`
- **Evidence**:
  - `SettingsWindow` persists `text_scale`, `color_filter`, `sticky_keys`, and `filter_keys` via `FinnThemeManager`, and `docs/roadmap.md:20-21` claims a `"full Accessibility suite (Orca Screen Reader, High Contrast, Color Filters, Text Scale, Reduced Motion, Sticky/Filter Keys, Pointer Size)"`.
  - None of `text_scale`, `color_filter`, `sticky_keys`, or `filter_keys` are read or applied in `src/comp/` (compositor shader/XKB), `src/ui/finn/tokens.hpp` (`finn_font`/`finn_font_px` ignore `text_scale`), or `src/qtplugin/finn_platform_theme.cpp:191-193` (doesn't even reload them).
  - `SettingsWindow` (`722`) spawns `orca`, which is **not** in `base/packages.x86_64` or `base/packages.aarch64`.

### `M17` — Hardcoded duplicated paths & diverging theme/settings watches ("one copy of a fact" violations)
- **Where**: `src/comp/server.cpp:95, 115, 135, 163, 248`; `src/session/main.cpp:217`; `apps/installer/main.cpp:741`; `apps/wallpaper/main.cpp:77`; `src/shell/popovers.cpp:2220`; `apps/viewer/main.cpp:558, 563, 569`; `src/ui/finn/theme.cpp:151`; `src/qtplugin/finn_platform_theme.cpp:115`
- **Evidence**:
  - `src/comp/server.cpp` duplicates `"/home/finn/.config/finn/settings"` 5 times instead of sharing `kUserSettingsRel` / resolving `$HOME`.
  - `/usr/local/bin/finn-terminal` (`src/session/main.cpp:217`), `/usr/local/bin/finn-install` (`apps/installer/main.cpp:741`), `/usr/share/finn/wallpaper/default.webp` (`apps/wallpaper/main.cpp:77`, `src/shell/popovers.cpp:2220`), and `/usr/share/finn/{wallpaper,icons,pointers}` (`apps/viewer/main.cpp:558-569`) are hardcoded outside `src/common/paths.hpp`.
  - Both `FinnThemeManager` (`theme.cpp:151`) and `FinnPlatformTheme` (`finn_platform_theme.cpp:115`) watch `/etc/finn`, even though `kSettingsPath` is `/etc/finn/settings` and `/etc/finn` is not created by `finn-tmpfiles.conf` or `vm/finn-chroot.sh`.

### `M18` — Zero accessibility names & missing focus/keyboard navigation across custom UI controls
- **Where**: `src/ui/finn/components.cpp`, `src/shell/{dock.cpp,popovers.cpp}`, `apps/{oobe,files,flourish}/`
- **Evidence**: `grep -rnE "setAccessible|QAccessible"` = 0 matches; `grep -rnE "hasFocus|focusInEvent"` = 0 matches; `grep -rn "setFocusPolicy"` = 1 match (`apps/terminal/main.cpp:99`). Custom-painted interactive widgets and shell popovers cannot be driven by keyboard or identified by assistive technology. (See `docs/audit/ui-audit.md`.)

### `M19` — Per-paint CPU software blur in OOBE & full widget-tree tear-down polling in Monitor/Settings
- **Where**: `apps/oobe/main.cpp:906-962`; `apps/monitor/main.cpp:195-198`; `apps/settings/main.cpp:1118-1167`
- **Evidence**:
  - `OobeWindow`'s `ProgressiveBlurOverlay::paintEvent` (`apps/oobe/main.cpp:906-962`) captures `target_->render()` and executes three sequential CPU `box_blur_image` passes (`radius 2, 4, 6`) on every repaint without caching.
  - `SettingsWindow` deletes and reconstructs every Wi-Fi and Bluetooth row widget every 3 seconds (`1118-1167`). `MonitorWindow` polls every 1.2s (`195-198`), rebuilding process tree rows on the UI thread.

---

## Minors (`m5..m11`)

### `m5` — Settings "Memory" stat card displays root filesystem disk capacity instead of RAM
- **Where**: `apps/settings/main.cpp:1026-1042`
- **Evidence**: `QStorageInfo::root()` (`1026`) populates `disk_primary`/`disk_suffix` and is passed into `FinnStatCard("Memory", disk_primary, disk_suffix, page)` (`1039-1040`).

### `m6` — `NotificationServer` `replaces_id` leaks prior auto-dismiss timer
- **Where**: `src/shell/notifications.cpp:354-382`
- **Evidence**: Updating an active notification via `replaces_id > 0` (`354`) replaces the entry in `active_popups_` and starts a new `QTimer::singleShot` (`380`), without cancelling the old popup's timer — causing the updated notification to vanish early when the original timer fires.

### `m7` — `nmcli -t` colon split corrupts Wi-Fi SSIDs containing colons
- **Where**: `src/system/desktop_services.cpp:188-194`
- **Evidence**: `line.split(QLatin1Char(':'))` splits escaped `nmcli -t` fields naively on unescaped `:`, misparsing any SSID containing `:` (`\:`).

### `m8` — `FinnProgressRing` 60 Hz spin timer ignores `reduced_motion()`
- **Where**: `src/ui/finn/components.cpp:982-999`
- **Evidence**: `spin_timer_` unconditionally ticks every `16ms` (`angle_deg_ = (angle_deg_ + 9) % 360`) without checking `finn::ui::reduced_motion()`.

### `m9` — Dead `BootSplashWindow` code & 1s wallpaper poll / 50ms `clearMask()` race
- **Where**: `src/shell/popovers.cpp:2319-2412`; `apps/wallpaper/main.cpp:37-42, 151-155`
- **Evidence**: `BootSplashWindow` is dead code unused since splash moved to Plymouth (`src/shell/main.cpp:237-240`). `finn-wallpaper` polls theme every 1s (`37-42`) and invokes `clearMask()` for `50ms` (`151-155`) before restoring an empty input region on resize/reload.

### `m10` — `probe_android_runtime()` omits `binder_present`; `inspect_pe_file()` reads only 4096 bytes
- **Where**: `src/compat/android_runner.cpp:41-47`; `src/compat/wine_runner.cpp:131`
- **Evidence**: `st.binder_present` is probed at `android_runner.cpp:41` (`!st.binder_present` gives an error hint at `47`) but is omitted from `st.ready` (`43`), so `waydroid` can report ready without `/dev/binder*`. `inspect_pe_file` (`wine_runner.cpp:131`) reads only the first `4096` bytes, failing on PE headers/metadata beyond 4 KB.

### `m11` — Miscellaneous edge bugs & hardcoded limits
- `FinnThemeManager::reload()` (`src/ui/finn/theme.cpp:179-196`) omits `lock_wallpaper` from change detection.
- `apps/files/main.cpp:667` uses `colors::mute_light` unconditionally, ignoring dark mode.
- `src/shell/main.cpp:64-70` adds `/run/finn/state` to `QFileSystemWatcher` without checking existence first (silent failure if absent at startup).
- `apps/installer/main.cpp:569-570` hardcodes only 4 timezones (`UTC`, `America/New_York`, `Europe/London`, `Europe/Berlin`).
- `src/comp/server.cpp:604` hardcodes `xkb_keymap_new_from_names(ctx, nullptr, ...)` with no user keyboard layout configuration anywhere in the OS.
