# Repository layout

> Status: Current and planned layout
> Implementation: Planned directories are intentionally not created

Current areas include `kernel/`, `boot/`, `architecture/`, `build/`, `docs/`, `tests/`, `tools/`, and `.github/`. The root Cargo workspace currently contains only `kernel`.

Future directories are created when their first meaningful implementation or specification is introduced:

- `platform/` — hardware platform integration
- `drivers/` — driver implementations
- `userspace/` — user-space runtime and startup
- `services/` — system services
- `libraries/` — shared libraries
- `frameworks/` — native frameworks
- `peony/` — Peony platform
- `sdk/` — developer SDK
- `system-apps/` — native applications
- `recovery/` — recovery environment
- `compatibility/` — optional compatibility environments
- `ports/` — hardware ports
- `examples/` — examples
- `images/` — image definitions and outputs
- `boot/protocol/` — shared versioned firmware handoff
- `boot/uefi/` — UEFI boot-manager package and ELF validation
- `tools/finnlib/` — host build, image, QEMU, and toolchain helpers
- `tools/tests/` — host-side tooling tests
