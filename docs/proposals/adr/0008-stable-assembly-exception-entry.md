# ADR 0008: Stable Assembly Exception Entry

## Status

Accepted

## Context

FinnOS needs x86-64 exception handling early in kernel initialization, before memory management, scheduling, or interrupt controllers are available. Rust's `extern "x86-interrupt"` ABI is only available on nightly and would tie the project to an unstable compiler feature. We want to stay on stable Rust while still installing a full GDT, TSS, IDT, and exception handlers.

## Decision

Use stable Rust plus minimal hand-written x86-64 assembly stubs for exception entry. The assembly boundary normalizes hardware exception frames and then calls a Rust dispatch function. Descriptor encoders and frame-layout helpers are kept safe and host-testable.

## Consequences

- The kernel remains on stable Rust.
- The assembly entry path is small, explicit, and documented.
- All privileged instructions (`lgdt`, `ltr`, `lidt`, `iretq`) are isolated with `SAFETY:` explanations.
- Host tests can verify descriptor encodings and frame layout without executing hardware instructions.

## Alternatives considered

### Nightly `extern "x86-interrupt"`

Would simplify handler definitions but require nightly Rust and obscure the exact frame layout. Rejected to keep the project on stable Rust.

### External architecture crate

Would add a dependency and runtime cost. Rejected because the early descriptor tables are small and the project benefits from understanding the exact encoding.

### All-assembly handlers

Would avoid Rust in the exception path but make diagnostics and testability harder. Rejected in favor of a clean Rust dispatch boundary.

## References

- `kernel/src/arch/x86_64/gdt.rs`
- `kernel/src/arch/x86_64/tss.rs`
- `kernel/src/arch/x86_64/idt.rs`
- `kernel/src/arch/x86_64/exceptions.rs`
- `docs/architecture/x86_64-exceptions.md`
