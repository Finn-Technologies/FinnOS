# Unsafe code policy

> Status: Initial policy
> Implementation: The initial crate forbids unsafe code

The protocol and pure helper modules forbid unsafe code. The kernel hardware boundary contains narrowly scoped assembly and raw-pointer operations for descriptor tables, segment reload, task-register load, and exception entry. Every unsafe block has a nearby `SAFETY:` explanation; unsafe modules must document invariants; unsafe code should be minimized and reviewed; and architecture assembly boundaries require documentation and tests. Descriptor encoders and exception-frame layout helpers are kept safe and host-testable.

Paging adds only validated raw-pointer volatile access to identity-mapped table pages, CPUID, CR0/CR3/CR4, EFER, and `invlpg`. Active tables are never exposed as ordinary mutable references.

The physical page allocator is safe Rust and does not access or write allocated
physical pages. It returns addresses only; no new unsafe code is required for
its metadata or validation logic.
