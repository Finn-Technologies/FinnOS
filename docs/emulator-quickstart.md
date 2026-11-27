# `finn-emulator` quickstart

`tools/finn-emulator` is the first-class probe/simulator tool shipped alongside
`finn-sdk` and `finn-win`. It reports which emulator backends really exist on a
host, prints runnable boot commands, and runs a FinnSDK app against Qt's
offscreen platform. It never claims a capability it cannot exercise, and it
never starts or changes the running dev VM.

## Install

`tools/finn-emulator` is installed by `make install` to `$(PREFIX)/bin/finn-emulator`
(and symlinked at `/usr/bin/finn-emulator`). From a source checkout you can also
run it directly as `./tools/finn-emulator`.

## Commands

```sh
finn-emulator --help
finn-emulator --version                     # finn-emulator 1.1.0
finn-emulator check                         # [ok] / [partial] / [missing] per tool
finn-emulator backends                      # backend capability matrix
finn-emulator validate                      # exit 0 if a real backend is usable, else 1
finn-emulator report                        # machine-readable JSON, always on stdout
finn-emulator selftest                      # verifies this tool's claims on this host
finn-emulator boot --arch x86_64 --iso finnos.iso
finn-emulator boot --arch aarch64 --iso finnos.iso
finn-emulator simulate --app sdk/template --form-factor phone --dry-run
finn-emulator simulate --app myapp --form-factor tablet
finn-emulator simulate --app myapp --form-factor phone --headless
```

`--json` additionally makes `check`, `backends` and `validate` emit JSON;
`report` is JSON unconditionally. `check`, `backends`, `validate`, `report` and
`selftest` are read-only.

## `report` — machine-readable capability report

One JSON object on stdout with `schema_version: 1`:

| Field | Meaning |
|---|---|
| `tool`, `tool_version` | `finn-emulator`, `1.1.0` |
| `host.os`, `host.arch`, `host.arch_raw` | `uname` values normalized to `x86_64` / `aarch64` where possible |
| `verdict.usable` | boolean; `true` only if a real full-OS guest **or** app simulator is present |
| `verdict.host` / `full_os_guest` / `app_simulator` | `PASS`, `FAIL` or `WARN` (host only) |
| `verdict.full_os_detail`, `verdict.app_simulator_detail` | human-readable reason for each state |
| `checks[]` | `{name, status, detail}` — `status` is `ok`, `partial` or `missing` |
| `backends[]` | `{backend, status, can, cannot}` — `status` is `ready`, `installed`, `available` or `absent` |
| `cannot_emulate[]` | constant list of capability limits, independent of installed tools |

Exit codes: `0` when `verdict.usable` is `true`, `1` when it is `false`, `2`
for a bad command line. A `partial` check (for example "qmake unavailable to
probe the Qt offscreen plugin") is never promoted to `ok`: the JSON says
`app_simulator_detail: "… unconfirmed …"` while `app_simulator` stays `PASS`
only because `finn-sdk` itself is present.

Example consumption:

```sh
finn-emulator report > capability.json     # exit 0/1 is the honest verdict
python3 -c 'import json;d=json.load(open("capability.json"));print(d["verdict"])'
```

## `selftest` — no lying about this host

`selftest` re-verifies the tool's own claims instead of trusting them:

- `bash -n` of the running script;
- all eight commands and the version appear in `--help`;
- every `ok` probe resolves to a real executable, instance or file, and every
  `missing` probe really is absent; every row has a non-empty detail;
- every non-`absent` backend row has a real backend binary;
- `validate`'s printed `RESULT:` matches its exit code;
- `report` parses as JSON (via `python3`, otherwise `[SKIP]`), and its
  `verdict.usable`, `full_os_guest` and `app_simulator` agree with the probes;
- `report` and `validate` exit identically;
- the `CANNOT_LIST` appears in help; `check` exits 0;
- `simulate --dry-run` resolves an offscreen plan when an SDK project is present
  (otherwise `[SKIP]`);
- `boot --arch aarch64` either prints a real backend command or refuses with an
  honest message — never a fake success.

It prints `[PASS]` / `[FAIL]` / `[SKIP]` lines, a summary, and `RESULT: selftest
PASS/FAIL`. Exit `0` only with zero failures. A sanitized host with no
Lima/QEMU/SDK produces `report` with `usable: false`, `validate` exit `1`, and
`selftest` still passes because its claims match that host.

## Honest limits (on every host)

- No ARM64 macOS binaries, Metal shaders or SwiftUI (no Apple SDK/frameworks).
- No modem, SIM, telephony or SMS (no `ModemManager`/ofono in v1).
- No touch gestures, pinch/zoom or virtual keyboard (`zwp_text_input_v3`).
- No real DRM GPU frame pacing or multi-monitor layout.
- No Secure Boot, `dm-verity` or TPM2 verified boot.

A simulator run is never device verification. In this tree see
[`docs/hardware.md`](hardware.md) for verified environments,
[`docs/sdk/simulator.md`](sdk/simulator.md) for form-factor presets and their
limits, and [`docs/sdk/audit.md`](sdk/audit.md) for the SDK gaps that a
simulator cannot close.
