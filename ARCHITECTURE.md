# FinnOS Architecture

This document maps the implemented system. `docs/architecture/` is the canonical location for subsystem details and `docs/proposals/adr/` records decisions. Text describing IPC, capabilities, processes, drivers, or Peony is prospective unless it explicitly cites implementation.

## Implemented component map

```text
UEFI firmware
  filesystem -> boot/uefi -> validates and loads KERNEL.ELF
  GOP --------^          -> framebuffer handoff
  ACPI tables -^          -> RSDP handoff
  memory map --^          -> boot/protocol::BootInfo v2
                                  |
                                  v
kernel/src/bin/x86_64.rs
  entry stack -> GDT/TSS/IDT -> memory classification -> physical allocator
       -> private page tables -> fixed heap -> PIC mask + xAPIC timer
       -> guarded cooperative tasks -> framebuffer diagnostic -> idle task
```

The loader and kernel are separate ELF/PE artifacts. The loader runs as an x86-64 UEFI application, loads the kernel at its linked physical addresses, exits boot services, and enters `_start` using SysV64 with a `BootInfo` pointer in `RDI`.

## Boot flow

1. UEFI starts `BOOTX64.EFI` from the FAT image.
2. `boot/uefi/src/main.rs` opens `EFI/FINNOS/KERNEL.ELF`, validates ELF64/x86-64 headers, allocates and copies load segments, obtains GOP and the ACPI RSDP, captures the final memory map, and exits boot services.
3. `kernel/src/bin/x86_64.rs::_start` disables interrupts, selects the linker-defined stack, and calls `kernel_main`.
4. The kernel installs a ring-0 GDT, TSS with double-fault IST, and exception IDT before trusting handoff data.
5. It validates `BootInfo`, classifies the UEFI map, excludes kernel/handoff/framebuffer ranges, and creates a bounded extent allocator.
6. It builds supervisor-only four-level page tables with W^X, NX, null protection, and stack guards, then sets NXE/WP and switches CR3.
7. It maps a guarded 1 MiB heap, masks both PICs, maps xAPIC, calibrates against PIT channel 2, and enables a 100 Hz local timer.
8. It initializes eight fixed task slots, guarded stacks, a FIFO cooperative scheduler, and an idle task.
9. It fills the GOP framebuffer and enters scheduler-backed idle. No userspace startup follows.

## Runtime flows

| Flow | Current implementation |
|---|---|
| Userspace startup | None |
| Application execution | None; only statically linked ring-0 functions can become kernel tasks |
| Input | None |
| Graphics | Loader provides GOP metadata; kernel writes a full-screen diagnostic fill |
| Storage | Firmware reads the kernel before `ExitBootServices`; no runtime storage flow |
| Networking | None; QEMU networking is disabled |
| Interrupts | Exceptions plus local APIC timer/spurious vectors; no device IRQ routing |
| Scheduling | Explicit `yield`/exit, single BSP, shared address space; timer does not preempt |

## Code boundaries

Architecture-independent code currently includes the boot protocol, UEFI descriptor decoding/classification, physical extent allocator, heap allocation policy, task state machine, and interrupt-depth concept. x86-specific code includes entry, linker layout, GDT/TSS/IDT, exception assembly, page tables, APIC/PIC/PIT, timer, task stacks, context switching, QEMU exit, and serial I/O.

The intended long-term boundary is:

- Kernel: scheduling, address spaces, interrupts, minimal IPC/capability primitives, and resource arbitration.
- User services: device management, drivers where feasible, filesystems, network stack, display server, and system policy.
- Peony: display/compositor protocol, shell, application framework, input routing, text, and accessibility.

None of the user-service boundary is implemented. It must be validated incrementally with one user process, one IPC endpoint, and one virtual device before broader abstractions are stabilized.

## Invariants and constraints

- All runtime code is ring 0 and shares one address space.
- User mappings are rejected.
- Heap allocation is forbidden in interrupt context.
- Scheduler mutation is forbidden in interrupt context.
- The timer is BSP-local and xAPIC-only.
- Capacity is intentionally bounded: 8 tasks, 1 MiB heap, 64 page-table pages, and fixed region/extent arrays.
- ACPI is handed off but not parsed.
- No firmware memory is reclaimed after boot.

## Detailed references

- [Boot](docs/architecture/boot.md)
- [Kernel](docs/architecture/kernel.md)
- [Memory map](docs/architecture/physical-memory-map.md)
- [Physical allocation](docs/architecture/physical-page-allocation.md)
- [Virtual memory](docs/architecture/x86_64-virtual-memory.md)
- [Interrupts and timer](docs/architecture/x86_64-interrupts-and-timer.md)
- [Cooperative tasks](docs/architecture/cooperative-kernel-tasks.md)
- [Planned processes](docs/architecture/processes.md)
- [Planned IPC and capabilities](docs/architecture/ipc.md)
- [Planned drivers](docs/architecture/drivers.md)
- [Planned Peony](docs/architecture/peony.md)
