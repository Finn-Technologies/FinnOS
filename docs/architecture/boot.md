# First-boot architecture

> Status: Implemented x86-64 path; ARM64 R3 integrated and R4.1-R4.4 locally verified
> Implementation: x86-64 kernel foundation plus ARM64 serial, exception, early-memory, owned-MMU, and pinned GICv2 slices

The implemented boundary is UEFI firmware → FinnOS Boot Manager → separately linked ELF64 Finn Kernel. The boot manager validates the kernel, loads `PT_LOAD` ranges into interim identity-mapped physical addresses beginning at 32 MiB, gathers the UEFI memory map and GOP framebuffer, retains their backing storage, exits boot services, and transfers a version-three `BootInfo` pointer using the architecture ABI. The tested OVMF configuration rejected the 16 MiB range, so the relocation is explicit in the linker script and documentation.

Version two introduced `boot_info_storage`; version three requires it to name the exact page allocated for the `BootInfo` structure and defines exact absent-resource encodings. The kernel uses this range to copy and reserve its own handoff storage when classifying memory.

The x86 kernel enters on a dedicated 64 KiB stack; ARM64 uses a guarded 256 KiB early stack. Both initialize exception state, copy a strictly validated handoff, classify memory, construct the early allocator, and install private supervisor-only page tables. x86 then installs a heap, starts a BSP timer and cooperative scheduler, renders a diagnostic framebuffer, and idles. The current trust model assumes firmware and the loader’s mappings until each architecture activates its own tables; verified boot is future work.

The interim address layout is QEMU-only. A bounded physical allocator, kernel-only virtual memory, BSP timer, and cooperative ring-0 scheduler exist on x86-64. There is no user space, device IRQ routing, driver framework, storage, networking, or Peony implementation.

The ARM64 path is intentionally smaller: AAVMF loads `BOOTAA64.EFI`, the
loader validates an AArch64 ELF and transfers `BootInfo` using AAPCS64, and the
kernel switches to its linked early stack before writing PL011 entry/ready
markers. R4.1 masks DAIF at assembly entry, owns and verifies EL1 FP/SIMD
access, confirms EL1, installs a resident VBAR, preserves GPR/FP/SIMD state, resumes one controlled `BRK`, and verifies a
separate bounded fatal path. R4.2 copies the handoff, validates the full `BootInfo` page and
protected ranges, classifies the final UEFI map, and constructs/tests the shared
page allocator. R4.3 installs four-level 4 KiB low-48-bit TTBR0 tables with
EL1-only W^X, null and stack guards, and exact translation/permission abort
probes. R4.4 maps and initializes the pinned QEMU GICv2 and proves one self-SGI
acknowledge/EOI cycle. Broad exception recovery, generic timer, task context,
external interrupt discovery/routing, and shutdown remain R4 work.
