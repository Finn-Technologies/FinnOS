# Capabilities

> Status: Accepted architectural direction
> Implementation: Not implemented

Capabilities are intended to be unforgeable handles in per-process tables. They should express typed rights, object-specific operations, least privilege, explicit delegation, revocation, stale-handle prevention, and auditability. Transfer over IPC must be explicit and normally reduce rights unless the kernel authorizes otherwise.

User consent and kernel authority are related but distinct: a capability is not the same thing as a user-facing permission dialog. Exact handle representation and revocation mechanics remain unresolved.
