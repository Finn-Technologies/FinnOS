# Developer tools

> Status: Foundation tooling
> Implementation: `tools/finn` is a lightweight wrapper

`./tools/finn help` prints usage. `doctor` checks required and optional tools and repository files. `build` runs `cargo build --workspace`; `test` runs workspace tests; `format` runs rustfmt; `format-check` checks formatting; `lint` runs Clippy with warnings denied; and `check` runs format-check, Cargo check, lint, and test in order.

The wrapper may later become a compiled FinnOS development CLI.
`./tools/finn test-page-allocator` runs the isolated physical-page allocator QEMU test. Its image and log directory are `build/out/x86_64-qemu-page-allocator`.
`./tools/finn test-page-tables` uses a dedicated output directory and verifies the FinnOS-owned four-level address space. `./tools/finn test-heap` uses `build/out/x86_64-qemu-heap` and verifies the bounded global allocator.
`./tools/finn test-timer-interrupts` uses `build/out/x86_64-qemu-timer-interrupts` and proves repeated real local-APIC timer delivery, EOI, monotonic ticks, spurious dispatch, and interrupt-context heap protection. `check-all` includes this seventh integration test.
