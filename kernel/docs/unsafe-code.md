# Unsafe code policy

> Status: Initial policy
> Implementation: The initial crate forbids unsafe code

The protocol and pure helper modules forbid unsafe code. The kernel hardware boundary contains narrowly scoped assembly and raw-pointer operations for descriptor tables, segment reload, task-register load, and exception entry. Every unsafe block has a nearby `SAFETY:` explanation; unsafe modules must document invariants; unsafe code should be minimized and reviewed; and architecture assembly boundaries require documentation and tests. Descriptor encoders and exception-frame layout helpers are kept safe and host-testable.
