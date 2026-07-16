# Releasing FinnOS

No repository revision is currently an OS release. The `first-boot-x86_64-v0.1` Git tag records a development milestone and has no corresponding verified GitHub Release.

## Release readiness checklist

- [ ] Scope, supported architectures, machine/firmware versions, and known limitations are frozen.
- [ ] Every maturity-level acceptance criterion is linked to passing evidence.
- [ ] No open P0/P1 blocker; security review and risk acceptance are recorded.
- [ ] Clean pinned environment passes formatting, lint, unit, integration, architecture, fault, recovery, and soak suites.
- [ ] Debug symbols and bounded crash-diagnostic procedure are preserved.
- [ ] Version, source revision, lockfile digest, toolchain, build inputs, and artifact hashes are embedded in provenance.
- [ ] Independent rebuild comparison is documented.
- [ ] Image, packages, SBOM, checksums, provenance, and release notes are signed.
- [ ] Signing-key ownership, rotation, revocation, and incident procedure are tested.
- [ ] Install, upgrade, interrupted-upgrade rollback, recovery, and user-data migration pass.
- [ ] `CHANGELOG.md`, supported versions, hardware matrix, security policy, API/format compatibility, and EOL are current.
- [ ] License and third-party notices are complete.
- [ ] Release candidate is tested by someone other than its primary implementer where feasible.
- [ ] Published artifacts are downloaded and signature/boot/install paths are reverified.

## Version policy

Until compatibility policy exists, versions are development milestones and may change every ABI/data format. Do not imply semantic compatibility. Before preview, define version sources for kernel, boot protocol, userspace ABI, IPC protocols, packages, and on-disk formats. Stable 1.0 requires migration or explicit rejection behavior for every persisted format.

## Non-destructive procedure

1. Open a release tracking issue and freeze acceptance evidence.
2. Build candidate artifacts in the pinned release environment without modifying source.
3. Run qualification and independent rebuild checks.
4. Produce notes listing verified capabilities, unsupported features, fixed security issues, and exact upgrade/recovery instructions.
5. Create a signed annotated tag and draft GitHub Release only after approval.
6. Verify downloaded artifacts, then publish. Never rewrite or replace a published tag/artifact silently.
7. Monitor regressions and use a new version for corrections; revoke compromised artifacts explicitly.
