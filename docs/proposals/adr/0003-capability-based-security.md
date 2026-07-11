# ADR 0003: Capability-based security

## Status

Accepted as initial direction

## Context

Ambient authority makes isolation and auditing difficult.

## Decision

Represent authority through explicit typed capabilities, minimize ambient authority, and distinguish user-facing permissions from kernel capabilities.

## Rationale

Explicit authority supports least privilege and delegation.

## Alternatives considered

An ambient, identity-only model was not selected as the native direction.

## Consequences

Handle, revocation, and user-consent designs are required.

## Security impact

The model is intended to reduce authority confusion but is not implemented or proven.

## Compatibility impact

Native APIs will not assume Unix permission primitives.

## Follow-up work

Specify object rights and transfer protocols.
