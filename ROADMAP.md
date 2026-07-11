# FinnOS Roadmap

> Status: Planning document
> Implementation: Foundation and x86-64 QEMU First Boot are complete

## 1. Foundation — Complete

Objective: establish architecture, governance, build metadata, and a minimal kernel crate. Completion criteria are met.

## 2. First Boot — Complete for x86-64 QEMU

Objective: load a minimal kernel through UEFI in x86-64 QEMU. The boot contract, diagnostics, real FAT image, separate kernel ELF, framebuffer handoff, and automated smoke test are complete. Non-goal: user space.

## 3. Kernel Core — Planned

Objective: implement initial privileged primitives. Deliverables include objects, memory, scheduling, interrupts, and IPC foundations. Completion requires reviewed invariants and tests. Non-goal: broad hardware.

## 4. First User Space — Planned

Objective: start isolated services and a minimal runtime. Completion requires service startup and failure diagnostics. Non-goal: desktop applications.

## 5. First Devices — Planned

Objective: support selected virtual devices through isolated drivers. Completion requires restart and resource tests. Non-goal: general hardware coverage.

## 6. Peony First Light — Planned

Objective: display a minimal Peony session. Completion requires compositor, shell, input, and accessibility foundations. Non-goal: a finished desktop.

## 7. ARM64 — Planned

Objective: bring the architecture-neutral design to ARM64 QEMU. Completion requires boot and core test parity. Non-goal: phones.

## 8. Native Application Platform — Planned

Objective: define stable-enough application lifecycle, packaging, and Peony APIs. Completion requires migration and security documentation.

## 9. Reference Computer — Planned

Objective: select and support one reference computer. Completion requires published hardware constraints.

## 10. Tablet Platform — Planned

Objective: adapt Peony and power/input services to a reference tablet. Completion requires device and usability evidence.

## 11. Phone Platform — Planned

Objective: evaluate a phone reference platform. Completion requires a deliberate hardware and radio strategy; broad support is not promised.
