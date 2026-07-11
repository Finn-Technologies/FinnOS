# ADR 0004: Typed IPC

## Status

Accepted as initial direction

## Context

System services require explicit, evolvable communication.

## Decision

Use versioned typed protocols, shared buffers for large data where suitable, and explicit capability transfer. Leave the exact wire format unresolved.

## Rationale

Typed contracts make validation and compatibility review possible.

## Alternatives considered

Unstructured byte streams and implicit global service state were not selected.

## Consequences

Protocol tooling and versioning rules must be designed.

## Security impact

Validation and bounds checking are required; no security guarantee exists yet.

## Compatibility impact

Protocol evolution must be explicit.

## Follow-up work

Create an RFC for the first stable protocol family.
