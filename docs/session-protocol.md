# Session protocol

`finn-session` (`src/session/main.cpp`) is an unprivileged user daemon running as `finn`. It serves a line-oriented Unix domain socket (`AF_UNIX`, `SOCK_STREAM`, mode `0600`) with 2-second read/write timeouts (`SO_RCVTIMEO` / `SO_SNDTIMEO`).

## Socket path resolution (`src/session/client.cpp`)

1. `$FINN_SESSION_SOCK` if set, otherwise
2. `$XDG_RUNTIME_DIR/finn/session.sock`, otherwise
3. `/tmp/finn-session-<uid>.sock`.

On Linux (`SO_PEERCRED`), `finn-session` accepts connections only when `cred.uid == getuid() || cred.uid == 0`. There is no TCP listener.

## Verbs

Each request is a single UTF-8 line terminated by `\n`. Reporting verbs never mutate state.

| Request | Reply | Notes |
|---|---|---|
| `PING` | `OK pong` | Liveness check |
| `QUIT` | `OK bye` | Stops the `finn-session` daemon |
| `APP.LIST` | `OK count=<N>` followed by `<N>` lines:<br>`id\|installed\|name\|icon\|source\|category\|note` | `installed` is `0` or `1`; `source` is `native`, `desktop`, `flatpak`, `appimage`, `windows`, `macos`, `ohos`, `android`, etc. |
| `APP.LAUNCH <id>`<br>`APP.LAUNCH <id>\t<arg>` | `OK launched` or `ERR <reason>` | Spawns the resolved app with `posix_spawnp` + `POSIX_SPAWN_SETSID` |
| `APP.OPEN <path-or-url>` | `OK ...` or `ERR <reason>` | Routes URLs, directories, `.app`, `.finnapp`, `.flatpakref`/`.flatpak`/`.flatpakrepo`, `.appimage`, `.dmg`, `.pkg`, `.msix`, `.apk`/`.xapk`/`.apkm`/`.apks`, `.hap`/`.hsp`, `.exe`/`.msi`, images, text/code, `.desktop`, or `xdg-open` |
| `APP.UNINSTALL <id>` | `OK uninstalled` or `ERR <reason>` | Uninstalls Android, macOS, OpenHarmony, Flatpak, Windows, AppImage, or user `.finnapp` entries |
| `WIN.RUN <exe>`<br>`WIN.RUN <exe>\|<prefix>` | `OK launched` or `ERR <reason>` | Launches a Windows executable via the planned `finn-win` / Wine invocation |
| `COMPAT.STATUS` | `OK arch=...\|wayland=...\|display=...\|xwayland=...\|wine=...\|flatpak=...\|finn_win=...\|prefix_ready=...\|prefix=...\|appimage=1\|appimage_fuse=...\|linux_sandbox=env-only\|linux_theme=finn\|android=...\|android_api=...\|launch_env=...\|wine_summary=...\|android_summary=...` | Single-line read-only compatibility probe |
| `COMPAT.PROBE <path>` | `OK ...` or `ERR <reason>` | Read-only ELF architecture, interpreter (`PT_INTERP`), and missing shared library (`DT_NEEDED`) diagnostic |
| `POWER.ACTION <reboot\|poweroff\|suspend>` | `OK ...` or `ERR <reason>` | Spawns `systemctl <action>` |

## CLI modes

`finn-session` also supports direct CLI invocation without a running daemon for diagnostics and scripting:
- `finn-session --version`
- `finn-session --help`
- `finn-session --list`
- `finn-session --launch <id>`

## Child launch environment

Launched apps inherit a sanitized environment built by `finn::compat::build_linux_launch_env()` (`src/compat/linux_sandbox.cpp`):
- Sets `WAYLAND_DISPLAY=/run/finn/wayland-0` (if unset) and `DISPLAY` from `/run/finn/xdisplay` or the environment.
- Sets `QT_QPA_PLATFORM=wayland;xcb`, `QT_QPA_PLATFORMTHEME=finn`, and `QT_STYLE_OVERRIDE=finn`.
- Strips loader and plugin hijack variables (`LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT`, `QT_PLUGIN_PATH`, `GTK_PATH`, etc.).
- Honestly reports `linux_sandbox=env-only`: FinnOS v1 does not enforce a kernel/namespace per-app sandbox.
