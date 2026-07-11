# ADR 0006: UEFI-first boot

## Status

Accepted as initial direction

## Context

The first virtual targets need a common firmware interface.

## Decision

Use UEFI initially, target x86-64 QEMU first, add ARM64 QEMU afterward, and do not initially support legacy BIOS.

## Rationale

UEFI is a practical common starting point for the planned virtual targets.

## Alternatives considered

Legacy BIOS-first boot was not selected.

## Consequences

Boot protocol and firmware-entry work remain ahead.

## Security impact

Verification requirements are planned, not implemented.

## Compatibility impact

Legacy BIOS is outside the initial scope.

## Follow-up work

Define the versioned boot contract.
