# FinnOS AArch64 virtual memory

> Implementation: integrated and locally reverified R4.3 QEMU `virt`/AAVMF slice; physical hardware is unverified

The ARM64 kernel replaces the inherited UEFI translation regime after validating the v3 handoff, classifying physical memory, and constructing the early page allocator. The initial FinnOS-owned address space is a low, identity-mapped, EL1-only regime. It uses four levels of 4 KiB translation tables through `TTBR0_EL1`, supports physical-address widths reported by `ID_AA64MMFR0_EL1` through 48 bits, disables `TTBR1_EL1` walks, and uses page descriptors rather than block mappings.

The fixed 64-page translation-table pool is reserved transactionally from classified `Usable` memory. Construction does not publish allocator changes until all address, CPU-capability, alias, descriptor, capacity, inherited-identity, and mapping checks succeed. Once activated, all pool pages remain reserved for the lifetime of this immutable early address space; this slice has no dynamic mapping, reclamation, ASID, or TLB-shootdown API.

## Initial mappings

Only resources required after activation are mapped:

- kernel text is privileged read-only/executable and EL0 execute-never;
- rodata, the copied-handoff backing page, used UEFI map storage, and optional RSDP page are privileged read-only and execute-never;
- data, BSS, the 256 KiB early stack, and table-pool pages are privileged read-write and execute-never;
- QEMU PL011 at `0x0900_0000` is privileged read-write, execute-never Device-nGnRnE memory;
- an optional framebuffer is privileged read-write, execute-never Normal non-cacheable memory;
- page zero and one protected 4 KiB page on each side of the early stack are unmapped.

Mapping-plan validation rejects unaligned or overflowing ranges, zero pages, unsupported address widths, writable-executable leaves, conflicting virtual mappings, and physical aliases with different permissions or memory types. `SCTLR_EL1.WXN` adds defense in depth to leaf-level W^X; all leaves deny EL0 access with AP/UXN policy.

## Activation

Before changing registers, software walking proves required identity translations and `AT S1E1R` proves that the inherited regime resolves the live PC, SP, VBAR, kernel resources, handoff storage, and PL011 to the same physical addresses. Used descriptor pages are cleaned to the point of coherency. A stack-free assembly sequence then briefly clears `SCTLR_EL1.M`, installs MAIR/TCR/TTBR0, zeros TTBR1, invalidates EL1 translations, and enables `M`, `C`, `I`, `SA`, and `WXN` with DSB/ISB ordering. Exact TTBR0/TCR/MAIR/SCTLR state and zero TTBR1 are read back before the address space is accepted. Post-switch PL011 output is runtime evidence that the Device mapping remains usable.

The transition is safe only because the supported QEMU/UEFI profile loads the kernel at its linked physical address and provides inherited identity translations until the switch. The implementation rejects a non-identity live translation, an unsupported 4 KiB granule/PARange, or big-endian EL1. This is not evidence for alternate firmware or hardware cache/coherency behavior.

## Fault evidence and limitations

The ARM page-table QEMU mode software-validates representative translations, table ownership, null absence, both guards, and PL011 attributes. It then uses isolated assembly stubs and an allocation-free armed-fault record to prove four hardware violations:

1. reading page zero produces a current-EL data translation abort;
2. reading the lower stack guard produces a current-EL data translation abort;
3. writing kernel text produces a write data permission abort;
4. branching to a temporary instruction in writable BSS produces an instruction permission abort.

The exception dispatcher resumes only when source, ESR exception/FSC class, WnR, FAR, ELR, state, and explicit resume symbol all match. Any mismatch remains fatal. The data cell is restored after the NX test.

The guards catch ordinary out-of-range accesses, but they do not make a true exhausted-stack exception recoverable because the current vector entry saves its frame on SP_EL1. A dedicated exception stack is required before claiming robust stack-overflow diagnostics. Higher-half mappings, EL0/user spaces, ASIDs, dynamic mapping, table reclamation, SMP shootdowns, KASLR, generic timer, task context, physical hardware, and UEFI LoaderData reclamation remain future work. The pinned QEMU GICv2 windows are now included as Device mappings, but discovery and dynamic device mappings are not.
