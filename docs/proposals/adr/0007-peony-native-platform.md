# ADR 0007: Peony native platform

## Status

Accepted as initial direction

## Context

FinnOS needs a coherent native graphical and application model across form factors.

## Decision

Peony will be FinnOS’s native compositor, shell, UI framework, design system, and accessibility platform. It will not be a skin over an imported Linux display server; desktop and mobile shells may differ while sharing the platform.

## Rationale

A native platform can align lifecycle, security, accessibility, and adaptive scenes.

## Alternatives considered

Building on Wayland, X11, Flutter, Android Views, UIKit, or another imported application framework was not selected as the native direction.

## Consequences

Peony protocols and APIs require substantial future design work.

## Security impact

Input, consent, and isolation must be designed together; no guarantees exist yet.

## Compatibility impact

No external UI ABI is promised.

## Follow-up work

Define Peony Display and application scene RFCs.
