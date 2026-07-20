# Supported Platforms

| Architecture/platform | Firmware | Status | Tested capabilities | Unsupported capabilities |
|---|---|---|---|---|
| x86-64 QEMU `q35`, 256 MiB | OVMF UEFI | Development target | Boot, serial, GOP handoff/fill, memory, paging, heap, xAPIC timer, cooperative tasks | Userspace, devices, storage, input, network, shutdown |
| x86-64 physical machine | UEFI expected | Unsupported/unverified | None | All hardware and reliability claims |
| ARM64 QEMU `virt,gic-version=2,secure=off`, one BSP | AAVMF/EDK2 | Integrated serial entry plus local R4.1-R4.4 exception, early-memory, MMU, and GICv2 slices | R3 integration; R4.1-R4.4 local only | Broad exception recovery, generic timer, tasks, external interrupt discovery/routing, SMP, shutdown, and all product capabilities |
| ARM64 physical machine | Unselected | Unsupported | None | All capabilities |

BIOS, Multiboot, legacy x86, 32-bit architectures, Raspberry Pi boards, Apple Silicon bare metal, phones, tablets, and secure boot are not supported. QEMU’s host architecture is not the guest support status: x86-64 QEMU was verified while running on an ARM64 macOS host.

Support means a configuration has an automated build and boot contract. It does not imply a stable ABI, data safety, security support, or suitability for use.
