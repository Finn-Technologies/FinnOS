# The session protocol

`finn-session` is the user's launch broker. It listens on a unix socket and
speaks one line-oriented request per connection. It is not a root daemon.

## The socket

`$XDG_RUNTIME_DIR/finn/session.sock`, mode `0600`, owned by the user running
the session. The server checks `SO_PEERCRED` and refuses connections from any
other uid.

`FINN_SESSION_SOCK` overrides the path for tests. Clients use
`finn::session_socket_path()`.

## Verbs

Requests are `\n` terminated (at most 4096 bytes):

| Request | Reply |
|---|---|
| `PING` | `OK finn-session 1` |
| `APP.LIST` | `OK count=N` then `N` lines of `id\|installed\|name\|icon` |
| `APP.LAUNCH <id>` | `OK pid=<pid>` or `ERR <reason>` |
| `QUIT` | `OK`, and the daemon exits (tests) |

## Launch environment

Children inherit `/run/finn/wayland-0` (or active `WAYLAND_DISPLAY`),
`XDG_RUNTIME_DIR`, `XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=FinnOS`,
`QT_QPA_PLATFORM=wayland`, `QT_QPA_PLATFORMTHEME=finn`,
`QT_STYLE_OVERRIDE=finn`, plus `DISPLAY` when `Xwayland` is active.

Launch verbs also support desktop `.desktop` entries and Windows `.exe` via
Wine helpers when called through `finn-open` / Finn Shell / `finn-session`.

```sh
finn-session --list
finn-session --launch files
```
