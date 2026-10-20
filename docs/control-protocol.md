# The control protocol

The control plane speaks a small line protocol. It is deliberately text and
not JSON: JSON is needed only at the MCP boundary, where the adapter
translates it, and keeping the on-device daemons free of a JSON parser
keeps them small and keeps a fast-moving external spec out of the
operating system.

It also means any shell can drive the system:

```sh
printf 'state.set battery=42\n' | finnctl
```

## The wire

One request per line, whitespace separated, `verb arg=value ...`:

```
state.set battery=42 charging=1
```

Three kinds of reply:

```
OK                                  the verb did what it said
OK <single-line payload>            a value
OK BINARY <len>\n<len raw bytes>    a binary payload
ERR <message>                       it did not
```

A verb that takes prose rather than key=value pairs takes one argument
separated by a pipe, because a pipe is the one character a title will not
contain:

```
notify.post <title>|<body>
```

## The sockets

| Path | Owner | Mode |
|---|---|---|
| `/run/finn/control.sock` | `finnd` | 0660, root and the `finn` group |
| `/run/finn/comp.sock` | `finn-comp` | 0660, root and the `finn` group |

Both are unix sockets. There is no TCP listener in the system, and there
will not be one: remote control is an explicit decision by the user, not a
default.

`finnctl` talks to the control plane and falls back to the compositor
socket when the control plane is not running. `--direct` forces the
compositor socket.

## The verbs

### Session

| Verb | Reply | What it does |
|---|---|---|
| `ping` | `OK FINNCTL/1` | is the plane alive |
| `version` | `OK finnd=0.1 proto=FINNCTL/1` | what is on the other end |

### State

The system state: battery, charging, wireless, volume, brightness,
Bluetooth, wired link, appearance. Every field has an absent value
(`-1`) rather than a default, because the shell has to be able to say "this
machine does not have one".

| Verb | Reply | What it does |
|---|---|---|
| `state.get` | `OK battery=… charging=… wifi=… volume=… brightness=… bluetooth=… wired=… dark=…` | read the state |
| `state.set` | `OK` | write an override: `state.set battery=42 charging=1` |
| `state.clear` | `OK` | drop the overrides; back to the real hardware |

The overrides live in `/run/finn/state`. They are a real interface: a
session daemon can publish state through them, and a test can drive the
shell through every state deterministically.

### Display and input

Provided by the compositor, proxied by the control plane.

| Verb | Reply | What it does |
|---|---|---|
| `screen.info` | `OK w=… h=… scale=…` | the framebuffer and the density |
| `screen.shell` | `OK cc=… openness=… menu=… clock=… dock=… pointer=…` | what the shell is showing |
| `screen.capture` | `OK BINARY …` | a PPM of the frame, cursor included; `step=2` halves it |
| `screen.pointer` | `OK x=… y=… buttons=…` | where the pointer is |
| `pointer.move` | `OK` | move the pointer: `pointer.move x=300 y=400` |
| `pointer.by` | `OK` | nudge it: `pointer.by dx=10 dy=-4` |
| `pointer.click` | `OK` | press and release |
| `pointer.down` / `pointer.up` | `OK` | hold it down, or let it go |
| `pointer.scroll` | `OK` | reserved; there is nothing to scroll yet |

A click is an edge the shell can see, so an injected click is pressed now
and released after the shell has handled it.

### The machine

| Verb | Reply | What it does |
|---|---|---|
| `sound.get` | `OK volume=… element=…` | the output level and the mixer it is on |
| `sound.set` | `OK` | set the output level for real |
| `display.get` | `OK brightness=…` | the backlight |
| `display.set` | `OK` | set the backlight for real |
| `radio.set` | `OK` | `radio.set wifi=1 bluetooth=0` |
| `appearance.set` | `OK` | `appearance.set dark=1` |

These do not write an override — they change the machine. That is the
difference between "show me 40%" and "be 40%".

### Notifications

| Verb | Reply | What it does |
|---|---|---|
| `notify.post` | `OK` | post a notification: `notify.post <title>\|<body>` |
| `notify.list` | `OK count=… [title\|body\|time]…` | what has happened this session |
| `notify.clear` | `OK` | forget it all |

The queue is the shell's own, and it is open to any part of the system:
the compositor posts the hardware transitions it observes, and a service can
post on its own behalf through the control plane. It starts empty on every
start, so the page can say "nothing has happened" and mean it.

### Power

| Verb | Reply | What it does |
|---|---|---|
| `power shutdown` | `OK` | power the machine off |
| `power restart` | `OK` | reboot it |
| `power restart-comp` | `OK` | restart the compositor only |

### Applications

| Verb | Reply | What it does |
|---|---|---|
| `app.list` | `OK count=… [id\|state\|name]…` | the catalogue and what is installed |
| `app.launch <id>` | `OK launched <id>` | start it, as the user |

The control plane is the only place in the system that starts user
programs. The compositor asks it, a script asks it, an agent asks it, and
every one of those goes through the same audit log.

## The shape of a session

```
client                     finnd                      finn-comp
  |  state.get                |                           |
  |-------------------------->|  (answered here)          |
  |  OK battery=87 ...        |                           |
  |<--------------------------|                           |
  |  screen.capture           |                           |
  |-------------------------->|-- proxy ------------------>|
  |                           |                           |  (a frame is composed)
  |  OK BINARY 3072000        |<-- relay ------------------|
  |<--------------------------|                           |
```

`finnd` serves one connection at a time, and a connection may carry many
requests. Every request is logged with the peer uid.

## Security posture

`finnd` is root-equivalent authority. It is therefore off by default, bound
to a unix socket only, authenticated per connection with `SO_PEERCRED`, and
audited. It is run under systemd hardening, and the audit log is at
`/var/log/finn/control.log`.

There is one development escape hatch, `FINN_CONTROL_ALLOW_ANY`, which
lets any uid connect. It is off unless it is asked for, and it exists for
the test harness and nothing else.
