# ADR 0011: Initial x86-64 address space

## Decision

Use four-level x86-64 paging with 4 KiB leaves, an identity-mapped initial layout, a fixed-capacity physical page-table pool supplied by the existing allocator, and an explicit validated mapping plan. All mappings are supervisor-only and W^X. Enable EFER.NXE and CR0.WP, leave the null page and early-stack guard pages absent, use conservative uncached framebuffer flags, and defer page-table reclamation, user address spaces, and higher-half relocation.

## Alternatives

Continuing with firmware tables leaves ownership and permissions ambiguous. Static tables embedded in the image waste fixed kernel space. Mapping all physical RAM weakens isolation. Immediate higher-half relocation increases boot risk. Huge pages complicate permissions and allocation. Recursive mappings add a permanent virtual-space convention. Dynamically allocated metadata would require a heap that does not exist yet.

The selected design is small, deterministic, testable in QEMU, and appropriate for the early single-core kernel phase.
