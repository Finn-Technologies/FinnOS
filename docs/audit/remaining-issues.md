# Remaining Issues Tracker (Living List)

*Branch: `audit-and-ota` | Last updated: Phase 1 code/UI audit & Phase 6 daily-driver checks.*

## 1. Open Blockers (`B1..B12`)

| ID | Summary | Primary Locations | Owner / Track | Status |
|---|---|---|---|---|
| `B1` | No OTA update mechanism (`FinnOS Update` missing) | `NAMING.md`, `apps/settings/main.cpp` | `@ota` | In progress |
| `B2` | Unprivileged security gaps (`finn-comp` root, no `finnd`, sandbox missing) | `docs/design/contract.md:39, 85-86`, `base/finn-comp.service:22` | Unassigned (v2 `finnd`) | Open |
| `B3` | `[finnos]` pacman repo URL `https://repo.finnos.org` unprovisioned | `base/pacman.conf:64-66` | `@repo` | In progress |
| `B4` | Branding / Arch references / naming violations | Across UI/boot/docs | `@rebrand` | In progress |
| `B5` | Hardcoded paths & duplicated settings constants (expanded in `M17`) | `src/common/paths.hpp`, `src/comp/server.cpp` | Unassigned | Open |
| `B6` | Lock screen bypassed via `Alt+Tab` / WM shortcuts; `ext-session-lock-v1` listener unattached | `src/comp/server.cpp:655-705, 1313`, `src/shell/popovers.cpp:1916` | Unassigned | **Open (New)** |
| `B7` | Lock screen unlocks `L` (locked) accounts on empty password; `loginctl activate` missing session arg | `src/shell/popovers.cpp:2155-2156, 2188-2197` | Unassigned | **Open (New)** |
| `B8` | GUI `finn-installer` invokes `finn-install` with invalid positional args, hides stderr, allows double-click, claims `btrfs` | `apps/installer/main.cpp:499, 735-758`, `vm/finn-install.sh:143-167` | Unassigned | **Open (New)** |
| `B9` | `finn-oobe` discards Account page inputs in `finishSetup()`; writes unread `~/.config/finn/locale.conf` | `apps/oobe/main.cpp:1899-1940, 2305-2335` | Unassigned | **Open (New)** |
| `B10` | Plaintext Wi-Fi passphrase in visible `FinnDialog::prompt` and `/proc/<pid>/cmdline` via `nmcli` | `src/ui/finn/components.cpp:2120`, `src/system/desktop_services.cpp:345-350`, `apps/{settings,oobe}/main.cpp` | Unassigned | **Open (New)** |
| `B11` | `finn-notes` silently discards unsaved note edits when Cancel clicked on Save Changes dialog | `apps/notes/main.cpp:127-135, 596-602` | Unassigned | **Open (New)** |
| `B12` | `[finnos]` `SigLevel = Required` placeholder breaks `pacman -Sy`; `root`/`finn` share password in `finn-chroot.sh:125` | `base/pacman.conf:64-66`, `vm/finn-chroot.sh:125` | `@repo` / Unassigned | **Open (New)** |

## 2. Open Majors (`M1..M19`)

