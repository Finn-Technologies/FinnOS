# Security

The security model of FinnOS is one sentence: **the machine's authority is one
unix socket, and the person at the machine is the person allowed to use it.**

Everything else follows from that sentence, including the parts that are not
finished.

## What `finnd` can do

`finnd` is root-equivalent authority over a running system. Through it a client
can:

- move the pointer, inject clicks and keys, and read the framebuffer;
- change the volume, the backlight, the radios and the appearance;
- open, move, resize, snap, minimise, maximise and close windows;
- start and install applications, and start Flatpak, Wine and winget jobs;
- check for, download, install and roll back the runtime update;
- reboot or power off the machine.

That is why the daemon is treated as a privilege boundary rather than a helper.

## The one door

| Property | How it is enforced |
|---|---|
| transport | a unix socket at `/run/finn/control.sock`; there is no TCP listener and no `.socket` unit |
| ownership | mode `0660`, owned by `root` and the `finn` group |
| authentication | `SO_PEERCRED` on every accepted connection: root, or the `finn` user |
| audit | every call is appended to `/var/log/finn/control.log` with the peer uid |
| audit failure | if the log cannot be opened, every verb except `ping` and `version` is refused (`ERR audit unavailable`) |
| hardening | systemd `NoNewPrivileges`, `ProtectHome=read-only`, `ProtectKernelTunables`, `ProtectKernelModules`, `ProtectControlGroups`, `RestrictRealtime`, `MemoryDenyWriteExecute`, `SystemCallFilter=@system-service`, `SystemCallArchitectures=native` |
| authority | the daemon runs as root in root's own group; the group that matters is the one on the socket |

The socket is mode `0660`, owned by root and the `finn` group, which lets the
desktop account open it; the daemon then accepts only root and the `finn` uid.
A second account is refused even if it is put in the group. The daemon does not
change uid: it asks systemd for a transient unit, and systemd starts the app as
the user.

The compositor's socket is **not** a second door. It is `/run/finn/comp.sock`,
mode `0600`, owned by root, and `finn-comp` checks `SO_PEERCRED` as well.
`finnctl --direct` needs root, says on stderr that it bypasses `finnd` and the
audit log, and exists for development and diagnosis. There is no automatic
fallback to it.

## What the audit log proves

Every request is one line:

```
2026-10-27T20:14:03 uid=1000 state.set battery=42 -> OK
```

Non-printable bytes are escaped, so a newline in a client's argument cannot forge
a second line. The log is opened `0600` and close-on-exec, and a pre-existing
`0644` log is repaired on the first start.

It proves *that* a call happened and *who* made it. It does not prove the log
was not edited afterwards: it is a local file, and root can rewrite it. There is
no remote append, no signing and no tamper-evident chain. That is a real limit,
not a design claim.

## Who is trusted

The `finn` user is the desktop user. The model is a personal machine: the
person logged in at the screen is allowed to move the pointer, open a window and
power the machine off, because it is their machine.

That has consequences worth stating:

- **A compromised application running as `finn` has the same authority as the
  person.** Native apps launched through `finnd` run as `finn` in a transient
  systemd unit. Flatpak apps have their own sandbox; Wine is not sandboxed by
  FinnOS. There is no per-verb authorisation, no polkit and no prompt.
- **A second account on the machine cannot use the socket.** The daemon accepts
  only root and the `finn` user, whatever the socket's group says. A second
  account can still log in on tty2 if it has a password.
- **`root` can do anything**, as on any Linux. The control plane narrows the
  path, not the privilege ceiling.

Remote access is a decision, not a default. `sshd` is installed and disabled on
an installed machine; enabling it is a command someone types on the machine.
There is no default password: the installer uses the one it was given, or
generates one and prints it once.

A machine prepared by `vm/deploy.sh` from a bare Arch rootfs is a build target,
not an installed system. The script creates `finn` with a locked password and
passwordless sudo, so that machine is reached as `root` over SSH or with a key.
That is a build-machine convenience, not a default of the installed system;
never expose a dev VM to a network you do not control.

