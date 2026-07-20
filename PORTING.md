# Porting FinnOS

## Port contract

An architecture port must provide a loader target, kernel entry and linker layout, exception entry, page tables/MMU, interrupt controller, monotonic timer, context switching, serial diagnostics, QEMU image/firmware support, and all architecture-neutral test hooks. Architecture-independent policy must not gain target-specific constants or firmware assumptions.

## ARM64 status

ARM64 R3 serial first boot is integrated at `f74fc49`, with local and integration evidence recorded in its handoff. The locally verified R4.1-R4.4 worktree additionally owns EL1 exception vectors, strict handoff/memory ownership, a bounded FinnOS-owned EL1 address space, and a pinned single-BSP GICv2 path that delivers/acknowledges/EOIs a self-SGI. Worktree CI is pending integration. There is still no generic timer, context switch, external device routing, discovery, or production shutdown path; FIQ/SError and unarmed synchronous faults remain fatal-only.

## Parity matrix

| Capability | x86-64 | ARM64 requirement |
|---|---|---|
| Boot | UEFI ELF loader verified | `BOOTAA64.EFI`, AArch64 ELF validation and handoff locally verified |
| CPU init | Long mode inherited; feature checks | EL selection, feature registers, FP/SIMD policy |
| Page tables | 4-level 4 KiB, private CR3 | Local R4.3 four-level 4 KiB TTBR0 regime, MAIR/TCR/barriers, supervisor W^X, null/stack guards |
| Exceptions | GDT/TSS/IDT + assembly stubs | Local R4.1 VBAR/raw frame/controlled `BRK`; broader normalized trap semantics remain future work |
| Physical memory | Shared UEFI classifier and extent allocator | Local R4.2 parity feeding allocator-owned R4.3 table storage; no LoaderData reclaim |
| Interrupts | BSP xAPIC, PIC masked | Local R4.4 pinned GICv2 distributor/CPU init and self-SGI; discovery/external routing absent |
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
3. **Integrated:** linker script, entry assembly, early stack, PL011, panic path, and serial marker run in QEMU `virt`.
4. **Done locally, CI pending:** detect EL1, install the architecture-specific raw exception frame/VBAR, and prove a controlled synchronous `BRK`. Defer a shared normalized trap API until another consumer proves its required semantics.
5. **Done locally, CI pending:** classify the UEFI memory map using existing shared code.
6. **Done locally, CI pending:** install four-level 4 KiB identity mappings matching x86 supervisor W^X/guard semantics, with ARM memory attributes and real abort probes.
7. **GIC done locally, CI pending:** initialize pinned single-BSP GICv2 and prove exact SGI acknowledge/EOI; next add the architectural generic timer and pass architecture-neutral timer semantics.
8. Add AAPCS64 task context and pass task-state tests.
9. Require both ports in CI before shared user ABI, IPC, or driver interfaces stabilize.

ARM64 must not wait until after the full desktop; early parity is an architecture-validation tool. SMP, phone hardware, battery, radio, and GPU work remain out of scope for initial parity.
