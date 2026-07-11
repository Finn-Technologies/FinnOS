# ADR 0005: Monorepo

## Status

Accepted

## Context

The initial system spans tightly related components and documentation.

## Decision

Begin with one repository for the kernel, services, Peony, SDK, tools, documentation, and tests. Splits may happen later if technical or organizational scale justifies them.

## Rationale

A single history simplifies cross-component architectural review during foundation.

## Alternatives considered

Multiple repositories from the start were not selected.

## Consequences

Repository boundaries must remain understandable as components grow.

## Security impact

Central review is useful but is not a security guarantee.

## Compatibility impact

No package or repository split policy exists yet.

## Follow-up work

Revisit repository scale after meaningful implementations exist.
