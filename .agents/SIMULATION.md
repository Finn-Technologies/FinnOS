# Clean-Agent Simulation Report

Date: 2026-07-16. Base commit: `3539a35` plus the dirty audit/loader/agent-system worktree described in `STATE.md`.

Read-only clean-agent simulations used only `.agents/` entry points. They did not execute builds or modify files, so they validate discoverability and procedure, not FinnOS runtime behavior.

## Workflows

| Scenario | Entry and selected workflow | Result |
|---|---|---|
| Build FinnOS | Entry protocol -> build environment -> test strategy -> build orchestration -> x86 debug runbook | Exact preflight/build/image commands, host-mutating setup, environment overrides, output paths, and debug-only scope are discoverable |
| Boot x86-64 | QEMU boot testing -> `runbooks/x86_64-debug-boot.md` -> debugging/handoff | Eight bounded modes, per-mode directories, ordered-marker source, exit semantics, overwrite behavior, and evidence retention are explicit |
| Select ARM64 task | Roadmap execution -> build orchestration -> cross-architecture/UEFI/ARM64/QEMU/CI | R1 clearing evidence, R3 serial-entry boundary, R4 parity boundary, absent commands/overrides, non-goals, and blocked status are explicit |
| Investigate kernel fault | Debugging -> fixing boot failure -> conditional loader/VM/interrupt skill | Decision path covers image, firmware, loader, protocol, entry, paging, exception, and harness; exact ELF/log preservation is required |
| Implement roadmap issue | Roadmap execution -> implementing-roadmap-issue -> test/docs/Git/PR/handoff | Task-state transitions require baseline, acceptance, narrow/aggregate tests, docs/status evidence, scoped diff, and protected-branch verification |
| Update documentation | Evidence/status -> documentation maintenance -> status-roadmap updates | Canonical authority and implemented/planned language are explicit; historical audit evidence is preserved |
| Prepare handoff | Agent handoff -> template/script | Script captures base commit and dirty state, writes only a new `.agents/handoffs/*.md`, and refuses traversal/overwrite |

## Ambiguities Found and Resolved

- Manual dependency bundles were not topological. Bundles are now generated from the prerequisite graph.
- Skill prerequisites conflated documents to load with product implementation gates. Metadata now separates `prerequisites`, `conditional_skills`, and `implementation_gates`.
- Positional category slices misclassified boundary skills. Category is now explicit registry metadata.
- All skills attributed dirty-worktree behavior to the base commit. Provenance now records base commit, dirty state, and context.
- x86 build/boot artifacts, mode directories, status 33, marker ownership, side effects, and log retention were unclear. The x86 debug runbook now defines them.
- Documented QEMU/OVMF override names did not match source. `BUILDING.md` and the runbook now use `FINNOS_QEMU_X86_64`, `FINNOS_QEMU_IMG`, and `FINNOS_OVMF_CODE`.
- ARM serial-first boot flowed directly into MMU/GIC/timer/context parity. The ARM skill now ends R3 at bounded serial kernel entry and assigns those mechanisms to R4.
- R4 depended on M2 item R5 while assigned to M1. The roadmap now makes R4 depend on R3; device discovery/routing remains M2.
- Generator pruning could remove unregistered directories. Rendering now refuses all unregistered skill directories and symlink writes.
- Handoff output could overwrite arbitrary paths. It is now confined to new `.agents/handoffs/*.md` files.
- The authoring template did not contain the required sections. It now provides the exact 22-section skeleton.
- State capture hid nonzero statuses and failed on missing tools. It now reports status/stdout/stderr and unavailable executables.

## Remaining Intentional Gaps

- No ARM command, AAVMF resolver, marker set, or termination device exists. The ARM skill requires R1/R3 to define them and forbids fabricated commands.
- No maintained GDB/LLDB kernel workflow or persistent crash dump exists. Fault investigation must use exact serial output and matching ELF until implemented documentation exists.
- Minimum Python/QEMU versions are unverified. Clean agents capture exact versions and use `doctor`; the runbook provides macOS and Ubuntu/Debian setup examples without claiming wider compatibility.
- `validate_yaml.py` validates FinnOS planning/template structure and cross-references without a third-party YAML dependency; GitHub remains the final parser for issue-form semantics.
- Later product skills are planning-gated. They can guide investigation/design but cannot produce completion evidence until their roadmap dependencies exist.

No remaining ambiguity prevents a clean agent from selecting the correct current task, building/booting the supported x86 debug target, recognizing ARM/future subsystem blockers, investigating a boot failure, updating documentation, or leaving a durable handoff.
