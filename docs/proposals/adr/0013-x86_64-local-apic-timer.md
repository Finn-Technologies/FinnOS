# ADR 0013: x86-64 BSP local APIC timer

## Decision

Use the BSP local APIC in xAPIC MMIO mode, with its physical base read from
IA32_APIC_BASE and mapped at a fixed high virtual address with PCD+PWT
permissions. Remap and fully mask the legacy PIC. Use timer vector `0x40`,
spurious vector `0xff`, periodic local APIC mode, 100 Hz, and polled PIT
channel 2 calibration. Expose an atomic monotonic tick count and prohibit
heap operations in interrupt context. Use a stable-Rust, single-BSP,
interruptible idle loop.

## Alternatives

Remaining interrupts disabled, PIT IRQ0, HPET, TSC-deadline mode, x2APIC,
immediate MADT/IOAPIC support, a QEMU synthetic clock, busy-loop timing, or
combining scheduler implementation with this milestone were rejected. The
chosen design provides real QEMU local-APIC delivery while keeping the current
Kernel Core surface bounded and testable.

## Consequences

This is x86-64 UEFI QEMU support only. MADT parsing is required before external
IRQ routing or SMP, and this timer foundation is not a scheduler, preemption
system, wall clock, or device-driver framework.
