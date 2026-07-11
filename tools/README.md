# Developer tools

> Status: Foundation tooling
> Implementation: `tools/finn` is a lightweight wrapper

`./tools/finn help` prints usage. `doctor` checks required and optional tools and repository files. `build` runs `cargo build --workspace`; `test` runs workspace tests; `format` runs rustfmt; `format-check` checks formatting; `lint` runs Clippy with warnings denied; and `check` runs format-check, Cargo check, lint, and test in order.

The wrapper may later become a compiled FinnOS development CLI.
