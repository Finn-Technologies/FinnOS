# FinnOS control plane & MCP adapter

This directory holds the agent-facing half of the FinnOS control plane. The
device-side half lives in `../src/control/` (`finnd`, `finnctl`) and in
`../src/compositor/` (`finn-comp`, the provider underneath it).

```
agent (Claude, opencode, any MCP client)
    │   MCP — JSON-RPC 2.0 over stdio
    ▼
finn-mcp                      ← this directory; runs on the host
    │   ssh
    ▼
finnctl state.set battery=42  ← device-side CLI
    │   unix socket, line protocol
    ▼
finnd                         ← control plane, /run/finn/control.sock
    ├── state.*                       handled here
    └── pointer.* screen.*            proxied to finn-comp
```

MCP is a transport, not the OS API. `finnd` owns the control plane and speaks
a small line protocol; all JSON lives in the adapter. That keeps an external,
fast-moving spec out of the operating system, and it means the same control
plane is fully scriptable with nothing but `ssh`:

```sh
ssh finn@10.211.55.6 finnctl state.set battery=42 charging=1
ssh finn@10.211.55.6 finnctl screen.capture -o - > shot.ppm
```

## Requirements

Python 3.6+ on the host, `ssh` on the host, `finnctl` + `finnd` on the device.
Pillow is optional — without it `finn_screenshot` returns a PPM file path
instead of an inline PNG.

## Check connectivity

```sh
./finn-mcp --check
```

```
finn-mcp -> finn@10.211.55.6
  ping: FINNCTL/1
  framebuffer: 2560x1600
  tools: 13
```

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `FINN_HOST` | `finn@10.211.55.6` | ssh target for the device |
| `FINN_SSH_PORT` | `22` | ssh port |
| `FINN_TIMEOUT` | `120` | per-call timeout, seconds |

## Tools

| Tool | What it does |
|---|---|
| `finn_state_get` | battery, charging, wifi signal, volume |
| `finn_state_set` | override any of those (drives the shell's rendering) |
| `finn_state_clear` | drop overrides; back to real hardware |
| `finn_pointer_move` | absolute pointer position |
| `finn_pointer_by` | relative pointer nudge |
| `finn_pointer_click` | click, optionally moving first |
| `finn_pointer_scroll` | scroll by notches |
| `finn_screen_info` | framebuffer resolution and UI density |
| `finn_screenshot` | capture the framebuffer, cursor included |
| `finn_log` | recent `finn-comp` log lines |
| `finn_app_list` | the application registry: id, name, and whether it is installed |
| `finn_app_launch` | start an app by its id, as the user |
| `finn_power` | reboot / power off / restart the compositor |

The registry is two files on the device: the catalogue the OS ships and
the install list the package manager keeps. So an app can be in the
catalogue and still not be on this machine, and `finn_app_list` says
which is which rather than pretending every app is there. An app that
is not installed cannot be started, and the device says so.

## Registering with a client

Claude Desktop (`~/Library/Application Support/Claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "finnos": {
      "command": "/Users/abhi/Desktop/FinnOS/mcp/finn-mcp",
      "env": { "FINN_HOST": "finn@10.211.55.6" }
    }
  }
}
```

opencode (`~/.config/opencode/config.json`):

```json
{
  "mcp": {
    "finnos": {
      "type": "local",
      "command": ["/Users/abhi/Desktop/FinnOS/mcp/finn-mcp"],
      "enabled": true
    }
  }
}
```

## Security posture

`finnd` is root-equivalent authority over a running system: it can move the
pointer, read the framebuffer, and power the machine off. It is therefore:

- **off by default**, on the same intent as the zero-AI rule;
- bound to a **unix socket only** — there is no TCP listener, ever;
- **authenticated per connection** with `SO_PEERCRED` (root, or the `finn` user);
- **audited** to `/var/log/finn/control.log`, with the peer uid on every call;
- run under systemd hardening (`SystemCallFilter`, `ProtectKernelTunables`, …).

`finn-mcp` inherits that authority, so it should only be registered with a
client you trust with the device.
