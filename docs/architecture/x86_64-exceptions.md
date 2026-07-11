# FinnOS x86-64 exception foundation

> Status: Accepted
> Implementation: GDT, TSS, IDT, and assembly exception stubs are installed in kernel_main

This document describes the early x86-64 exception-handling architecture in FinnOS. It covers the GDT, TSS, IDT, exception-entry stubs, register and error-code normalization, fatal versus resumable exceptions, and the current test strategy.

## Goals

- Provide FinnOS-owned descriptor tables before any memory management or scheduling.
- Deliver deterministic COM1 diagnostics for kernel exceptions.
- Reserve a dedicated double-fault IST stack so a stack overflow does not silently triple-fault.
- Keep the implementation on stable Rust with small, documented assembly boundaries.

## Non-goals

- External IRQ routing or APIC initialization.
- Timer interrupts.
- User-mode exceptions or signal delivery.
- Page-fault recovery or FinnOS-owned page tables.
- Scheduler support.

## GDT role

The FinnOS GDT contains:

- Null descriptor (entry 0)
- Kernel 64-bit code descriptor (entry 1, selector `0x08`)
- Kernel data descriptor (entry 2, selector `0x10`)
- 64-bit available TSS descriptor (entries 3 and 4, selector `0x18`)
- Spare entries for future expansion

The GDT is loaded once on the BSP with `lgdt`, segment registers are reloaded, and the task register is loaded with `ltr`. Selectors are defined as constants in `kernel/src/arch/x86_64/gdt.rs` and are not hard-coded elsewhere.

## TSS role

The Task State Segment provides:

- `RSP0`: the kernel stack top used when an interrupt or exception transitions from a lower privilege level.
- `IST[0]` (IST1): the top of a dedicated 64 KiB, page-aligned double-fault stack.
- `io_map_base`: set to the TSS size so no I/O-permission bitmap is exposed.

The TSS is zero-initialized, its reserved fields are explicitly set to zero, and it remains permanently resident in static storage.

## IST and the dedicated double-fault stack

A double fault can occur when the current stack is exhausted or corrupted. To avoid a triple fault, the double-fault handler runs on a separate IST stack. The stack is 64 KiB, aligned to a page boundary, and its top is stored in TSS IST1. The IDT gate for vector 8 (double fault) uses IST index 1.

## IDT gate structure

The IDT has 256 entries. Each gate encodes:

- 64-bit handler offset
- Kernel code selector
- IST index
- Gate attributes (interrupt or trap gate, present, ring 0)

The breakpoint handler (vector 3) is installed as a trap gate so it can be resumed. Other early handlers are interrupt gates.

## Exception entry stubs

Stable Rust does not provide `extern "x86-interrupt"`, so small assembly stubs in `kernel/src/arch/x86_64/exceptions.rs` normalize the hardware frame:

- Save all general-purpose registers.
- Push a synthetic zero error code for vectors that do not provide one.
- Call the Rust dispatch function with a pointer to the normalized `ExceptionFrame`.
- On return, restore registers and execute `iretq`.

The assembly stubs preserve the CPU-pushed `rip`, `cs`, `rflags`, and (when present) `rsp` and `ss`.

## Normalized error-code handling

The `ExceptionFrame` contains a synthetic zero for vectors without a hardware error code. Vectors with hardware error codes keep the value pushed by the CPU. The Rust dispatch function uses this value for diagnostics and for decoding page-fault flags.

## Register preservation and stack alignment

The assembly stubs push registers in a fixed order, and the `ExceptionFrame` layout is verified with compile-time offset assertions. The stack is aligned before calling Rust, and the direction flag is cleared implicitly by the `iretq` path.

## Rust dispatch boundary

`rust_exception_dispatch` receives a pointer to the normalized frame and routes by vector number. Handlers are split into:

- **Resumable**: breakpoint returns through `iretq`.
- **Fatal**: invalid opcode, double fault, general-protection fault, page fault, and unhandled vectors print diagnostics and halt or exit QEMU.

## Page-fault CR2 reporting

The page-fault handler reads `CR2` to obtain the faulting address and decodes the architectural error-code flags (present/protection violation, read/write, user/supervisor, reserved-bit violation, instruction fetch, protection key, shadow stack). It does not attempt recovery because FinnOS does not yet own page tables.

## Fatal versus resumable exceptions

Only the breakpoint handler is resumable during this milestone. All other implemented handlers are fatal: they print `FINNOS:EXCEPTION:FATAL`, emit category-specific markers, and either halt with interrupts disabled or write the QEMU debug-exit failure value.

## Current single-core assumptions

Descriptor tables are initialized once on the BSP. No per-CPU data structures exist yet, and exception handlers do not acquire locks or allocate memory.

## Current lack of user mode

All handlers run in ring 0. User-mode exceptions, system calls, and ring transitions are not implemented.

## Current lack of IRQ handling

No external interrupt controller is initialized. The IDT contains only CPU exception vectors 0-31.

## Security implications

- The GDT, TSS, and IDT are stored in static memory and are never written after init.
- The double-fault IST stack is isolated from the normal kernel stack.
- The TSS I/O map base is set so no I/O-permission bitmap is exposed.
- All privileged instructions are isolated in small functions with `SAFETY:` explanations.

## Unsafe invariants

- `init_exception_foundation` must run exactly once on the BSP with interrupts disabled.
- The assembly stubs and Rust frame layout must match exactly.
- The TSS and GDT must remain valid for the lifetime of the kernel.
- `iretq` is only used for resumable handlers.

## Test strategy

- Host-side tests verify descriptor encodings, selector calculation, IDT offset reconstruction, exception-frame layout, and test-state transitions.
- `./tools/finn test-boot` verifies that normal First Boot still reaches `FINNOS:KERNEL:FIRST_BOOT_COMPLETE`.
- `./tools/finn test-exceptions` builds a separate image with the `qemu-test-exceptions` feature and verifies controlled breakpoint and invalid-opcode behavior, exiting with status 33.
Exception delivery continues under the FinnOS-owned identity map. The page-table test reserves a single expected non-present supervisor read state; unrelated faults remain fatal.
