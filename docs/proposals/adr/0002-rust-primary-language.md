# ADR 0002: Rust as primary language

## Status

Accepted as initial direction

## Context

The project needs a systems language with explicit control and strong tooling.

## Decision

Use Rust primarily, allow limited assembly for architecture entry and context switching, and carefully defined C ABIs where interoperability requires them. Do not create a new language during foundation.

## Rationale

Rust provides a mature toolchain without removing the need for unsafe-code review.

## Alternatives considered

Using C as the primary language or creating a new language were not selected.

## Consequences

Toolchain policy and unsafe boundaries need continued documentation.

## Security impact

Rust does not eliminate design or unsafe-code risks.

## Compatibility impact

ABIs remain explicitly designed rather than inherited.

## Follow-up work

Document assembly and FFI rules when implementation begins.
