# ADR 0015: Hardened boot protocol version 3

## Status

Accepted

## Context

R4 ARM64 handoff consumption requires copying `BootInfo` by value before any
nested address is consumed. The version-two layout carried the storage range,
but allowed the loader to report only `size_of::<BootInfo>()` and did not define
exact absent-resource encodings. Tightening those rules while retaining version
two would make an older accepted producer fail under a newer consumer without
an explicit compatibility signal.

## Decision

Increment the protocol version to 3. The loader reports an exact aligned 4 KiB
`BootInfo` storage page. Known flags, required range arithmetic, memory-map
descriptor metadata, absent-resource zeroing, and RSDP consistency are exact.
`PixelFormat` is an all-bit-pattern-valid transparent integer newtype so the
kernel can safely copy malformed wire bytes before validation. Consumers reject
versions 1 and 2.

## Consequences

- Boot manager and kernel must be upgraded together.
- Version mismatches fail explicitly instead of masquerading as compatible v2.
- The top-level handoff can be copied before nested physical resources are read.
- LoaderData reclamation remains forbidden because the protocol records used
  memory-map bytes rather than the helper pool's complete allocation.

## Security impact

Exact page ownership and all-bit-pattern-valid copying remove undefined enum
construction and ambiguous storage/absence states at the privileged handoff
boundary. Firmware mapping and pointer readability remain trusted premises.

## Compatibility impact

This deliberately supersedes the accepted version-two wire contract in ADR
0009. No compatibility mode is provided; older boot managers and kernels are
rejected with `UnsupportedVersion`.
