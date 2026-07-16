# Driver Development

> Status: blocked; no driver ABI or userspace exists.

Do not add device-specific drivers before platform discovery, interrupt-resource ownership, IPC, process isolation, DMA policy, and restart semantics are defined. Polling serial, xAPIC/PIT, and GOP bootstrap code are platform primitives, not a reusable driver framework.

The first driver program should target VirtIO under QEMU and prove enumeration, bounded MMIO/PIO access, IRQ mask/ack/teardown, DMA bounds, reset, cancellation, driver crash/restart, malformed descriptors, and resource revocation. See [HARDWARE_SUPPORT.md](../../HARDWARE_SUPPORT.md) and roadmap R5/R10.

Every future driver must document supported device IDs/versions, ownership, concurrency, DMA/IOMMU assumptions, firmware, power lifecycle, failure behavior, security boundary, tests, and unsupported variants.
