# Kernel invariants

> Status: Preliminary requirements
> Implementation: Not enforced yet

Planned baseline invariants include validating user-controlled addresses; preventing stale handles from resolving to new objects; avoiding executable and writable mapping overlap by default; forbidding blocking work in interrupt context; reducing capability rights during delegation unless authorized; releasing or revoking resources on process destruction; and documenting ownership and synchronization for kernel data structures.
