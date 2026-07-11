# Inter-process communication

> Status: Initial architectural direction
> Implementation: Not implemented; wire format unresolved

Native IPC is planned as typed, versioned messages over channels or endpoints. Capability transfer must be explicit. Messages should be bounded; large payloads should use shared buffers where suitable. Request/response, cancellation, timeouts, service discovery, validation, decoding, and denial-of-service protections require defined contracts.

The wire format, scheduling interaction, and compatibility policy are unresolved. No protocol is stable yet.
