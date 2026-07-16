# Application and API Development

> Status: no application runtime or SDK exists.

FinnOS cannot execute userspace applications. There is no syscall ABI, process loader, libc/native runtime, IPC implementation, service discovery, package format, application manifest, toolkit, Peony runtime, or stable API. Kernel cooperative tasks are not applications.

Application development documentation becomes actionable after roadmap R7-R9 and R15. The first SDK must include versioned ABI/API references, capability declarations, lifecycle/crash semantics, CLI and GUI examples, package signing/install steps, compatibility policy, and clean-environment tests. Until then, application/API proposals belong in RFCs and must not promise stability.
