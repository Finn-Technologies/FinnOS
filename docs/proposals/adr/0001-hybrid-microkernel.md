# ADR 0001: Hybrid microkernel

## Status

Accepted as initial direction

## Context

FinnOS needs strong isolation while retaining practical privileged primitives.

## Decision

Use a hybrid microkernel direction. Keep scheduling, virtual memory, interrupts, timers, IPC primitives, capability enforcement, and kernel-object management privileged; prefer user-space drivers and services where practical.

## Rationale

This balances isolation and implementation practicality.

## Alternatives considered

Monolithic kernel, strict minimal microkernel, and adopting an existing kernel were considered and not selected for the initial direction.

## Consequences

Service boundaries and privileged interfaces require careful design; no implementation exists yet.

## Security impact

User-space isolation is a primary goal, not a demonstrated property.

## Compatibility impact

No existing-kernel ABI is adopted.

## Follow-up work

Define objects, IPC, and failure behavior through later RFCs.