## Starting a program

An app is started by **systemd**, as the user, in its own transient unit. The
control plane does not fork and does not change uid. The catalogue's command is
passed as one argv element — never through a shell — and the app id is validated
before it reaches a path or the install list.

`app.install` and `app.remove` do not edit the install list themselves; they run
`finnpkg`, which owns it, and pass the package manager's own refusal back. The
control plane validates the id shape and that the catalogue knows the app before
it runs anything, and bounds the wait at 1.5 seconds.

The package manager's `.finn` path is a tarball reader with its own defences:
it lists before unpacking, refuses absolute paths, `..`, symlinks, hard links
and device nodes, unpacks into a fresh temporary directory, requires a manifest
and a `SHA256SUMS` that covers every file, and only then records the id. It has
**no signature check**; the hashes prove the files arrived as packaged, not who
packaged them.

## Reports versus changes

The protocol keeps two kinds of verb apart on purpose:

- a **reporting** verb (`screen.info`, `state.get`, `window.list`,
  `app.list`) changes nothing;
- a **changing** verb (`display.set`, `radio.set`, `power`, `app.launch`)
  reports the actuator's own result, so a control cannot light up on a machine
  that cannot act.

This is a security property as well as an honesty one: a report that can change
state is an action nobody audited as an action.

`state.set` is the one deliberate exception in appearance: it writes
`/run/finn/state` overrides rather than touching hardware. It is a test and
session-publishing interface, it is not durable, and `state.clear` returns to
the hardware.

## The update boundary

- Downloads are HTTPS only, with a bounded timeout and size.
- The SHA-256 comes from the release asset and is checked before pacman sees the
  package.
- **There is no signing key in the image.** The checksum protects against a
  truncated or corrupted download; it does not protect against a compromised
  GitHub release. The UI does not claim a signature exists.
- The package is installed with `pacman -U` as root through a tracked systemd
  unit, so the action is audited.
- **Secure Boot is not signed.** The installer writes a normal GRUB or
  systemd-boot setup; firmware Secure Boot must be disabled or given a key you
  control.

## The MCP boundary

`finn-mcp` runs on the host, speaks JSON-RPC to the agent there, and reaches the
device over `ssh` and `finnctl`. JSON does not enter the OS, and MCP does not
enter a system daemon.

The adapter inherits the authority of the credentials it uses. Register it only
with a client you trust with the device, and prefer a dedicated key or account
over a password. The MCP adapter is not a privilege boundary of its own; it is a
transport.

## Zero AI

Nothing in the OS talks to a model, ships weights, or requires a network call to
a model service. There is no inference code in the tree. Any future FinnAI is
opt-in and off by default. This is a product decision, and it is also the
smallest possible attack surface for a feature that does not exist yet.

## Known gaps

| Gap | Consequence | Where it is tracked |
|---|---|---|
| No package signatures | a compromised release cannot be distinguished from a good one by checksum alone | [updates.md](updates.md) |
| No Secure Boot signing | firmware cannot verify the bootloader | [install.md](install.md) |
| The `finn` account is the trust boundary | a compromised user app can control the machine | this page |
| No per-verb authorisation or prompt | no polkit, no "allow this once" | this page |
| Audit log is a local file | root can rewrite it; no remote append | this page |
| Native apps and Wine are not sandboxed | they run as `finn` with the user's authority | [hardware.md](hardware.md) |
| The compositor runs as root | DRM master requires it; hardening is the mitigations in the unit, not a separate user | `base/finn-comp.service` |

## Reporting a problem

Do not open a public issue for a privilege-boundary problem. Contact the
maintainers privately with the version (`cat /usr/share/finn/version`), the
machine, the command, and what you observed. If the problem is "a control says
it can do something it cannot", that is a correctness and honesty bug; it is
still worth reporting, and it is not a security issue unless it crosses the
socket boundary.

## Related

- [architecture.md](architecture.md) — where the daemon sits in the system
- [control-protocol.md](control-protocol.md) — the wire and every verb
- [updates.md](updates.md) — what the update path verifies