| ID | Summary | Primary Locations | Owner / Track | Status |
|---|---|---|---|---|
| `M1..M8` | First-pass majors (blocking UI calls, `FinnUI` tokens, motion, SDK parity, test coverage) | See first-pass `audit_report.md` | `@rebrand` / `@design` / `@motion` / `@sdk` | In progress / partial |
| `M9` | XDG toplevel `wlr_scene_tree` + 13 SSD `wlr_scene_rect` leak on every closed Wayland window | `src/comp/server.cpp:762, 874-886`, `src/comp/window.cpp:31-60` | Unassigned | **Open (New)** |
| `M10` | Compositor fullscreen fakes maximize, leaving dock visible | `src/comp/server.cpp:856-860` | Unassigned | **Open (New)** |
| `M11` | `finn-session` blocking `accept()`, `client_fd` FD leak across `posix_spawnp`, per-request registry rescan, `0660` vs `0600` mode | `src/session/main.cpp:181, 332, 483, 502, 510-519` | Unassigned | **Open (New)** |
| `M12` | `Terminal=true` `.desktop` launch drops args after `-e`; `feedBytes()` splits multi-byte UTF-8 | `src/session/main.cpp:217-219`, `apps/terminal/main.cpp:605-607, 1028-1030` | Unassigned | **Open (New)** |
| `M13` | Blocking `waitForFinished` / `popen` / sync D-Bus on UI threads + 3s process-spawning poll | `src/system/desktop_services.cpp`, `apps/store/main.cpp`, `src/shell/popovers.cpp`, `apps/{settings,installer}/main.cpp`, `src/compat/android_runner.cpp:61` | Unassigned | **Open (New)** |
| `M14` | `finn-files` `copy_recursive()` blocks UI, recurses infinitely on dir symlinks, lacks self-paste guard | `apps/files/main.cpp:62-76, 927-937` | Unassigned | **Open (New)** |
| `M15` | `finn-store` reads wrong `windows.conf` path, disables Install button, `chmod +x` launches AppImages | `apps/store/main.cpp:429, 659-660, 723` | Unassigned | **Open (New)** |
| `M16` | Accessibility toggles (`text_scale`, `color_filter`, `sticky_keys`, `filter_keys`) unimplemented; `orca` unpackaged | `apps/settings/main.cpp:722, 745-790`, `src/qtplugin/finn_platform_theme.cpp:190-193` | Unassigned | **Open (New)** |
| `M17` | Hardcoded paths (`/home/finn/.config/finn/settings`, `/usr/local/bin/finn-*`, `/usr/share/finn/...`) & `/etc/finn` watch | `src/comp/server.cpp`, `src/session/main.cpp`, `apps/{installer,wallpaper,viewer}/main.cpp`, `src/ui/finn/theme.cpp` | Unassigned | **Open (New)** |
| `M18` | 0 accessibility names/descriptions, 0 focus rings (`hasFocus`), 1 `setFocusPolicy`, shell popovers pointer-only | `src/ui/finn/components.cpp`, `src/shell/{dock,popovers}.cpp`, `apps/{oobe,files,flourish}/` | `@design` / Unassigned | **Open (New)** |
| `M19` | Per-paint 3-pass CPU software blur in OOBE; 3s/1.2s widget tear-down polling in Settings & Monitor | `apps/oobe/main.cpp:906-962`, `apps/settings/main.cpp:1118-1167`, `apps/monitor/main.cpp:195-198` | `@design` / Unassigned | **Open (New)** |

## 3. Open Minors (`m1..m11`)

| ID | Summary | Primary Locations | Owner / Track | Status |
|---|---|---|---|---|
| `m1..m4` | First-pass minors (copy, docs, style) | See first-pass `audit_report.md` | `@rebrand` | In progress |
| `m5` | Settings "Memory" card displays root disk capacity (`QStorageInfo::root()`) instead of RAM | `apps/settings/main.cpp:1026-1042` | Unassigned | **Open (New)** |
| `m6` | `NotificationServer` `replaces_id` leaks previous auto-dismiss `QTimer` | `src/shell/notifications.cpp:354-382` | Unassigned | **Open (New)** |
| `m7` | `nmcli -t` `split(':')` breaks Wi-Fi SSIDs containing `:` | `src/system/desktop_services.cpp:188-194` | Unassigned | **Open (New)** |
| `m8` | `FinnProgressRing` 16ms timer ignores `reduced_motion()` | `src/ui/finn/components.cpp:982-999` | `@motion` | **Open (New)** |
| `m9` | Dead `BootSplashWindow` code; `finn-wallpaper` 1s poll & 50ms `clearMask()` race | `src/shell/popovers.cpp:2319-2412`, `apps/wallpaper/main.cpp:37-42, 151-155` | Unassigned | **Open (New)** |
| `m10` | `probe_android_runtime()` omits `binder_present`; `inspect_pe_file()` reads only 4096 bytes | `src/compat/android_runner.cpp:41-47`, `src/compat/wine_runner.cpp:131` | Unassigned | **Open (New)** |
| `m11` | `theme.cpp` omits `lock_wallpaper`; `files` uses `mute_light` in dark mode; `/run/finn/state` watch race; 4 timezones in Installer; default XKB layout | `src/ui/finn/theme.cpp:179-196`, `apps/files/main.cpp:667`, `src/shell/main.cpp:64-70`, `apps/installer/main.cpp:569-570`, `src/comp/server.cpp:604` | Unassigned | **Open (New)** |

## 4. UI Token-Bypass & Daily-Driver Gaps

- **UI Token Bypasses**: ~100+ raw RGB/RGBA literals across `src/comp/{server.cpp,window.cpp}`, `src/ui/finn/components.cpp`, `src/shell/{dock.cpp,popovers.cpp,notifications.cpp}`, `apps/oobe/main.cpp`, and `apps/flourish/` — full mechanical replacement table in [ui-audit.md](ui-audit.md).
- **Daily-Driver Gaps** (detailed in [daily-driver.md](daily-driver.md)):
  - **Missing**: Accessibility runtime (`text_scale`, `color_filter`, `sticky_keys`, `filter_keys`, `orca`), Localization (`QTranslator`, locale generation), Backup/Restore, Touch/Tablet input (`wlr_touch`/`wlr_tablet`).
  - **Partial / Blocked**: GUI Installer (`B8`), Screen Lock (`B6`, `B7`), Store package install (`M15`), XKB keyboard layout switching + IME, per-output multi-monitor & live HiDPI scale reload, PipeWire mixer/device selector, AMD/NVIDIA GPU drivers.
