# FinnOS AArch64 synchronous exceptions

> Implementation: integrated and locally reverified R4.1 QEMU `virt`/AAVMF slice

The ARM64 kernel measures `CurrentEL` and currently accepts only EL1. Assembly
masks DAIF before the first Rust/MMIO work, then the kernel installs a resident,
enables `CPACR_EL1.FPEN`, executes `ISB`, and verifies the setting before it
installs a resident, 2 KiB-aligned `VBAR_EL1` and checks the `BootInfo` pointer. All 16
architectural vector slots enter an architecture-specific 816-byte frame that
preserves `x0`–`x30`, `q0`–`q31`, `FPCR`, `FPSR`, `ELR_EL1`, `SPSR_EL1`,
`ESR_EL1`, `FAR_EL1`, and the vector source. Entry and Rust dispatch remain
allocation-free and keep a 16-byte-aligned stack.

The isolated exception image arms `BRK #0xf100`, verifies a same-EL synchronous
breakpoint syndrome, advances `ELR_EL1` by exactly four bytes, restores the raw
frame, and resumes. A separate unarmed-breakpoint image verifies that source,
ESR, ELR, FAR, SPSR, and x0 diagnostics are emitted before the fatal path exits
with failure status and does not return. Semihosting is used only to terminate
bounded QEMU tests.

This exception slice does not define a shared trap trait, implement generic
timer/task state, handle user faults, or establish physical-hardware support.
The later R4.2-R4.4 slices consume the memory map, activate an owned MMU, and
dispatch a bounded pinned-GICv2 self-SGI while leaving production IRQ delivery
masked. FIQ, SError, lower-EL entries, and unrecognized synchronous exceptions
remain fatal-only.

The supported firmware handoff is EL1. A higher-level monitor must not trap
EL1 FP/SIMD access; alternate EL2 entry and trap policies are not supported by
this slice.

Verification:

```bash
cargo test -p finn-kernel
cargo clippy -p finn-kernel --bin finn-kernel-aarch64 \
  --features kernel-bin,qemu-test-exit,qemu-test-exceptions \
  --target aarch64-unknown-none -- -D warnings
./tools/finn test-boot --target arm64-qemu
./tools/finn test-exceptions --target arm64-qemu
./tools/finn test-arm64-exception-fatal --target arm64-qemu
./tools/finn test-exceptions
```
