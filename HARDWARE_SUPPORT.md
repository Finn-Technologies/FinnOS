# Hardware Support Strategy

FinnOS currently supports no physical hardware. The tested machine models are QEMU x86-64 `q35` with OVMF and QEMU ARM64 `virt` with AAVMF/ramfb. Those emulator paths are not general hardware support.

## Driver inventory

| Class | Current state | Scope |
|---|---|---|
| Serial | Polling COM1 output | x86/QEMU diagnostic; no input/IRQ/timeout |
| Display | UEFI GOP/ramfb framebuffer, software Peony composition, a bounded BAR-backed VirtIO-GPU control/2D setup plus Peony damage follow-up, a mapped owned display-buffer presentation path, and a verified scanout-disable/backing-detach/resource-unref teardown that then releases the owned pages | QEMU emulator evidence only; teardown is polled and a failed command retains the buffer for recovery; no continuous compositor presentation, GPU-composited frame, 3D acceleration, physical display driver, or IRQ-driven presentation |
| Interrupt controllers | PIC mask, BSP xAPIC, ARM64 GICv2 with SPI group-1/priority routing, a bounded device-handler table, and device-SPI acknowledgement and dispatch to a registered handler | x86 has no IOAPIC or MSI-X table yet, so no device interrupt is delivered on x86; the emulated ARM64 GICv2 distributor does not latch SPI pending in this environment, so SPI delivery is unproven; routing is single-BSP only, with no SMP |
| Timer | PIT calibration + local APIC | No clocksource abstraction, sleep queue, RTC, or power timers |
| PCI/PCIe | Bounded bus scan, BAR/device matching, modern VirtIO capability discovery including the ISR window, MSI and MSI-X capability discovery (confirmed 3-entry MSI-X table on the emulated GPU), per-queue MSI-X vector programming, and a bounded vector allocator | No complete resource broker, no platform MSI-X table programming, no end-to-end device interrupt delivery, no restart path |
| VirtIO | Block policy and a polled modern VirtIO-GPU control-virtqueue transport for `GET_DISPLAY_INFO`, bounded 2D resource/backing/transfer/scanout/flush setup, and one damage follow-up | No complete transport for block, continuous GPU rendering, network, or input; no physical hardware |
| Block/NVMe/AHCI | Absent | Defer physical controllers until block/VFS contracts work |
| USB/HID/input | x86 PS/2 mouse polling path; host-testable ordinary PS/2 scancode decoder | No VirtIO input, hardware keyboard polling, touch, or controller |
| Network/Wi-Fi/Bluetooth | Absent | Defer wireless until basic virtual Ethernet works |
| Audio | Absent | Post-desktop-alpha |
| Power/battery/sensors | Absent | Post-reference-hardware selection |
| ARM devices | Absent | QEMU GIC/timer/UART first |

## Strategy

Use virtual hardware to validate interfaces before physical breadth: PCI enumeration and resource ownership, IOAPIC/MSI delivery, VirtIO block, VirtIO input, VirtIO network, then a simple display path. Drivers should be isolated in userspace once user processes, IPC, DMA ownership, and restart semantics exist; minimal bootstrap mechanisms may remain in kernel with explicit rationale.

Each driver requires device matching, resource/capability declaration, bounded DMA, interrupt teardown, cancellation, reset/restart, suspend/resume behavior where relevant, malformed-device tests, and user-visible diagnostics. “Works in QEMU” is emulator support, not generic hardware support.

Select one x86-64 reference computer only after storage/input/network APIs survive virtual-device integration. Select an ARM64 reference board only after QEMU parity. Publish firmware versions, exact device IDs, unsupported variants, and automated/manual test results before calling any machine supported.
