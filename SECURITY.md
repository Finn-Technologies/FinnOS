# FinnOS Security

FinnOS is an experimental kernel prototype with no supported release. Do not use it for sensitive data or untrusted workloads. All current tasks execute in ring 0 in one address space; capability security, userspace isolation, authentication, secure boot, package signing, and updates are not implemented.

## Reporting a vulnerability

Use GitHub private vulnerability reporting for `Finn-Technologies/FinnOS` when available. If that channel is unavailable, contact the maintainers through the private contact method in the organization profile rather than filing a public issue. Do not include exploit details, secrets, or personal data in public issues.

The project currently has no guaranteed response SLA or supported-version window. Maintainers should acknowledge a valid private report, establish scope and embargo expectations, prepare a fix and regression test, and publish an advisory when users could have consumed affected artifacts. This process must be formalized before the first developer preview.

## Current security properties

- Rust is the primary implementation language; unsafe code is reviewed as a privileged boundary.
- x86 kernel mappings use supervisor-only permissions, NX, CR0.WP, W^X intent, an unmapped null page, and stack/heap guards.
- The local ARM64 R4.3 worktree uses EL1-only W^X mappings, SCTLR.WXN, PXN/UXN, an unmapped null page and stack guards, and validates four real translation/permission aborts. This is QEMU evidence, not a user-isolation or physical-hardware claim.
- The local ARM64 R4.4 path keeps IRQs masked until an explicit consumer, performs no allocation, locking, or serial output inside the GIC handler, and EOIs the exact acknowledged token once. Its fixed single-BSP QEMU GICv2 model is not a general device-discovery or interrupt-isolation claim.
- Boot structures are versioned and structurally validated.
- QEMU tests reject panic/fatal markers.

These are memory-safety and fault-containment foundations, not a complete security model. The loader is trusted, physical addresses are trusted within the handoff, kernel tasks are mutually untrusted only by convention, and serial output may expose addresses/state.

## Threat model baseline

Before Level 2, document assets, actors, trust anchors, physical-access assumptions, malicious applications, compromised drivers, malformed devices, hostile network input, malicious packages/updates, and denial-of-service policy. Required boundaries are firmware/loader, kernel/user, process/process, driver/device, service/client, package/repository, updater/boot state, and lock-screen/session.

## Hardening plan

| Gate | Required controls |
|---|---|
| Core OS | User/kernel page isolation, validated syscall copies, W^X user mappings, rights-bearing generation-safe handles, IPC quotas, fatal user-fault containment, entropy source policy |
| Devices/storage | DMA bounds/IOMMU strategy, resource revocation, parser fuzzing, filesystem permissions/integrity, secret-storage design |
| Networking/UI | Process-isolated network service, packet fuzzing, TLS/trust store, firewall policy, consent indicators, lock-screen data suppression |
| Preview | Signed packages/images, dependency/license audit, SBOM/provenance, coordinated disclosure, crash redaction, key management |
| Beta | Threat-model review, unsafe-code and syscall/IPC audit, sandbox tests, update rollback/tamper tests, no open critical findings |
| Stable | Secure-boot position, supported-version policy, security update process, key rotation/recovery, reproducible signed release evidence |

Security claims must cite executable adversarial tests. Capability terminology must not be used as evidence until rights are enforced by kernel objects and transfer rules.
