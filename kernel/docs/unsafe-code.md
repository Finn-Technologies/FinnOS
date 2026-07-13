# Unsafe code policy

> Status: Initial policy
> Implementation: The initial crate forbids unsafe code

The protocol and pure helper modules forbid unsafe code. The kernel hardware boundary contains narrowly scoped assembly and raw-pointer operations for descriptor tables, segment reload, task-register load, and exception entry. Every unsafe block has a nearby `SAFETY:` explanation; unsafe modules must document invariants; unsafe code should be minimized and reviewed; and architecture assembly boundaries require documentation and tests. Descriptor encoders and exception-frame layout helpers are kept safe and host-testable.

Paging adds only validated raw-pointer volatile access to identity-mapped table pages, CPUID, CR0/CR3/CR4, EFER, and `invlpg`. Active tables are never exposed as ordinary mutable references.

The physical page allocator is safe Rust and does not access or write allocated
physical pages. It returns addresses only; no new unsafe code is required for
its metadata or validation logic.

The early heap contains the narrow unsafe boundary for in-band free-list nodes, `UnsafeCell`,
`GlobalAlloc`, and zeroing the already-validated virtual heap range. Nodes are written only inside
validated free regions, the global heap is initialized once, and allocation is not supported from
interrupt context.
# Interrupt and MMIO unsafe boundaries

Interrupt stubs, `iretq`, port I/O, APIC MSR access, volatile APIC MMIO, and
IF/HLT instructions are isolated in x86-64 modules. Their comments document
the ring-0 frame layout, bounded offsets, byte/32-bit widths, and the required
mapped permissions. No ordinary Rust reference aliases the APIC register page.
# Interrupt-context unsafe boundary

The assembly entry is the only live-frame boundary: it saves/restores all GPRs,
validates the canonical 184-byte frame and saved RSP/SS tail, and finishes with
`iretq`. Snapshot reads reject interrupt context, mask IF only around the fixed
copy, and restore the prior IF state; phase tokens freeze the first matching
vector/task capture so unrelated interrupts cannot overwrite evidence. The
sequence counter detects an incomplete copy but is not, by itself, a
soundness mechanism for concurrent `UnsafeCell` access. The snapshot never
owns a raw interrupted-frame pointer. No interrupt path allocates, writes
CR3, or calls cooperative context switching.
