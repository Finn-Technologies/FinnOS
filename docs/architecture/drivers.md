# Drivers

> Status: Initial architectural direction
> Implementation: No hardware driver framework exists

FinnOS prefers user-space drivers, with minimal boot-critical kernel drivers. Driver processes should be isolated and support matching, hardware-resource ownership, DMA/IOMMU considerations, interrupts, restart, suspend, and resume. Signed driver packages and a reference-hardware strategy are planned security and release concerns.

Matching policy, package format, and recovery details remain unresolved.
