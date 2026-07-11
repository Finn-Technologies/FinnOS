# FinnOS x86-64 virtual memory

FinnOS now owns the active x86-64 address space after UEFI handoff. It uses four-level paging with 4 KiB leaves only and an initial identity-mapped layout. Five-level paging is rejected, canonical virtual addresses are validated, and CPUID supplies the physical-address width and NX capability.

Page-table storage is a deterministic 64-page pool allocated through `EarlyPhysicalPageAllocator`. The pool includes the root, keeps unused pages reserved, and maps every reserved page writable and NX. A fixed-capacity mapping plan is validated before hardware construction; all mappings are supervisor-only and enforce W^X.

Kernel text is read-only executable. Read-only data and BootInfo/memory-map storage are read-only NX. Writable data, BSS, the early stack, and page-table storage are writable NX. The null page and one page on each side of the 64 KiB early stack are non-present. The framebuffer is writable NX with conservative PWT+PCD uncached policy; write combining is not claimed. ACPI RSDP storage is mapped only when present.

Activation enables EFER.NXE and CR0.WP, writes the new root to CR3, and validates translations through a software walker. Mapping changes use `invlpg`. The current assumptions are one BSP, no user address spaces, no intermediate-table reclamation, and no final higher-half layout. The next step is an early kernel heap.
