# Hardware Support Strategy

FinnOS currently supports no physical hardware. The only tested machine model is QEMU x86-64 `q35` with OVMF, an IDE-attached boot image, COM1, GOP, PIT, and xAPIC. Those platform primitives are not a general driver stack.

## Driver inventory

| Class | Current state | Scope |
|---|---|---|
| Serial | Polling COM1 output | x86/QEMU diagnostic; no input/IRQ/timeout |
| Display | UEFI GOP framebuffer handoff and fill | Firmware mode only; no display driver |
| Interrupt controllers | PIC mask, BSP xAPIC | No IOAPIC, MSI/MSI-X, GIC, or device IRQs |
| Timer | PIT calibration + local APIC | No clocksource abstraction, sleep queue, RTC, or power timers |
| PCI/PCIe | Absent | Required for x86 device discovery |
| VirtIO | Absent | Preferred first virtual block/network/input devices |
| Block/NVMe/AHCI | Absent | Defer physical controllers until block/VFS contracts work |
| USB/HID/input | Absent | No keyboard, mouse, touch, or controller |
| Network/Wi-Fi/Bluetooth | Absent | Defer wireless until basic virtual Ethernet works |
| Audio | Absent | Post-desktop-alpha |
| Power/battery/sensors | Absent | Post-reference-hardware selection |
| ARM devices | Absent | QEMU GIC/timer/UART first |

## Strategy

Use virtual hardware to validate interfaces before physical breadth: PCI enumeration and resource ownership, IOAPIC/MSI delivery, VirtIO block, VirtIO input, VirtIO network, then a simple display path. Drivers should be isolated in userspace once user processes, IPC, DMA ownership, and restart semantics exist; minimal bootstrap mechanisms may remain in kernel with explicit rationale.

Each driver requires device matching, resource/capability declaration, bounded DMA, interrupt teardown, cancellation, reset/restart, suspend/resume behavior where relevant, malformed-device tests, and user-visible diagnostics. “Works in QEMU” is emulator support, not generic hardware support.

Select one x86-64 reference computer only after storage/input/network APIs survive virtual-device integration. Select an ARM64 reference board only after QEMU parity. Publish firmware versions, exact device IDs, unsupported variants, and automated/manual test results before calling any machine supported.
