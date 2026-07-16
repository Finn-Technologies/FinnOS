# Porting FinnOS

## Port contract

An architecture port must provide a loader target, kernel entry and linker layout, exception entry, page tables/MMU, interrupt controller, monotonic timer, context switching, serial diagnostics, QEMU image/firmware support, and all architecture-neutral test hooks. Architecture-independent policy must not gain target-specific constants or firmware assumptions.

## ARM64 status

ARM64 is 0% at executable level. `build/targets/arm64-qemu.toml` and `Finnfile.toml` only mark intent. There is no AArch64 UEFI binary, kernel binary, linker script, exception vector, MMU, GIC, generic timer, context switch, UART, image flow, QEMU command, CI job, or test.

## Parity matrix

| Capability | x86-64 | ARM64 requirement |
|---|---|---|
| Boot | UEFI ELF loader verified | `BOOTAA64.EFI`, AArch64 ELF validation and handoff |
| CPU init | Long mode inherited; feature checks | EL selection, feature registers, FP/SIMD policy |
| Page tables | 4-level 4 KiB, private CR3 | Translation regime, MAIR/TCR/TTBR, barriers, W^X |
| Exceptions | GDT/TSS/IDT + assembly stubs | VBAR vector table and normalized trap frame |
| Interrupts | BSP xAPIC, PIC masked | GICv2/v3 discovery and CPU/distributor init |
| Timer | PIT-calibrated local APIC | Architectural generic timer |
| Context switch | SysV64 callee-saved state | AAPCS64 integer state, later FP/SIMD |
| SMP | Not implemented | Defer on both; design shared topology interface first |
| System calls/userspace | Not implemented | Shared ABI semantics with architecture entry glue |
| Device discovery | ACPI RSDP only, unparsed | Device tree and/or ACPI policy must be explicit |
| Emulator | QEMU `q35` + OVMF | QEMU `virt` + AAVMF/EDK2 |
| Hardware | None | None until an explicit reference board is selected |

## Ordered ARM64 bring-up

1. Refactor the build wrapper so target/profile metadata selects artifacts without changing x86 behavior.
2. Add an AArch64 UEFI loader and protocol round-trip host tests.
3. Add linker script, entry assembly, early stack, UART, panic path, and serial marker in QEMU `virt`.
4. Normalize exception frames behind a shared trap API.
5. Classify the UEFI memory map using existing shared code.
6. Implement MMU mappings matching x86 supervisor W^X/guard invariants.
7. Add GIC and generic timer, then pass architecture-neutral timer tests.
8. Add AAPCS64 task context and pass task-state tests.
9. Require both ports in CI before shared user ABI, IPC, or driver interfaces stabilize.

ARM64 must not wait until after the full desktop; early parity is an architecture-validation tool. SMP, phone hardware, battery, radio, and GPU work remain out of scope for initial parity.
