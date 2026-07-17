# Porting FinnOS

## Port contract

An architecture port must provide a loader target, kernel entry and linker layout, exception entry, page tables/MMU, interrupt controller, monotonic timer, context switching, serial diagnostics, QEMU image/firmware support, and all architecture-neutral test hooks. Architecture-independent policy must not gain target-specific constants or firmware assumptions.

## ARM64 status

ARM64 R3 serial first boot is locally verified: the build produces `BOOTAA64.EFI` and a minimal AArch64 kernel, stages a FAT image, boots it with QEMU `virt` and AAVMF, writes through the PL011, and exits the bounded test through AArch64 semihosting. The CI job is implemented but pending integration evidence. There is still no exception vector, FinnOS-owned MMU, GIC, generic timer, context switch, memory initialization, or production shutdown path; those are R4 work.

## Parity matrix

| Capability | x86-64 | ARM64 requirement |
|---|---|---|
| Boot | UEFI ELF loader verified | `BOOTAA64.EFI`, AArch64 ELF validation and handoff locally verified |
| CPU init | Long mode inherited; feature checks | EL selection, feature registers, FP/SIMD policy |
| Page tables | 4-level 4 KiB, private CR3 | Translation regime, MAIR/TCR/TTBR, barriers, W^X |
| Exceptions | GDT/TSS/IDT + assembly stubs | VBAR vector table and normalized trap frame |
| Interrupts | BSP xAPIC, PIC masked | GICv2/v3 discovery and CPU/distributor init |
| Timer | PIT-calibrated local APIC | Architectural generic timer |
| Context switch | SysV64 callee-saved state | AAPCS64 integer state, later FP/SIMD |
| SMP | Not implemented | Defer on both; design shared topology interface first |
| System calls/userspace | Not implemented | Shared ABI semantics with architecture entry glue |
| Device discovery | ACPI RSDP only, unparsed | Device tree and/or ACPI policy must be explicit |
| Emulator | QEMU `q35` + OVMF | QEMU `virt` + AAVMF/EDK2 locally verified for serial entry |
| Hardware | None | None until an explicit reference board is selected |

## Ordered ARM64 bring-up

1. **Done locally:** target/profile metadata selects architecture-specific artifacts without changing the external ESP contract.
2. **Done locally:** the UEFI loader validates the selected ELF machine and hands off `BootInfo` with AAPCS64.
3. **Done locally, CI pending:** linker script, entry assembly, early stack, PL011, panic path, and serial marker run in QEMU `virt`.
4. Normalize exception frames behind a shared trap API.
5. Classify the UEFI memory map using existing shared code.
6. Implement MMU mappings matching x86 supervisor W^X/guard invariants.
7. Add GIC and generic timer, then pass architecture-neutral timer tests.
8. Add AAPCS64 task context and pass task-state tests.
9. Require both ports in CI before shared user ABI, IPC, or driver interfaces stabilize.

ARM64 must not wait until after the full desktop; early parity is an architecture-validation tool. SMP, phone hardware, battery, radio, and GPU work remain out of scope for initial parity.
