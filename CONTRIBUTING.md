# Contributing

FinnOS is an early kernel project. Read [STATUS.md](STATUS.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [ROADMAP.md](ROADMAP.md) before proposing implementation. Documentation and filenames are not proof that a feature exists.

Coding agents must also follow [`.agents/README.md`](.agents/README.md), select skills through [`.agents/MANIFEST.md`](.agents/MANIFEST.md), and leave the required structured handoff.

## Setup and checks

Install Git, Rust stable with rustfmt/Clippy and the two x86 targets, Python 3, QEMU x86-64, `qemu-img`, OVMF, and the platform image tools listed in [BUILDING.md](BUILDING.md).

```bash
./tools/finn doctor
./tools/finn check
./tools/finn test-boot
python3 .agents/scripts/validate.py --all
```

Run the integration mode for every subsystem changed. `./tools/finn check-all` is the complete current x86 gate. Preserve serial logs for boot failures.

## Changes

- Keep changes focused and preserve current bounded invariants unless an accepted design replaces them.
- Use `area: imperative summary` commit subjects, such as `kernel: validate user mappings`.
- Add host tests for pure policy and QEMU tests for architecture/runtime behavior.
- Update status, architecture, acceptance criteria, and limitations in the same change.
- Never describe planned capabilities as implemented.
- Document every unsafe block with a specific `SAFETY:` argument and review affected invariants in `kernel/docs/unsafe-code.md`.

Public syscalls, kernel objects, IPC protocols, package/on-disk formats, security boundaries, and major cross-architecture abstractions require an RFC or ADR before compatibility assumptions form.

Pull requests must state motivation, current/desired behavior, scope/non-goals, architecture impact, security and compatibility impact, exact tests, documentation changes, and rollback implications. Link the roadmap/issue acceptance criteria and include evidence that they pass.

Use private reporting for vulnerabilities as described in [SECURITY.md](SECURITY.md). By contributing, you agree that your contribution is available under the repository’s MIT OR Apache-2.0 terms.
