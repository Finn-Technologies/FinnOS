# The session protocol

`finn-session` is the user's launch broker. It listens on a unix socket and
speaks one request per connection. It is not an authority over the machine.

## The socket

`$XDG_RUNTIME_DIR/finn/session.sock`, mode `0600`, owned by the user running
the session. The directory is created `0700`. The server checks `SO_PEERCRED`
and refuses any connection from another uid.

`FINN_SESSION_SOCK` overrides the path; tests use it. Clients use
`session_socket_path()`, which is the one implementation.

## Verbs

One line, `\n` terminated, at most 4096 bytes. The reply is one line, except
`APP.LIST`.

| Request | Reply |
|---|---|
| `PING` | `OK finn-session 1` |
| `APP.LIST` | `OK count=N` then N lines of `id\|installed\|name\|icon` |
| `APP.LAUNCH <id>` | `OK pid=<pid>` or `ERR <reason>` |
| `QUIT` | `OK`, and the daemon exits (used by tests) |

`installed` is `1` or `0`. The list contains every catalogue entry, installed
or not; the shell filters. An unknown request is `ERR unknown request`.

## Launch rules

`APP.LAUNCH` refuses, in this order:

- an unknown id;
- an id that is not installed;
- an entry that is not native or has no command;
- a command that is not executable;
- a session with no `XDG_RUNTIME_DIR`.

The child gets:

```
WAYLAND_DISPLAY=/run/finn/wayland-0
XDG_RUNTIME_DIR=<the session's>
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=FinnOS
PATH=/usr/local/bin:/usr/bin:/bin
HOME=<the session's>
FINN_SESSION=1
FINN_DARK / FINN_ICON_DIR / FINN_FONT  (when set)
```

stdin, stdout and stderr are `/dev/null`; a launched app's own logging is its
problem. The shell is the intended client and scripts use the same verbs:

```sh
finn-session --list
finn-session --launch files
```

## Why a broker at all

The old design started apps from a root daemon. That made every app run with
the user's authority but started by root, and it put app launch in the same
process as power and packages. The broker is the smallest thing that keeps
launch scriptable without giving a GUI app the machine.

The next step is a per-app transient unit or a namespace sandbox; today the
app runs with the user's full authority. The [security page](security.md)
says so.
