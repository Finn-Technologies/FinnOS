# Contributing

> Status: Contribution guidance
> Implementation: Applies to the current scaffold

## Prerequisites

Install Git, Rust stable, Cargo, rustfmt, Clippy, and Python 3. QEMU is optional because no bootable image exists.

## Workflow

Run `./tools/finn doctor`, then `./tools/finn build`, `./tools/finn test`, `./tools/finn format-check`, and `./tools/finn lint`. Keep documentation accurate about implementation status.

Use focused branches such as `kernel/capability-table`, `docs/process-lifecycle`, `build/arm64-target`, and `peony/scene-protocol`. Commit subjects should follow `area: imperative summary`, for example `kernel: add capability table design`, `docs: document process lifecycle`, `build: add ARM64 target metadata`, or `peony: define scene protocol proposal`.

Pull requests should explain motivation, testing, security and compatibility impact, and documentation changes. New public system calls, kernel objects, stable IPC protocols, application package changes, security-model changes, and on-disk format changes require an RFC. Unsafe code is forbidden in the initial crate; future unsafe blocks require a `SAFETY:` explanation and review. Security reports follow [SECURITY.md](SECURITY.md).
