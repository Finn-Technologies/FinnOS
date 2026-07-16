# Changelog

FinnOS has no OS releases. This file records repository milestones; Git history remains authoritative for individual changes.

## Unreleased

- Added validated target/profile build orchestration, x86-64 release-image boot testing, preserved QEMU evidence, and hardened CI workflow permissions/action pinning.
- Added the repository-local `.agents/` operating system with 87 FinnOS-specific skills, dependency validation, entry/checklist/templates, state capture, and handoff tooling.
- Added an evidence-backed status, architecture, build/test, platform, porting, hardware, UI, security, release, audit, roadmap, and GitHub-planning documentation set.
- Fixed ELF validation so an entry in a gap between executable segments is rejected.

## first-boot-x86_64-v0.1

- Added the initial x86-64 QEMU UEFI loader/kernel first-boot path.

Subsequent untagged `main` milestones added x86-64 exception handling, UEFI memory-map classification, physical allocation, private page tables, a bounded heap, local APIC timer interrupts, and cooperative kernel tasks. They are development milestones, not a supported OS release.
