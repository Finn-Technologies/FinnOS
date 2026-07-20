# Kernel Change

- [ ] Preserve early no-heap/no-interrupt assumptions at each initialization stage.
- [ ] Review pointer, integer, alignment, aliasing, stack, and page ownership.
- [ ] Review interrupt/preemption/SMP safety and lock ordering.
- [ ] Keep user/supervisor and W^X/NX/guard invariants explicit.
- [ ] Map architecture-specific behavior to both ports or document the intentional gap.
- [ ] Run host policy tests and relevant feature-specific QEMU test.
- [ ] Preserve bounded serial diagnostics without allocating in forbidden contexts.
