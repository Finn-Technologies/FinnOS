"""Canonical FinnOS agent skill registry.

Edit this file, run render_skills.py, review every generated diff, then run validate.py.
The compact registry avoids metadata and prerequisite drift while generated SKILL.md files
remain standalone operational documents.
"""

VERIFIED_COMMIT = "d21a477"
VERIFIED_DATE = "2026-07-16"
VERIFIED_CONTEXT = "base commit plus uncommitted audit and agent-system worktree"

SKILLS = []
CURRENT_CATEGORY = None


def category(name):
    global CURRENT_CATEGORY
    CURRENT_CATEGORY = name


def skill(name, title, purpose, current, actions, docs, milestones, prerequisites=None,
          triggers=None, commands=None, architecture=None, tests=None, safety=None,
          outputs=None, blocked=None, maturity="operational", conditional_skills=None,
          implementation_gates=None, version=1, status="active", owners=None,
          verified_commit=VERIFIED_COMMIT, verified_date=VERIFIED_DATE,
          verified_context=VERIFIED_CONTEXT, verified_dirty=True):
    if CURRENT_CATEGORY is None:
        raise ValueError(f"skill {name} has no category")
    if blocked and maturity == "operational":
        maturity = "planning-gated"
    SKILLS.append({
        "name": name, "title": title, "purpose": purpose, "current": current,
        "actions": actions, "docs": docs, "milestones": milestones,
        "prerequisites": ["finnos-operating-rules"] if prerequisites is None else prerequisites,
        "triggers": triggers or [name.replace("-", " ")],
        "commands": commands or ["./tools/finn check"],
        "architecture": architecture or "State shared semantics explicitly; isolate x86-64 and ARM64 mechanisms.",
        "tests": tests or f"Make `{actions[-1]}` observable with a negative case, then run the narrow and aggregate gates.",
        "safety": safety or f"Preserve the boundary described by the current state: {current}",
        "outputs": outputs or f"An evidence-backed {title.lower()} result with scoped artifacts, tests, documentation, and handoff.",
        "blocked": blocked, "maturity": maturity, "category": CURRENT_CATEGORY,
        "conditional_notes": conditional_skills or [],
        "conditional_skills": [],
        "implementation_gates": implementation_gates or ([blocked] if blocked else []),
        "version": version, "status": status, "owners": owners or [],
        "verified_commit": verified_commit, "verified_date": verified_date,
        "verified_context": verified_context, "verified_dirty": verified_dirty,
    })


category("Foundations")
skill("finnos-operating-rules", "FinnOS Operating Rules",
      "Apply non-negotiable evidence, scope, architecture, safety, and handoff policy to every task.",
      "FinnOS is an x86-64 QEMU kernel prototype; all later product layers remain absent or planned.",
      ["Read .agents/STATE.md and authority order", "Classify every claim by executed evidence", "Stop at named roadmap dependencies", "Preserve commands/logs and leave a handoff"],
      ["STATUS.md", "ROADMAP.md", "ARCHITECTURE.md"], ["M0 Reproducible Build", "M8 Stable 1.0"], [],
      ["any FinnOS task", "universal rules"], ["python3 .agents/scripts/capture_state.py"],
      "Never infer ARM64 from x86-64 or hardware from QEMU.")
skill("repository-orientation", "Repository Orientation", "Establish a fresh, evidence-backed task context before edits.",
      "The worktree may contain the audit and loader regression changes; issue #16/PR #17 are active.",
      ["Read agent entry and state", "Inspect status, log, tree, source, tests, docs, issues, and PRs", "Locate supported targets and last passing evidence", "Write an initial context report"],
      ["README.md", "STATUS.md", "ROADMAP.md", "docs/README.md"], ["M0 Reproducible Build"],
      ["finnos-operating-rules"], ["enter repository", "new agent", "orientation"],
      ["git status --short --branch", "git log --oneline -10", "./tools/finn doctor"])
skill("task-planning", "Task Planning", "Convert a request into bounded work with dependencies, non-goals, acceptance, and verification.",
      "Roadmap work is dependency-ordered; M0 precedes ARM64, userspace, devices, and Peony.",
      ["Restate observed problem", "Select minimum skills", "Trace dependencies and active overlap", "Define measurable acceptance and commands", "Name non-goals, rollback, docs, and blockers"],
      ["ROADMAP.md", ".agents/templates/task-plan-template.md"], ["M0 Reproducible Build"], ["repository-orientation"],
      ["plan task", "acceptance criteria", "scope"])
skill("roadmap-execution", "Roadmap Execution", "Select and advance critical-path work without premature later-phase implementation.",
      "M0 is current; R1 is locally verified pending CI/integration, while R2 and active preemption-context review are immediate priorities.",
      ["Identify current maturity gate", "Confirm predecessor evidence", "Split one reviewable outcome", "Map acceptance to issue and tests", "Update state only after evidence"],
      ["ROADMAP.md", "STATUS.md", "docs/github-planning/issues.yml"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["task-planning", "evidence-status-reporting"], ["roadmap", "milestone", "critical path"],
      verified_commit="cc828ec", verified_context="R1 publication branch locally verified; integration CI pending", verified_dirty=False)
skill("evidence-status-reporting", "Evidence and Status Reporting", "Classify implementation state with reproducible evidence and calibrated confidence.",
      "The audit distinguishes verified x86 paths from absent ARM64/product subsystems.",
      ["Name the claim and scope", "Collect source, build, runtime, hardware, and CI evidence", "Assign one classification", "Record confidence/unknowns", "Update canonical status only when justified"],
      ["STATUS.md", "docs/audit/2026-07-16.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["repository-orientation"], ["status", "completion percentage", "verified"],
      outputs="A claim table using verified complete, implemented-unverified, partial, prototype, stub, planned, broken, blocked, or unknown.")
skill("build-environment-management", "Build Environment Management", "Provision and capture a reproducible FinnOS host toolchain without hiding host differences.",
      "macOS ARM64 and Linux CI build x86; Rust x86 bare-metal/UEFI targets, QEMU, qemu-img, and OVMF are required.",
      ["Run doctor before installation", "Capture host/Rust/Python/QEMU/firmware versions", "Install only documented targets/tools", "Distinguish macOS hdiutil from Linux mtools/dosfstools", "Re-run doctor and record unresolved dependencies"],
      ["BUILDING.md", "rust-toolchain.toml", "tools/finnlib/toolchain.py"], ["M0 Reproducible Build"],
      ["task-planning"], ["toolchain", "OVMF", "AAVMF", "cross compiler"],
      ["./tools/finn doctor", "rustup target add x86_64-unknown-none x86_64-unknown-uefi", "rustc --version --verbose", "qemu-system-x86_64 --version"],
      "ARM64 host does not imply ARM64 guest support; future AAVMF setup requires an implemented target.")
skill("build-orchestration", "Build Orchestration", "Change workspace, target/profile, image, and CI build flow coherently.",
      "Tooling validates Finnfile target/profile data; development and release x86 images boot locally while ARM64 remains explicitly non-bootable.",
      ["Trace CLI to build/image/QEMU functions", "Define target-profile artifact contract", "Reject unsupported combinations", "Keep feature-specific target isolation", "Compare clean local commands with CI"],
      ["BUILDING.md", "Finnfile.toml", "tools/finnlib/build.py", "tools/finnlib/image.py", ".github/workflows/ci.yml"], ["M0 Reproducible Build"],
      ["build-environment-management", "test-strategy"], ["build script", "target profile", "image"],
      ["./tools/finn build", "./tools/finn image --profile release", "./tools/finn test-boot --profile release"],
      version=2, verified_commit="cc828ec", verified_context="R1 publication branch locally verified; integration CI pending", verified_dirty=False)
skill("qemu-boot-testing", "QEMU Boot Testing", "Build, boot, capture, and classify bounded emulator evidence.",
      "x86 QEMU q35/OVMF has eight development modes plus release first boot; bounded runs preserve serial logs. ARM QEMU is absent.",
      ["Build exact feature/profile image", "Record firmware and full QEMU command", "Capture ordered serial markers and return status", "Locate first missing/forbidden marker", "Preserve image, manifest, ELF, and log on failure"],
      ["TESTING.md", "docs/development/qemu.md", "tools/finnlib/qemu.py", ".agents/runbooks/x86_64-debug-boot.md"], ["M0 Reproducible Build", "M1 Dual-Architecture Boot"],
      ["build-orchestration", "debugging-investigation"], ["QEMU", "boot smoke", "serial marker"],
      ["./tools/finn test-boot", "FINNOS_BOOT_TIMEOUT_SECONDS=90 ./tools/finn test-boot"],
      "Use q35/OVMF only for current x86 claims; future ARM uses virt/AAVMF and a distinct marker contract.",
      version=2, verified_commit="cc828ec", verified_context="R1 publication branch locally verified; integration CI pending", verified_dirty=False)
skill("test-strategy", "Test Strategy", "Choose layered regression evidence and protect CI gates.",
      "63 Rust and 33 Python tests plus eight x86 QEMU modes passed at the audit snapshot; counts are historical, not permanent assertions.",
      ["Make the defect observable", "Put pure policy in host tests and hardware behavior in QEMU", "Isolate feature artifacts", "Add malformed/exhaustion/teardown cases", "Run narrow then aggregate gates and document gaps"],
      ["TESTING.md", "tests/README.md", ".github/workflows/boot-smoke.yml"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["task-planning"], ["tests", "fuzz", "Clippy", "regression"],
      ["cargo fmt --all -- --check", "cargo clippy --workspace --all-targets -- -D warnings", "cargo test --workspace", "python3 -m unittest discover -s tools/tests -p 'test_*.py'"])
skill("debugging-investigation", "Debugging and Investigation", "Localize failures across host, image, firmware, loader, protocol, kernel, and harness layers.",
      "COM1 markers are primary runtime evidence; no maintained GDB workflow or persistent crash dump exists.",
      ["Reproduce unchanged", "Capture versions, command, status, and full serial", "Identify first divergent marker", "Symbolize exact ELF if possible", "Minimize and bisect without discarding evidence"],
      ["docs/development/debugging.md", "kernel/docs/panic-policy.md", "tools/finnlib/qemu.py"], ["M0 Reproducible Build"],
      ["repository-orientation", "test-strategy"], ["panic", "exception", "hang", "investigate"],
      ["./tools/finn test-boot", "git log --oneline -20"],
      safety="Never infer symbols from a different feature/profile ELF; do not use destructive Git operations.")
skill("code-review", "Code Review", "Review correctness, invariants, architecture coupling, unsafe boundaries, evidence, and scope.",
      "Kernel code contains deliberate fixed capacities and assembly/unsafe boundaries; product architecture remains mostly planned.",
      ["Verify claimed baseline and issue scope", "Trace inputs, ownership, errors, rollback, and concurrency", "Audit integer/pointer/unsafe and architecture assumptions", "Evaluate tests against failure modes", "Report findings by severity with paths/lines"],
      ["CONTRIBUTING.md", "kernel/docs/invariants.md", "kernel/docs/unsafe-code.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["repository-orientation", "test-strategy", "unsafe-rust-low-level-safety"], ["review", "pull request", "diff"])
skill("git-commit-hygiene", "Git and Commit Hygiene", "Prepare scoped, secret-free, reviewable changes without rewriting shared history.",
      "The worktree may be dirty with audit changes; generated build outputs belong under ignored target/build paths.",
      ["Inspect status/diff/log", "Separate unrelated existing changes", "Detect generated files, secrets, local paths, and debug residue", "Run required gates", "Propose logical commits using repository style"],
      ["CONTRIBUTING.md", ".gitignore", ".agents/checklists/pre-commit.md"], ["M0 Reproducible Build"],
      ["task-planning", "test-strategy"], ["commit", "git diff", "stage"],
      ["git status --short --branch", "git diff --check", "git log --oneline -10"])
skill("github-project-management", "GitHub Project Management", "Create evidence-based issues, milestones, labels, PR metadata, and fallback proposals.",
      "Issue #16/PR #17 are active; proposed normalized metadata lives under docs/github-planning and Projects permission may be unavailable.",
      ["Search existing issues/PRs first", "Use roadmap acceptance and dependencies", "Prefer epics plus independently reviewable children", "Update actual state from evidence", "Write machine-readable proposals if write access is absent"],
      ["docs/github-planning/README.md", "docs/github-planning/issues.yml", ".github/ISSUE_TEMPLATE/"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["roadmap-execution", "evidence-status-reporting"], ["GitHub issue", "milestone", "labels", "project"],
      ["gh issue list --state all", "gh pr list --state all", "gh label list"])

# Boot and kernel skills.
category("Boot and kernel")
skill("uefi-bootloader-development", "UEFI Bootloader Development", "Safely evolve firmware entry, ELF loading, GOP/memory handoff, and architecture-specific UEFI binaries.",
      "Only x86 BOOTX64.EFI exists; entry membership is checked per executable segment; loader trusts firmware mappings and handoff storage.",
      ["Model malformed ELF/range cases first", "Validate class/machine/segments/alignment/overflow/entry", "Preserve allocations through ExitBootServices", "Construct versioned BootInfo with explicit ownership", "Boot-test exact loader/kernel pair"],
      ["boot/uefi/", "boot/README.md", "docs/architecture/boot.md", "docs/reference/boot-protocol.md"], ["M0 Reproducible Build", "M1 Dual-Architecture Boot"],
      ["build-orchestration", "boot-protocol-evolution", "unsafe-rust-low-level-safety", "qemu-boot-testing"], ["UEFI", "ELF loader", "ExitBootServices", "GOP"],
      ["cargo test -p finn-boot-uefi --lib", "./tools/finn test-boot"],
      "Keep ELF machine/ABI and BOOTX64 versus future BOOTAA64 explicit.", tests="Add table-driven malformed ELF tests and fuzz/property targets before expanding accepted inputs.")
skill("boot-protocol-evolution", "Boot Protocol Evolution", "Version and migrate the loader-kernel ABI without layout, ownership, or cross-architecture ambiguity.",
      "BootInfo v2 is repr(C), used by x86 loader/kernel, and carries map, framebuffer, ranges, and optional RSDP; some comments still require version-language review.",
      ["Inventory every producer/consumer", "Specify field size/alignment/validity/ownership/lifetime", "Choose compatible version/size rules", "Update both sides atomically", "Add old/new and malformed fixtures plus reference docs"],
      ["boot/protocol/src/lib.rs", "docs/reference/boot-protocol.md", "docs/proposals/adr/0009-memory-map-protocol-v2.md"], ["M0 Reproducible Build", "M1 Dual-Architecture Boot"],
      ["cross-architecture-design", "unsafe-rust-low-level-safety", "test-strategy"], ["BootInfo", "protocol version", "handoff ABI"],
      ["cargo test -p finn-boot-protocol", "./tools/finn test-boot"],
      "Use fixed-width architecture-neutral representation; pointers are physical addresses with explicit validation, not Rust references.")
skill("x86-64-platform-development", "x86-64 Platform Development", "Modify x86 entry, descriptor tables, paging, APIC/PIC/PIT, context, ACPI, SMP, and future syscall mechanisms.",
      "x86 has verified BSP entry, GDT/TSS/IDT, private paging, xAPIC timer, and cooperative SysV64 contexts; no IOAPIC, SMP, user mode, or syscall entry.",
      ["Trace linker/entry/init order", "State required CPU features and register invariants", "Normalize traps before shared policy", "Preserve canonical addresses and interrupt ownership", "Test real QEMU behavior, not assembly compilation only"],
      ["kernel/src/arch/x86_64/", "kernel/arch/x86_64/linker.ld", "architecture/x86_64/README.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["cross-architecture-design", "cpu-early-initialization", "unsafe-rust-low-level-safety"], ["x86_64", "GDT", "TSS", "APIC", "syscall"],
      ["./tools/finn test-exceptions", "./tools/finn test-page-tables", "./tools/finn test-timer-interrupts"],
      "Do not leak x86 descriptor/vector/CR3 semantics into shared interfaces.",
      conditional_skills=["interrupt-exception-handling for trap/APIC/IRQ work", "virtual-memory for page-table work", "scheduler-thread-development for context work"])
skill("arm64-platform-development", "ARM64 Platform Development", "Bring up AArch64 UEFI/QEMU mechanisms while maintaining semantic parity with x86.",
      "ARM64 has no executable implementation: no BOOTAA64.EFI, kernel entry, MMU, vectors, GIC, timer, UART, context, image, QEMU command, or CI.",
      ["Clear R1: target/profile data must drive build, image, run/test, artifacts, invalid-combination errors, and local/CI parity", "For R3 add BOOTAA64, matching kernel/linker/entry/UART, QEMU virt plus AAVMF resolution, and bounded ordered loader/kernel-entry markers", "End R3 at deterministic serial kernel entry with retained manifest/artifacts/log and existing x86 gates green", "For R4 only, add exception vectors, MMU/MAIR/TCR/TTBR, GIC, generic timer, and AAPCS64 task context parity", "Define ARM-specific timeout/termination evidence and environment overrides rather than copying x86 isa-debug-exit or OVMF variables"],
      ["PORTING.md", "build/targets/arm64-qemu.toml", "Finnfile.toml"], ["M1 Dual-Architecture Boot"],
      ["build-orchestration", "uefi-bootloader-development", "cross-architecture-design", "qemu-boot-testing", "unsafe-rust-low-level-safety"], ["arm64", "aarch64", "AAVMF", "GIC", "exception vectors"],
      ["python3 .agents/scripts/validate.py --all", "./tools/finn check"],
      "Use AAPCS64, EL0/EL1, VBAR, translation-table, GIC, and barrier semantics; never transliterate x86 registers.",
      blocked="R3 serial first boot is blocked by roadmap R1 build target/profile orchestration; no ARM command, qemu-system-aarch64/AAVMF resolver, ARM override, or marker/termination contract exists. R4 parity starts only after R3 serial entry is verified.")
skill("cross-architecture-design", "Cross-Architecture Design", "Separate semantic policy from x86/ARM mechanisms and define parity contracts without premature abstraction.",
      "Shared code covers protocol, memory classification/allocation, heap policy, tasks, and interrupt depth; executable architecture dispatch is x86-only.",
      ["Extract existing semantic invariant from source", "Map independently to x86 and ARM mechanisms", "Choose the smallest shared contract", "Document intentional differences", "Add contract tests before migrating callers"],
      ["ARCHITECTURE.md", "PORTING.md", "kernel/src/arch/mod.rs"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["task-planning"], ["architecture abstraction", "platform interface", "parity"],
      architecture="Avoid both x86-shaped traits and lowest-common-denominator APIs; retain target-specific types behind explicit adapters.")
skill("cpu-early-initialization", "CPU and Early Initialization", "Maintain initialization ordering before heap, interrupts, and secondary CPUs are safe.",
      "Only the x86 BSP initializes; early serial, exception state, memory, paging, heap, timer, scheduler occur in a fixed sequence; no SMP.",
      ["Write an initialization dependency graph", "Mark heap/interrupt/CPU-local availability at each stage", "Fail through allocation-free serial/panic paths", "Initialize exception readiness before risky dereference", "Add secondary-CPU protocol only after BSP contracts stabilize"],
      ["kernel/src/bin/x86_64.rs", "docs/architecture/boot.md", "kernel/docs/invariants.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["cross-architecture-design", "logging-diagnostics", "unsafe-rust-low-level-safety"], ["early init", "BSP", "SMP", "CPU features"])
skill("interrupt-exception-handling", "Interrupt and Exception Handling", "Design trap frames, controllers, IRQ lifecycle, fault policy, and preemption interaction safely.",
      "x86 exception vectors and timer/spurious gates work; PIC is masked; no IOAPIC/device IRQs, user faults, nested scheduling, or ARM vectors.",
      ["Specify raw and normalized frame layouts", "Define mask-route-ack-EOI-teardown ownership", "Separate fatal kernel fault from containable user fault", "Audit nesting/preemption/logging constraints", "Test real faults, spurious IRQs, and forbidden contexts"],
      ["kernel/src/arch/x86_64/exceptions.rs", "kernel/src/arch/x86_64/interrupts.rs", "docs/architecture/x86_64-exceptions.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["cross-architecture-design", "synchronization-concurrency", "unsafe-rust-low-level-safety"], ["interrupt", "exception", "trap frame", "IRQ"],
      ["./tools/finn test-exceptions", "./tools/finn test-timer-interrupts"])
skill("timer-timekeeping", "Timer and Timekeeping", "Evolve hardware timers into monotonic clocks, deadlines, and scheduler time without conflating wall time.",
      "x86 calibrates a 100 Hz local APIC timer with PIT channel 2 and increments atomic ticks; no sleep queue, wall clock, tickless mode, or ARM timer.",
      ["Define counter frequency/width/wrap", "Separate clocksource, event timer, monotonic, and wall clock", "Validate calibration bounds", "Integrate deadlines through deferred scheduling", "Test drift, wrap, missed events, and architecture parity"],
      ["kernel/src/arch/x86_64/timer.rs", "docs/architecture/x86_64-interrupts-and-timer.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["interrupt-exception-handling", "cross-architecture-design"], ["timer", "timekeeping", "sleep", "deadline"],
      ["./tools/finn test-timer-interrupts"])
skill("physical-memory-management", "Physical Memory Management", "Classify, reserve, allocate, reclaim, and account physical pages with explicit ownership.",
      "UEFI descriptors become bounded normalized regions; only Conventional memory is allocated; boot-service memory is not reclaimed; contiguous first-fit is tested.",
      ["Validate descriptor stride/count/range/overlap", "Subtract kernel/handoff/framebuffer/platform reservations", "Preserve page alignment and ownership", "Model capacity/exhaustion/double-free", "Plan reclamation and DMA zones only from measured needs"],
      ["kernel/src/memory/", "docs/architecture/physical-memory-map.md", "docs/architecture/physical-page-allocation.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["boot-protocol-evolution", "cross-architecture-design", "reliability-fault-injection"], ["physical memory", "page allocator", "UEFI map", "DMA"],
      ["./tools/finn test-memory-map", "./tools/finn test-page-allocator"])
skill("virtual-memory", "Virtual Memory", "Manage kernel and future process mappings, faults, permissions, ownership, and TLB behavior.",
      "x86 private four-level 4 KiB mappings enforce supervisor W^X/NX/null/guards; user mappings, address-space objects, COW, shared memory, and ARM tables are absent.",
      ["Define mapping owner and lifetime", "Validate canonical/aligned ranges and overflow", "Enforce W^X, NX, user/supervisor, and guards", "Specify TLB invalidation and table reclamation", "Test expected faults and cross-address-space isolation"],
      ["kernel/src/arch/x86_64/paging.rs", "docs/architecture/x86_64-virtual-memory.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["physical-memory-management", "cross-architecture-design", "boot-kernel-hardening"], ["paging", "virtual memory", "page fault", "address space"],
      ["./tools/finn test-page-tables"])
skill("kernel-allocation", "Kernel Allocation", "Maintain early/fixed heap behavior and design a safe runtime replacement under interrupts, preemption, and SMP.",
      "A guarded fixed 1 MiB first-fit heap works; allocation in interrupt context is rejected; lock design assumes one non-preemptive CPU.",
      ["State allocator availability by init phase", "Validate layout/alignment/metadata", "Keep failure transactional", "Audit reentrancy, interrupt, preemption, and lock behavior", "Measure fragmentation before replacement"],
      ["kernel/src/memory/heap.rs", "kernel/src/arch/x86_64/heap.rs", "docs/architecture/kernel-heap.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["physical-memory-management", "synchronization-concurrency", "reliability-fault-injection"], ["kernel heap", "allocator", "fragmentation"],
      ["./tools/finn test-heap"])
skill("scheduler-thread-development", "Scheduler and Thread Development", "Evolve bounded cooperative ring-0 tasks into tested preemptible blocking threads.",
      "Eight generation-tagged task slots, guarded stacks, FIFO yield, exit/reap/reuse, context switching, and idle work on x86 BSP; timer does not preempt.",
      ["Write lifecycle/queue/ownership invariants", "Preserve stack mapping rollback and stale-ID rejection", "Normalize architecture context", "Add deferred preemption then wait queues/timeouts", "Prove forced preemption, blocking, cancellation, fairness, and idle"],
      ["kernel/src/task.rs", "kernel/src/arch/x86_64/scheduler.rs", "docs/architecture/cooperative-kernel-tasks.md"], ["M2 Core Kernel"],
      ["interrupt-exception-handling", "timer-timekeeping", "synchronization-concurrency", "unsafe-rust-low-level-safety"], ["scheduler", "task", "thread", "preemption", "wait queue"],
      ["./tools/finn test-cooperative-tasks", "./tools/finn test-timer-interrupts"],
      safety="Do not call scheduling from current interrupt context or claim preemption from preemption-ready frame support.",
      conditional_skills=["x86-64-platform-development or arm64-platform-development for the affected context switch"])
skill("synchronization-concurrency", "Synchronization and Concurrency", "Specify atomics, locks, interrupt/preemption rules, wait queues, and future SMP memory ordering.",
      "Current code is BSP-only with atomics, interrupt-depth guards, and a heap spinlock; no general lock-order or SMP model exists.",
      ["Identify execution contexts and ownership", "Choose interrupt masking, spin, or blocking primitive", "Document lock order and memory ordering", "Model cancellation/priority inversion", "Add contention, reentrancy, deadlock, and weak-memory tests"],
      ["kernel/docs/locking.md", "kernel/src/interrupt.rs", "kernel/src/memory/heap.rs"], ["M2 Core Kernel"],
      ["cross-architecture-design", "unsafe-rust-low-level-safety"], ["spinlock", "atomic", "deadlock", "memory ordering"],
      architecture="x86 TSO is not the portable memory model; ARM64 requires explicit acquire/release and barriers where device/CPU ordering demands them.")
skill("kernel-object-handle-model", "Kernel Object and Handle Model", "Design rights-bearing resource ownership, lifetime, revocation, transfer, and waitability.",
      "Capabilities and typed objects are ADR/design text only; no handle table or enforcement exists.",
      ["List objects and authority", "Define generation-safe handles and rights", "Specify reference/lifetime/revocation/duplication/transfer", "Integrate wait and peer-death semantics", "Test forged/stale handles, races, quotas, and teardown"],
      ["docs/architecture/capabilities.md", "docs/proposals/adr/0003-capability-based-security.md"], ["M2 Core Kernel"],
      ["task-planning", "synchronization-concurrency", "capability-privilege-review"], ["kernel object", "handle", "rights", "revocation"],
      blocked="Implementation depends on user address spaces/process lifecycle; design prototypes must remain explicitly unimplemented.",
      conditional_skills=["userspace-isolation when binding handles to process lifetime"])
skill("syscall-development", "Syscall Development", "Define and implement versioned, validated x86/ARM user-kernel entry semantics.",
      "No userspace or syscall ABI/entry exists.",
      ["Define semantic operation and rights", "Assign ABI only after review", "Map x86 entry/return and ARM SVC independently", "Validate scalar and user memory arguments", "Add wrappers, fault/interrupt/restart tests, and reference docs"],
      ["docs/architecture/processes.md", "ROADMAP.md"], ["M2 Core Kernel"],
      ["task-planning", "cross-architecture-design", "capability-privilege-review", "unsafe-rust-low-level-safety"], ["syscall", "copy_from_user", "SVC", "ring 3"],
      blocked="No user address space or ARM entry path exists; syscall numbering is premature.",
      conditional_skills=["userspace-isolation and the affected x86-64/ARM64 platform skill during implementation", "kernel-object-handle-model for rights-bearing operations"])
skill("ipc-capabilities", "IPC and Capabilities", "Implement bounded endpoints, queues, rights transfer, cancellation, timeouts, and shared-memory authority.",
      "Typed IPC and capability transfer are planned only; no endpoints or isolated processes exist.",
      ["Prototype smallest endpoint semantic", "Bound payload, queue, handles, and memory", "Define blocking/cancel/timeout/peer death", "Enforce rights transfer atomically", "Prove two isolated processes and adversarial exhaustion"],
      ["docs/architecture/ipc.md", "docs/architecture/capabilities.md", "docs/proposals/adr/0004-typed-ipc.md"], ["M2 Core Kernel"],
      ["task-planning", "synchronization-concurrency", "reliability-fault-injection", "capability-privilege-review"], ["IPC", "endpoint", "capability transfer", "message queue"],
      blocked="Requires userspace isolation, syscall entry, and object handles.",
      conditional_skills=["kernel-object-handle-model, syscall-development, and userspace-isolation during implementation"])
skill("userspace-isolation", "Userspace Isolation", "Create EL0/ring-3 processes, address spaces, stacks, executable state, fault containment, and resource limits.",
      "All current tasks are ring 0 in one x86 address space; ARM has no implementation.",
      ["Define process/thread ownership", "Build user page tables and stack", "Enter/return safely per architecture", "Terminate only faulting process", "Test kernel/peer isolation, invalid pointers, exhaustion, and cleanup"],
      ["docs/architecture/processes.md", "ROADMAP.md", "SECURITY.md"], ["M2 Core Kernel", "M3 Userspace Foundation"],
      ["virtual-memory", "cross-architecture-design", "boot-kernel-hardening", "unsafe-rust-low-level-safety"], ["userspace", "ring 3", "EL0", "process isolation"],
      blocked="Depends on M1 architecture parity and preemptible/blocking thread foundations.",
      conditional_skills=["scheduler-thread-development and the affected platform skill during implementation"])

# Driver and hardware skills.
category("Drivers and hardware")
skill("driver-architecture", "Driver Architecture", "Define discovery, resource, IRQ, MMIO/PIO, DMA, capability, lifecycle, and isolation contracts.",
      "No general driver model, process isolation, device manager, DMA broker, or device IRQ path exists.",
      ["Gate on discovery/IPC/userspace", "Define resource grant/revoke and matching", "Specify IRQ/DMA/MMIO ownership", "Choose kernel bootstrap versus userspace policy", "Test crash/reset/hot-remove/malformed device behavior"],
      ["docs/architecture/drivers.md", "HARDWARE_SUPPORT.md"], ["M4 Devices and Storage"],
      ["task-planning", "cross-architecture-design", "threat-modelling", "capability-privilege-review"], ["driver model", "DMA", "device manager"],
      blocked="General driver implementation waits for M2/M3 isolation and IPC; platform discovery may proceed earlier under R5.",
      conditional_skills=["interrupt-exception-handling when defining IRQ ownership", "userspace-isolation and ipc-capabilities when implementing isolated drivers"])
skill("acpi-device-discovery", "ACPI and Device Discovery", "Validate firmware tables and derive x86 topology/resources without trusting addresses or checksums.",
      "The loader passes an RSDP address; the kernel maps one page but parses no ACPI table.",
      ["Validate RSDP signature/checksum/revision", "Bound and map RSDT/XSDT/table headers", "Parse MADT first, then MCFG/FADT as needed", "Reject overlap/overflow/duplicate resources", "Use synthetic fixtures and QEMU table evidence"],
      ["ARCHITECTURE.md", "kernel/src/bin/x86_64.rs", "HARDWARE_SUPPORT.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["physical-memory-management", "unsafe-rust-low-level-safety", "reliability-fault-injection"], ["ACPI", "RSDP", "MADT", "MCFG", "FADT"],
      architecture="ACPI is primary for x86; ARM64 may use ACPI or device tree only after an explicit firmware policy.")
skill("device-tree", "Device Tree", "Decide and implement validated ARM firmware-description parsing when ARM bring-up requires it.",
      "No DT policy, parser, blob handoff, or ARM executable exists; QEMU virt requirements remain planned.",
      ["Decide DT versus ACPI scope in ADR", "Validate FDT header/offsets/strings/reservations", "Parse chosen, memory, CPUs, UART, GIC", "Translate address/interrupt cells with bounds", "Test malformed blobs and QEMU fixture"],
      ["PORTING.md", "build/targets/arm64-qemu.toml"], ["M1 Dual-Architecture Boot"],
      ["task-planning", "cross-architecture-design", "reliability-fault-injection"], ["device tree", "FDT", "chosen node", "reserved-memory"],
      blocked="Do not implement a generic DT framework before ARM64 boot firmware/handoff strategy is accepted.",
      conditional_skills=["arm64-platform-development and boot-protocol-evolution after the firmware policy is accepted"])
skill("pci-pcie", "PCI and PCIe", "Enumerate buses, validate configuration/BARs/capabilities, route MSI, and match drivers safely.",
      "No PCI enumeration, ECAM parser, BAR allocator, MSI/MSI-X, or driver matching exists.",
      ["Obtain validated MCFG or legacy access policy", "Enumerate bounded bus/device/function space", "Size/assign BARs without clobbering firmware resources", "Parse capability lists with loop bounds", "Test QEMU devices, hot/error paths, and MSI teardown"],
      ["HARDWARE_SUPPORT.md", "ROADMAP.md"], ["M4 Devices and Storage"],
      ["acpi-device-discovery", "unsafe-rust-low-level-safety", "reliability-fault-injection"], ["PCI", "PCIe", "BAR", "MSI", "ECAM"],
      blocked="Requires validated discovery and device IRQ/resource ownership.",
      conditional_skills=["interrupt-exception-handling for MSI/MSI-X", "driver-architecture for matching/resource grants"])
skill("virtio", "VirtIO", "Implement portable, bounded feature negotiation, virtqueues, barriers, DMA, interrupts, and reset.",
      "No VirtIO transport or device driver exists; it is the planned first virtual-device family.",
      ["Select PCI/MMIO transport from discovered resources", "Negotiate only implemented features", "Validate descriptor chains and ownership", "Apply device/CPU memory barriers", "Test reset, malformed chains, queue exhaustion, and interrupt races"],
      ["HARDWARE_SUPPORT.md", "ROADMAP.md"], ["M4 Devices and Storage"],
      ["synchronization-concurrency", "unsafe-rust-low-level-safety", "reliability-fault-injection"], ["VirtIO", "virtqueue", "descriptor", "feature negotiation"],
      blocked="Requires driver resource broker, IRQ routing, and DMA policy.",
      conditional_skills=["pci-pcie or the accepted MMIO transport", "driver-architecture and interrupt-exception-handling during integration"])
skill("storage-block-devices", "Storage and Block Devices", "Define request ownership, queues, alignment, cache/flush, errors, partitions, and first VirtIO block support.",
      "The kernel has no runtime block device; the FAT image is firmware-only boot media.",
      ["Define bounded asynchronous block requests", "Specify buffer/DMA ownership and alignment", "Implement flush/barrier/error semantics", "Add partition parsing with hostile-input tests", "Validate persistence/reboot and injected device failures"],
      ["HARDWARE_SUPPORT.md", "ROADMAP.md"], ["M4 Devices and Storage"],
      ["task-planning", "reliability-fault-injection", "threat-modelling"], ["block device", "VirtIO block", "partition", "flush"],
      blocked="Requires VirtIO/resource broker and userspace service model.",
      conditional_skills=["virtio and driver-architecture for the first implementation"])
skill("vfs-filesystems", "VFS and Filesystems", "Design namespace, mounts, file objects, permissions, caching, concurrency, and crash-consistent persistence.",
      "No VFS, runtime filesystem, descriptors, root mount, permissions, or persistent user data exists.",
      ["Define vnode/file/mount/namespace lifetime", "Specify path normalization and authority", "Select a minimal root/persistent format with migration policy", "Handle cache/writeback/flush/crash ordering", "Test hostile images, concurrency, power loss, and reboot persistence"],
      ["ROADMAP.md", "SECURITY.md"], ["M4 Devices and Storage"],
      ["synchronization-concurrency", "reliability-fault-injection", "threat-modelling"], ["VFS", "filesystem", "mount", "inode", "file descriptor"],
      blocked="Requires block service, userspace/IPC, and an accepted on-disk compatibility decision.",
      conditional_skills=["storage-block-devices and ipc-capabilities during implementation"])
skill("input-devices", "Input Devices", "Create secure normalized keyboard, pointer, touch, keymap, text-input, and focus event paths.",
      "No input driver, event model, keymap, focus routing, or UI exists.",
      ["Define raw device versus semantic input events", "Implement VirtIO input first", "Bound queues and validate ranges", "Separate key events from text composition", "Test focus/grab/revocation, multi-device, and malformed events"],
      ["HARDWARE_SUPPORT.md", "UI_GUIDELINES.md"], ["M4 Devices and Storage", "M5 Graphical Stack"],
      ["task-planning", "threat-modelling", "ui-ux-review"], ["keyboard", "pointer", "touch", "VirtIO input", "keymap"],
      blocked="Device event work requires driver/IPC foundations; UI routing requires compositor/session policy.",
      conditional_skills=["virtio, driver-architecture, and ipc-capabilities for device delivery", "compositor-window-system for focus routing"])
skill("usb", "USB", "Control post-critical-path host-controller, enumeration, transfer, HID/storage, hotplug, power, and recovery scope.",
      "No USB stack or controller driver exists; USB is explicitly not an early critical-path dependency.",
      ["Require an approved hardware use case", "Choose one controller family after PCI/DMA works", "Model device/config/interface/endpoint lifetime", "Bound transfer descriptors and timeouts", "Test disconnect, stall, malformed descriptors, and reset"],
      ["HARDWARE_SUPPORT.md", "ROADMAP.md"], ["post-1.0"],
      ["task-planning", "threat-modelling", "unsafe-rust-low-level-safety"], ["USB", "xHCI", "HID", "hotplug"],
      blocked="Defer until stable virtual input/storage and selected physical hardware make USB necessary.", maturity="planning",
      conditional_skills=["pci-pcie and driver-architecture for a selected controller", "storage-block-devices or input-devices for a selected class"])
skill("networking-drivers", "Networking Drivers", "Define NIC packet ownership, queues, DMA, interrupts, offloads, backpressure, reset, and statistics.",
      "No NIC interface or driver exists; QEMU networking is disabled.",
      ["Define bounded RX/TX buffer lifecycle", "Implement VirtIO net with minimal negotiated features", "Validate lengths/checksum metadata", "Handle queue backpressure/reset/interrupt moderation", "Test drops, malformed descriptors, saturation, and restart"],
      ["HARDWARE_SUPPORT.md", "ROADMAP.md"], ["M4 Devices and Storage"],
      ["synchronization-concurrency", "unsafe-rust-low-level-safety", "reliability-fault-injection"], ["NIC", "VirtIO net", "packet DMA", "offload"],
      blocked="Requires VirtIO/resource broker and a bounded network buffer contract.",
      conditional_skills=["virtio and driver-architecture for the first NIC"])
skill("audio-power-management", "Audio and Power Management", "Separate minimal shutdown/reboot work from deferred audio, battery, thermal, and platform power ambitions.",
      "No production shutdown/reboot, audio, mixer, stream timing, ACPI power, battery, or thermal support exists.",
      ["Implement emulator shutdown/reboot as M1 platform work", "Parse only required FADT/power data", "Defer audio until scheduler/IPC/timing/UI exist", "Define bounded streams/mixer clocks", "Qualify battery/thermal only on selected hardware"],
      ["HARDWARE_SUPPORT.md", "SUPPORTED_PLATFORMS.md", "ROADMAP.md"], ["M1 Dual-Architecture Boot", "post-1.0"],
      ["acpi-device-discovery", "timer-timekeeping", "cross-architecture-design"], ["shutdown", "reboot", "audio", "battery", "thermal"],
      architecture="x86 ACPI and ARM PSCI/platform mechanisms differ; expose shutdown semantics, not register-shaped APIs.",
      conditional_skills=["driver-architecture for post-1.0 audio, battery, and thermal devices"])

# Userspace and platform skills.
category("Userspace and platform")
skill("executable-loader", "Executable Loader", "Load untrusted user ELF segments, permissions, stack, arguments, and ABI state safely.",
      "Only the trusted kernel ELF loader exists; no user executable loader or process exists.",
      ["Define supported ELF class/machine/type/relocations", "Validate every range/overlap/permission", "Create W^X user mappings and guarded stack", "Populate bounded arguments/environment", "Test hostile ELF and cleanup after partial failure"],
      ["boot/uefi/src/lib.rs", "docs/architecture/processes.md"], ["M3 Userspace Foundation"],
      ["userspace-isolation", "virtual-memory", "unsafe-rust-low-level-safety"], ["user ELF", "executable loader", "process image"],
      blocked="Requires user address spaces and syscall/process lifecycle.")
skill("runtime-standard-library", "Runtime and Standard Library", "Build architecture-neutral userspace startup, allocation, threads, files, time, IPC, and errors on FinnOS ABI.",
      "No userspace ABI, runtime, standard library, headers/bindings, or SDK exists.",
      ["Freeze only the minimal proven syscall/IPC surface", "Add per-architecture startup stubs", "Define stable error/result conventions", "Wrap allocation/time/files/threads without bypassing rights", "Run runtime tests in isolated processes on both ports"],
      ["docs/development/application-development.md", "ROADMAP.md"], ["M3 Userspace Foundation", "M6 Developer Preview"],
      ["task-planning", "cross-architecture-design", "test-strategy"], ["runtime", "standard library", "startup code", "userspace ABI"],
      blocked="Requires a running isolated process and minimal syscall/IPC ABI.",
      conditional_skills=["syscall-development, ipc-capabilities, and executable-loader during implementation"])
skill("init-service-management", "Init and Service Management", "Design first-process startup, dependency order, restart, discovery, logging, shutdown, and recovery.",
      "No userspace or init exists.",
      ["Define minimal immutable bootstrap manifest", "Start logging and service discovery first", "Model dependency cycles/timeouts/restart budgets", "Grant explicit capabilities", "Test crash loops, missing dependencies, shutdown, and recovery mode"],
      ["ROADMAP.md", "docs/architecture/processes.md"], ["M3 Userspace Foundation"],
      ["logging-diagnostics", "capability-privilege-review", "reliability-fault-injection"], ["init", "service manager", "restart policy", "first process"],
      blocked="Requires userspace runtime, IPC, and executable loading.",
      conditional_skills=["runtime-standard-library and ipc-capabilities during implementation"])
skill("shell-core-utilities", "Shell and Core Utilities", "Deliver a diagnostic/recovery shell and minimal utilities without prematurely cloning UNIX semantics.",
      "No shell, command execution, filesystem API, pipes, scripting, or utilities exist.",
      ["Start with serial diagnostic commands", "Execute explicit process images", "Expose process/memory/service diagnostics", "Add filesystem commands only after VFS", "Define scripting/pipes only from native IPC and recovery needs"],
      ["ROADMAP.md", "docs/development/application-development.md"], ["M3 Userspace Foundation", "M4 Devices and Storage"],
      ["runtime-standard-library", "logging-diagnostics", "test-strategy"], ["shell", "core utilities", "recovery console"],
      blocked="Requires init/runtime; filesystem commands require VFS.",
      conditional_skills=["init-service-management and executable-loader for command execution", "vfs-filesystems for file commands"])
skill("logging-diagnostics", "Logging and Diagnostics", "Unify allocation-safe early serial, bounded kernel events, userspace logging, crash capture, privacy, and tooling.",
      "Early/kernel COM1 markers work; no ring buffer, structured event schema, userspace collector, persistent logs, or privacy policy exists.",
      ["Define severity/component/event IDs and bounded fields", "Keep early path allocation-free with timeout policy", "Add lock/context-safe kernel ring buffer", "Route userspace logs through explicit capability", "Redact, rotate, persist, and test crash/log-loss behavior"],
      ["docs/development/debugging.md", "kernel/docs/panic-policy.md", "SECURITY.md"], ["M0 Reproducible Build", "M3 Userspace Foundation", "M7 Beta"],
      ["synchronization-concurrency", "reliability-fault-injection"], ["logging", "serial", "crash log", "structured event"],
      ["./tools/finn test-boot"])
skill("package-update-architecture", "Package and Update Architecture", "Plan signed packages, metadata, atomic updates, rollback, recovery, and versions after prerequisites exist.",
      "No package format, repository, updater, persistent root, signing, or recovery exists.",
      ["Wait for userspace/storage/security foundations", "Define content-addressed signed metadata and capability manifests", "Separate app packages from system image updates", "Specify atomic staging/activation/rollback", "Threat-model repository, keys, downgrade, and interrupted update"],
      ["RELEASING.md", "SECURITY.md", "ROADMAP.md"], ["M6 Developer Preview", "M7 Beta"],
      ["task-planning", "threat-modelling", "architecture-documentation-adrs"], ["package", "update", "rollback", "repository metadata"],
      blocked="Planning only until persistent storage, process isolation, signing/entropy, and recovery boot paths exist.", maturity="planning",
      conditional_skills=["vfs-filesystems, init-service-management, cryptography-entropy, and secure-updates-recovery before implementation"])

# Networking skills.
category("Networking")
skill("network-stack-architecture", "Network Stack Architecture", "Define bounded packet buffers, driver/service boundary, sockets, routing, timers, concurrency, and isolation.",
      "Networking is entirely absent and QEMU uses -net none.",
      ["Choose userspace service boundary after IPC", "Define owned/refcounted bounded packet buffers", "Specify socket rights, routing, timers, and backpressure", "Separate protocol policy from NIC DMA", "Build loopback/model tests before enabling external QEMU networking"],
      ["ROADMAP.md", "SECURITY.md"], ["M4 Devices and Storage"],
      ["task-planning", "cross-architecture-design", "timer-timekeeping", "threat-modelling"], ["network stack", "socket", "routing", "packet buffer"],
      blocked="Requires userspace/IPC and network-device contract.",
      conditional_skills=["ipc-capabilities and networking-drivers during implementation", "network-security before exposing untrusted networks"])
skill("ethernet-arp-ipv4-icmp", "Ethernet, ARP, IPv4, and ICMP", "Implement the first bounded L2/L3 path with strict packet/state validation.",
      "No packet parser or network service exists.",
      ["Parse Ethernet with length/type bounds", "Implement capped ARP cache with expiry", "Validate IPv4 header/options/fragment policy/checksum", "Implement bounded ICMP echo/error handling", "Interoperate through QEMU and fuzz malformed packets"],
      ["ROADMAP.md", "SECURITY.md"], ["M4 Devices and Storage"],
      ["network-stack-architecture", "networking-drivers", "reliability-fault-injection"], ["Ethernet", "ARP", "IPv4", "ICMP", "ping"],
      blocked="Requires packet buffers, timers, and VirtIO net.")
skill("ipv6-neighbor-discovery", "IPv6 and Neighbor Discovery", "Add bounded IPv6, extension-header, ICMPv6, NDP, SLAAC, and route handling after IPv4 baseline.",
      "No network stack exists; IPv6 is beyond the first basic-network acceptance scope.",
      ["Bound extension-header chain and payload lengths", "Implement address/scope/route rules", "Validate ICMPv6 and NDP options", "Cap neighbor/SLAAC state and timers", "Test interoperability, spoofing, fragments, and malformed chains"],
      ["ROADMAP.md", "SECURITY.md"], ["M7 Beta"],
      ["ethernet-arp-ipv4-icmp", "network-security", "timer-timekeeping"], ["IPv6", "NDP", "SLAAC", "ICMPv6"],
      blocked="Defer until bounded IPv4/UDP networking is reliable and threat-reviewed.", maturity="planning")
skill("udp-tcp-dhcp-dns", "UDP, TCP, DHCP, and DNS", "Implement bounded transport/configuration/resolution state machines with timers and interoperability evidence.",
      "None of UDP, TCP, DHCP, or DNS exists; roadmap first network scope names UDP/DHCP/DNS before TCP/IPv6.",
      ["Add UDP demux/queues/checksums and limits", "Add DHCP lease state with hostile-option bounds", "Add DNS parser/cache/retry limits", "Implement TCP state/retransmit/window/congestion scope only after timer tests", "Fuzz and interoperate each layer independently"],
      ["ROADMAP.md", "SECURITY.md"], ["M4 Devices and Storage", "M7 Beta"],
      ["ethernet-arp-ipv4-icmp", "timer-timekeeping", "network-security", "reliability-fault-injection"], ["UDP", "TCP", "DHCP", "DNS"],
      blocked="Requires base IPv4 path; TCP requires mature timer, buffering, and resource-limit design.")
skill("network-security", "Network Security", "Threat-model packet parsing, exhaustion, socket authority, firewalling, entropy, TLS, and service isolation.",
      "No network exposure exists; entropy, TLS, firewall, socket permissions, and trust store are absent.",
      ["Define external and local attack surfaces", "Bound every parser/table/queue/timer", "Require socket/network capabilities", "Specify firewall default and audit events", "Gate TLS on reviewed entropy/crypto/trust-store services"],
      ["SECURITY.md", "ROADMAP.md"], ["M4 Devices and Storage", "M7 Beta"],
      ["threat-modelling", "capability-privilege-review", "reliability-fault-injection"], ["firewall", "TLS", "socket permission", "network threat"],
      conditional_skills=["cryptography-entropy before TLS or signed network identity"])

# Graphics and Peony skills.
category("Graphics and Peony")
skill("graphics-architecture", "Graphics Architecture", "Define display backends, software buffers/rendering, formats, damage, synchronization, scaling, and future acceleration.",
      "Only GOP metadata and a full-screen kernel fill exist; no display driver, renderer, buffer protocol, or multi-display support.",
      ["Gate on userspace/IPC/display ownership", "Define buffer format/stride/ownership/lifetime", "Make software rendering baseline", "Specify damage/frame synchronization/scaling", "Defer GPU/multi-display until reference workload and driver model"],
      ["UI_GUIDELINES.md", "docs/architecture/peony.md", "HARDWARE_SUPPORT.md"], ["M5 Graphical Stack"],
      ["task-planning", "cross-architecture-design", "performance-engineering"], ["graphics", "framebuffer", "display buffer", "software renderer"],
      blocked="Requires isolated userspace, IPC, input, and a display resource path.",
      conditional_skills=["ipc-capabilities and driver-architecture during implementation"])
skill("compositor-window-system", "Compositor and Window System", "Implement secure client surfaces, windows, composition, focus/input, frame timing, crash isolation, and tests.",
      "No compositor, window server, clients, input routing, or session exists.",
      ["Define versioned client/server surface protocol", "Validate buffer/damage/window state atomically", "Implement z-order/occlusion/focus/grabs", "Schedule frames and measure latency", "Test malicious clients, compositor restart, screenshots, and leaks"],
      ["UI_GUIDELINES.md", "ROADMAP.md"], ["M5 Graphical Stack"],
      ["graphics-architecture", "capability-privilege-review", "performance-engineering", "reliability-fault-injection"], ["compositor", "window server", "surface", "focus"],
      blocked="Requires M4 input/storage and M2/M3 isolation/IPC.",
      conditional_skills=["input-devices and ipc-capabilities during implementation"])
skill("text-fonts-localization", "Text, Fonts, and Localization", "Implement licensed font loading, rasterization, shaping, Unicode/bidi/fallback, scaling, input methods, and caches.",
      "No font, text renderer, shaping, localization, input method, or accessibility runtime exists.",
      ["Select licensed fonts and Unicode/shaping library strategy", "Bound parser/cache inputs", "Implement script/bidi/fallback/line-break tests", "Support 1x/1.5x/2x and 200% text", "Test RTL, mixed direction, missing glyphs, and 30% expansion"],
      ["UI_GUIDELINES.md", "docs/architecture/peony.md"], ["M5 Graphical Stack"],
      ["graphics-architecture", "ui-ux-review", "reliability-fault-injection"], ["font", "text shaping", "Unicode", "localization", "bidi"],
      blocked="Requires graphics buffers/toolkit foundations.",
      conditional_skills=["input-devices for input-method integration"])
skill("peony-design-system", "Peony Design System", "Apply FinnOS semantic tokens, controls, states, themes, motion, accessibility, localization, DPI, and performance rules.",
      "UI_GUIDELINES.md is a proposal; no Peony runtime or component is implemented.",
      ["Use semantic color/type/spacing/geometry tokens", "Implement every required state and focus indicator", "Verify keyboard and accessibility semantics", "Test themes, scaling, RTL, text expansion, reduced motion", "Record screenshots and reference workload performance"],
      ["UI_GUIDELINES.md"], ["M5 Graphical Stack"],
      ["graphics-architecture", "text-fonts-localization", "ui-ux-review"], ["Peony", "design token", "theme", "accessibility"],
      blocked="Design artifacts may proceed; implementation waits for compositor/toolkit dependencies.")
skill("peony-toolkit-development", "Peony Toolkit Development", "Build widget tree, layout, painting, events, focus, accessibility, themes, text, invalidation, and lifecycle APIs.",
      "No toolkit, widget tree, app lifecycle, accessibility tree, or API stability exists.",
      ["Define retained/immediate state ownership deliberately", "Implement deterministic layout constraints", "Propagate invalidation/damage minimally", "Unify pointer/keyboard/assistive actions", "Test widgets, accessibility tree, themes, text, lifecycle, and API examples"],
      ["UI_GUIDELINES.md", "docs/architecture/peony.md"], ["M5 Graphical Stack", "M6 Developer Preview"],
      ["peony-design-system", "text-fonts-localization", "ui-ux-review", "performance-engineering"], ["widget", "layout", "Peony toolkit", "accessibility tree"],
      blocked="Requires compositor, text, userspace runtime, and input.",
      conditional_skills=["compositor-window-system and runtime-standard-library during implementation"])
skill("desktop-shell", "Desktop Shell", "Implement session/login, panel, launcher, notifications, window policy, settings, lock, shutdown, recovery, and accessibility.",
      "No login/session/shell/window manager/notification/settings/lock screen exists.",
      ["Define session and authentication boundary", "Start minimal window policy/panel/launcher", "Add privacy-safe notifications and settings", "Integrate lock/shutdown/recovery capabilities", "Test restart, keyboard-only use, locked data, and service failure"],
      ["UI_GUIDELINES.md", "ROADMAP.md", "SECURITY.md"], ["M5 Graphical Stack"],
      ["peony-toolkit-development", "capability-privilege-review", "ui-ux-review", "threat-modelling"], ["desktop shell", "launcher", "notifications", "lock screen"],
      blocked="Requires toolkit, session services, storage/settings, and power control.",
      conditional_skills=["init-service-management and audio-power-management during implementation"])
skill("core-graphical-applications", "Core Graphical Applications", "Build terminal, file manager, settings, viewer/editor, monitor, and later installer behind explicit dependency gates.",
      "No graphical application or application SDK exists.",
      ["Verify toolkit/runtime/package API is usable", "Implement terminal after shell/process PTY-equivalent", "Implement file manager after VFS", "Implement settings/monitor from bounded service APIs", "Defer installer until persistent image/recovery exists"],
      ["UI_GUIDELINES.md", "docs/development/application-development.md", "ROADMAP.md"], ["M5 Graphical Stack", "M6 Developer Preview"],
      ["peony-toolkit-development", "ui-ux-review", "test-strategy"], ["terminal app", "file manager", "settings app", "installer"],
      blocked="Each app is blocked by its named service and toolkit dependencies; mockups are not implementation.",
      conditional_skills=["shell-core-utilities for terminal", "vfs-filesystems for file manager", "package-update-architecture only for installer/package UI"])
skill("ui-ux-review", "UI/UX Review", "Apply measurable visual, interaction, accessibility, localization, scaling, state, and performance review gates.",
      "No implemented UI can currently pass review; this skill also reviews design proposals without relabeling them implemented.",
      ["Capture target/state matrix", "Measure alignment/type/spacing/contrast", "Traverse every action by keyboard", "Inspect accessibility semantics and responsive states", "Record screenshots, latency, localization, scaling, and usability results"],
      ["UI_GUIDELINES.md", ".agents/checklists/ui-review.md"], ["M5 Graphical Stack", "M7 Beta"],
      ["test-strategy", "documentation-maintenance"], ["UI review", "accessibility", "visual regression", "usability"])

# Security skills.
category("Security")
skill("threat-modelling", "Threat Modelling", "Identify assets, actors, trust boundaries, attack surfaces, likelihood/impact, mitigations, and review cadence.",
      "SECURITY.md defines a baseline but no complete threat model exists; current ring-0-only system has no hostile-code boundary.",
      ["Scope assets and supported deployment", "Draw firmware/loader/kernel/process/driver/service/storage/network/update boundaries", "Enumerate threats and denial-of-service", "Prioritize by evidence and milestone", "Assign mitigations/tests/residual risk and revisit on boundary changes"],
      ["SECURITY.md", "docs/audit/2026-07-16.md"], ["M2 Core Kernel", "M7 Beta"],
      ["repository-orientation", "evidence-status-reporting"], ["threat model", "attack surface", "trust boundary"])
skill("unsafe-rust-low-level-safety", "Unsafe Rust and Low-Level Safety", "Review pointers, provenance, aliasing, alignment, lifetimes, conversions, MMIO, volatile access, and assembly contracts.",
      "Boot/kernel contain necessary unsafe and assembly; repository policy requires specific SAFETY arguments. Miri cannot execute bare-metal hardware paths.",
      ["State preconditions at every unsafe boundary", "Prove pointer provenance/alignment/lifetime and integer bounds", "Use volatile only for MMIO, not synchronization", "Specify assembly clobbers/ABI/stack", "Test pure models plus QEMU fault behavior and review generated assembly when needed"],
      ["kernel/docs/unsafe-code.md", "CONTRIBUTING.md", "Cargo.toml"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["task-planning"], ["unsafe Rust", "assembly", "MMIO", "pointer"],
      commands=["cargo clippy --workspace --all-targets -- -D warnings", "./tools/finn check"])
skill("boot-kernel-hardening", "Boot and Kernel Hardening", "Preserve W^X/NX/isolation/guards/null protection and plan entropy, KASLR, panic, secure/verified boot.",
      "x86 supervisor W^X/NX/WP/null/guards exist; no user isolation, KASLR, stack canary, secure boot, signatures, or entropy service.",
      ["Inventory mapping and trust boundaries", "Test permission-negative cases", "Validate all loader/handoff/user pointers", "Bound panic diagnostics and failure state", "Gate KASLR/secure boot on entropy/key/update architecture"],
      ["SECURITY.md", "docs/architecture/x86_64-virtual-memory.md", "kernel/docs/panic-policy.md"], ["M0 Reproducible Build", "M2 Core Kernel", "M7 Beta"],
      ["threat-modelling", "uefi-bootloader-development", "unsafe-rust-low-level-safety"], ["W^X", "NX", "KASLR", "secure boot", "guard page"])
skill("capability-privilege-review", "Capability and Privilege Review", "Verify least authority, rights, transfer, revocation, device/service privilege, and audit evidence.",
      "Capability architecture is planned only; no enforced rights or service/process boundary exists.",
      ["List required authority per operation", "Remove ambient/global access", "Trace handle grant/duplicate/transfer/revoke", "Review service and driver resource scope", "Test forged/stale/excess rights and audit events"],
      ["docs/architecture/capabilities.md", "SECURITY.md"], ["M2 Core Kernel", "M7 Beta"],
      ["threat-modelling"], ["least privilege", "capability review", "rights"])
skill("cryptography-entropy", "Cryptography and Entropy", "Define trustworthy entropy, DRBG, reviewed crypto APIs, key storage, signing, and TLS prerequisites.",
      "No entropy source, RNG, crypto API, key store, signatures, or TLS exists.",
      ["Threat-model boot/platform entropy", "Use reviewed standard primitives/libraries only", "Separate entropy collection, DRBG, and consumers", "Define key generation/storage/rotation/zeroization", "Test known vectors, failure-to-seed, fork/process separation, and platform variance"],
      ["SECURITY.md", "RELEASING.md"], ["M6 Developer Preview", "M7 Beta"],
      ["threat-modelling", "driver-architecture", "unsafe-rust-low-level-safety"], ["entropy", "DRBG", "cryptography", "signing", "key storage"],
      blocked="Implementation waits for platform entropy devices and userspace isolation; never create custom cryptography.")
skill("secure-updates-recovery", "Secure Updates and Recovery", "Design signed, atomic, rollback-aware updates and recovery that preserve user data.",
      "No persistent system, updater, signatures, boot slot policy, recovery environment, or rollback protection exists.",
      ["Define signed manifest/provenance and trust roots", "Choose atomic slot/snapshot activation", "Handle failed boot and power loss", "Separate rollback recovery from anti-downgrade policy", "Test tamper, interrupted update, key rotation, and data migration"],
      ["RELEASING.md", "SECURITY.md", "ROADMAP.md"], ["M6 Developer Preview", "M7 Beta"],
      ["cryptography-entropy", "vfs-filesystems", "reliability-fault-injection"], ["secure update", "recovery", "rollback protection"],
      blocked="Requires persistent storage, release signing, boot policy, and recovery image.")
skill("vulnerability-response", "Vulnerability Response", "Privately receive, triage, fix, validate, disclose, advise, and backport security issues.",
      "Private GitHub reporting is preferred; there is no SLA, supported-version window, or released OS.",
      ["Keep report private and acknowledge scope", "Assign severity from affected supported claims", "Reproduce and develop regression", "Coordinate fix/advisory/credit/embargo", "Backport only supported versions and update threat model"],
      ["SECURITY.md", "RELEASING.md"], ["M6 Developer Preview", "M8 Stable 1.0"],
      ["threat-modelling"], ["vulnerability", "security advisory", "CVE", "disclosure"],
      safety="Never place exploit details, reporter identity, credentials, or embargoed patches in public issues/logs.")

# Quality, performance, release, and documentation skills.
category("Quality, release, and docs")
skill("ci-maintenance", "CI Maintenance", "Maintain required checks, architecture/profile matrices, QEMU evidence, artifacts, caching, and branch protection.",
      "Workflows pin actions, use least privilege/concurrency, build both x86 profiles, boot release, and retain failure evidence; CI execution is pending integration and ARM remains absent.",
      ["Match local commands exactly", "Set explicit least-privilege permissions/concurrency", "Retain logs/manifests/ELFs/images on failure", "Add target/profile matrix without feature contamination", "Verify protected check names and diagnose CI-only divergence"],
      [".github/workflows/ci.yml", ".github/workflows/boot-smoke.yml", "TESTING.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["build-orchestration", "qemu-boot-testing", "test-strategy"], ["CI", "GitHub Actions", "required check", "artifact retention"],
      ["./tools/finn check", "./tools/finn test-boot --profile release", "python3 .agents/scripts/validate.py --all"],
      version=2, verified_commit="cc828ec", verified_context="R1 publication branch locally verified; integration CI pending", verified_dirty=False)
skill("performance-engineering", "Performance Engineering", "Measure reproducible boot, scheduling, memory, IPC, storage, network, rendering, and input regressions before optimizing.",
      "Only UI proposal targets exist; no stable benchmark suite or baseline hardware is defined.",
      ["Define workload/environment/metric and warmup", "Capture distributions and resource counters", "Profile before code changes", "Set evidence-based regression threshold", "Compare x86/ARM semantics without assuming equal hardware"],
      ["UI_GUIDELINES.md", "ROADMAP.md"], ["M5 Graphical Stack", "M7 Beta"],
      ["test-strategy", "evidence-status-reporting"], ["benchmark", "latency", "boot time", "performance"])
skill("reliability-fault-injection", "Reliability and Fault Injection", "Inject deterministic allocation, input, interrupt, timeout, device, storage, network, and service failures.",
      "Current tests cover allocator exhaustion and selected expected faults; no general injection framework or recovery tests exist.",
      ["Select one failure boundary and deterministic trigger", "State pre/post ownership invariants", "Inject before/after each commit point", "Verify bounded diagnostics, rollback, and liveness", "Keep production path unchanged when injection is disabled"],
      ["TESTING.md", "docs/audit/2026-07-16.md"], ["M0 Reproducible Build", "M7 Beta"],
      ["test-strategy", "debugging-investigation"], ["fault injection", "allocation failure", "timeout", "recovery"])
skill("release-engineering", "Release Engineering", "Produce versioned profiles, artifacts, hashes, signatures, provenance, notes, upgrade/recovery evidence, and approvals.",
      "No OS release exists; first-boot tag is a milestone. Release components compile but no release image boots; no signing/SBOM/provenance.",
      ["Open release tracking scope", "Build from pinned clean environment", "Run full qualification and independent rebuild", "Generate signed image/checksum/SBOM/provenance/notes", "Verify downloaded artifacts and never replace tags silently"],
      ["RELEASING.md", "RELEASES.md", "CHANGELOG.md"], ["M6 Developer Preview", "M8 Stable 1.0"],
      ["ci-maintenance", "git-commit-hygiene", "vulnerability-response", "threat-modelling"], ["release", "artifact", "provenance", "SBOM", "signature"],
      blocked="OS releases wait for preview prerequisites; current tags must remain development milestones.",
      conditional_skills=["secure-updates-recovery for an updatable OS release"])
skill("stable-release-readiness", "Stable Release Readiness", "Audit maturity Level 6 across build, architecture, product, security, recovery, docs, hardware, and operations.",
      "FinnOS is Level 0 for x86 and partial Level 1; every product/release gate remains open.",
      ["Freeze supported scope", "Audit each STATUS maturity criterion with evidence", "Require x86/ARM or explicitly narrowed supported architecture policy", "Qualify install/update/recovery/security/hardware", "Reject release with open P0/P1 or unsupported claims"],
      ["STATUS.md", "RELEASING.md", "SUPPORTED_PLATFORMS.md"], ["M8 Stable 1.0"],
      ["release-engineering", "threat-modelling", "evidence-status-reporting", "status-roadmap-updates"], ["stable release", "1.0", "release readiness"],
      blocked="Not actionable as a release approval until M0-M7 acceptance evidence exists.",
      conditional_skills=["ui-ux-review, secure-updates-recovery, hardware, storage, and network skills for their release gates"])
skill("documentation-maintenance", "Documentation Maintenance", "Keep authoritative current/future boundaries, commands, links, terminology, dates, and implementation claims synchronized.",
      "Audit docs are current at 3539a35 plus uncommitted changes; legacy and component docs can drift from canonical root/docs architecture.",
      ["Choose canonical document", "Verify behavior/command from source and execution", "Use explicit state classification and scope", "Link rather than duplicate", "Run link/agent validation and update affected skills"],
      ["docs/README.md", "STATUS.md", "ROADMAP.md", ".agents/GOVERNANCE.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["repository-orientation", "evidence-status-reporting"], ["documentation", "README", "broken link", "stale docs"],
      ["python3 .agents/scripts/check_links.py", "python3 .agents/scripts/validate.py --all"])
skill("architecture-documentation-adrs", "Architecture Documentation and ADRs", "Record durable decisions, alternatives, architecture/security impact, migration, and supersession.",
      "ADRs 0001-0014 capture current direction and x86 milestones; RFC infrastructure exists with no accepted non-template RFC.",
      ["Prove the decision is durable/cross-cutting", "Document problem/evidence/constraints/options", "Map x86 and ARM plus security", "Specify migration/rollback/contract tests", "Mark accepted/rejected/superseded explicitly"],
      ["docs/proposals/adr/README.md", "docs/proposals/rfcs/README.md", ".agents/templates/architecture-decision-template.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["documentation-maintenance", "cross-architecture-design", "threat-modelling"], ["ADR", "RFC", "architecture decision"])
skill("status-roadmap-updates", "Status and Roadmap Updates", "Change percentages, confidence, milestone state, blockers, and critical path only from acceptance evidence.",
      "STATUS and ROADMAP are audit-derived; M0 is current and later feature layers remain unimplemented.",
      ["Identify exact claim and acceptance criterion", "Gather integrated/runtime/hardware evidence", "Update classification/confidence before percentage", "Move milestone only when all exit criteria pass", "Retain historical audit and record blockers/unknowns"],
      ["STATUS.md", "ROADMAP.md", "docs/audit/2026-07-16.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["evidence-status-reporting", "roadmap-execution", "documentation-maintenance"], ["roadmap update", "status percentage", "milestone complete"])
skill("contributor-experience", "Contributor Experience", "Keep setup, first build/boot, issues, debugging, review, support, and AI contribution expectations executable.",
      "Current onboarding supports x86 development but no SDK/application path; ownership slots are unassigned.",
      ["Test instructions from clean environment", "Minimize first successful command path", "Create bounded evidence-rich first issues", "Link debugging/review/support policies", "Require AI contributions to disclose tests, limitations, and handoff"],
      ["CONTRIBUTING.md", "BUILDING.md", "TESTING.md", "SUPPORT.md"], ["M0 Reproducible Build", "M6 Developer Preview"],
      ["documentation-maintenance", "github-project-management", "build-environment-management"], ["contributor", "onboarding", "good first issue"])

# Specialized execution skills.
category("Execution workflows")
skill("implementing-roadmap-issue", "Implementing a Roadmap Issue", "Execute one issue from selection through baseline, code, evidence, docs, commit preparation, and handoff.",
      "Roadmap R1/R2 are first; active #16/#17 overlap must be checked before scheduler work.",
      ["Confirm issue/milestone/dependencies", "Write plan and failing baseline", "Implement smallest acceptance slice", "Run narrow and aggregate gates", "Update docs/status, inspect diff, prepare PR evidence and handoff"],
      ["ROADMAP.md", ".agents/templates/task-plan-template.md", ".agents/templates/implementation-report-template.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["roadmap-execution", "task-planning", "test-strategy", "documentation-maintenance", "git-commit-hygiene", "agent-handoff"], ["implement issue", "roadmap item"])
skill("fixing-boot-failure", "Fixing a Boot Failure", "Use a staged decision tree to localize image, firmware, loader, handoff, entry, paging, exception, emulator, or harness failures.",
      "Current x86 marker sequence is validated; ARM boot does not exist.",
      ["Verify image files/manifest", "Verify firmware/QEMU command", "Classify last loader marker", "Validate BootInfo and kernel entry", "Inspect CR3/mappings/exceptions", "Check harness status/ordering/timeout", "Add regression at owning layer"],
      ["docs/development/debugging.md", "tools/finnlib/qemu.py", "docs/architecture/boot.md"], ["M0 Reproducible Build", "M1 Dual-Architecture Boot"],
      ["debugging-investigation", "qemu-boot-testing", "unsafe-rust-low-level-safety", "agent-handoff"], ["boot failure", "QEMU timeout", "missing marker"],
      conditional_skills=["uefi-bootloader-development for loader/handoff failures", "virtual-memory for paging failures", "interrupt-exception-handling for trap failures"])
skill("adding-architecture-abstraction", "Adding an Architecture Abstraction", "Introduce a minimal shared contract only after mapping existing x86 and required ARM semantics.",
      "Architecture-independent policy exists but executable dispatch is x86-only, making premature abstractions especially risky.",
      ["Document current invariant/callers", "Map x86 mechanism", "Map ARM mechanism from architecture specs", "Define narrow semantic contract", "Add contract tests and staged migration", "Record ADR and intentional differences"],
      ["ARCHITECTURE.md", "PORTING.md"], ["M1 Dual-Architecture Boot", "M2 Core Kernel"],
      ["cross-architecture-design", "architecture-documentation-adrs", "test-strategy", "agent-handoff"], ["add abstraction", "platform trait"])
skill("adding-driver", "Adding a Driver", "Implement one specified device through discovery, resources, MMIO/PIO, DMA, IRQ, timeout, recovery, tests, and support docs.",
      "No general driver can currently be added end-to-end because resource broker/userspace/IRQ/DMA prerequisites are absent.",
      ["Confirm dependency gates and public spec", "Add exact device matching", "Acquire bounded resources", "Implement register/queue state with barriers", "Handle IRQ/timeouts/reset/removal", "Test QEMU faults then update exact hardware claim"],
      ["HARDWARE_SUPPORT.md", "docs/development/driver-development.md", ".agents/checklists/driver-change.md"], ["M4 Devices and Storage"],
      ["driver-architecture", "interrupt-exception-handling", "unsafe-rust-low-level-safety", "reliability-fault-injection", "agent-handoff"], ["add driver", "device support"],
      blocked="Report blocked instead of bypassing discovery, isolation, IRQ, or DMA ownership.")
skill("adding-syscall", "Adding a Syscall", "Add one reviewed semantic operation across ABI, architecture entries, rights, user memory, wrappers, tests, and docs.",
      "No syscall table or userspace exists; this workflow is gated.",
      ["Prove operation belongs in kernel ABI", "Assign reviewed version/number", "Implement both architecture entry mappings", "Validate rights/scalars/user ranges", "Add user wrapper and adversarial tests", "Document compatibility/restart/error behavior"],
      ["ROADMAP.md", "docs/architecture/processes.md"], ["M2 Core Kernel"],
      ["syscall-development", "capability-privilege-review", "unsafe-rust-low-level-safety", "test-strategy", "agent-handoff"], ["add syscall"],
      blocked="Cannot execute until user isolation and architecture entry paths exist.")
skill("adding-userspace-service", "Adding a Userspace Service", "Define one service's responsibility, capabilities, IPC, lifecycle, diagnostics, recovery, security, and tests.",
      "No userspace service manager or IPC exists.",
      ["Keep responsibility narrow", "Declare least capabilities and versioned IPC", "Define startup/readiness/dependencies", "Handle cancellation/client/service death", "Test restart, malformed requests, quotas, and docs"],
      ["docs/architecture/processes.md", "docs/architecture/ipc.md"], ["M3 Userspace Foundation", "M4 Devices and Storage"],
      ["task-planning", "capability-privilege-review", "logging-diagnostics", "reliability-fault-injection", "agent-handoff"], ["add service", "userspace daemon"],
      blocked="Requires init, runtime, IPC, and process isolation.",
      conditional_skills=["init-service-management and ipc-capabilities during implementation"])
skill("adding-peony-component", "Adding a Peony Component", "Implement a token-correct, state-complete, accessible, localized, scalable, performant Peony component.",
      "No Peony toolkit exists; component work is currently design-only and blocked.",
      ["Confirm toolkit/compositor gate", "Specify semantics and all states", "Use tokens/layout and unified input actions", "Expose accessibility and localization behavior", "Test themes/scales/keyboard/visual/performance", "Document screenshots as implementation evidence only when running"],
      ["UI_GUIDELINES.md", ".agents/checklists/ui-review.md"], ["M5 Graphical Stack"],
      ["graphics-architecture", "peony-design-system", "peony-toolkit-development", "ui-ux-review", "test-strategy", "agent-handoff"], ["add Peony component", "widget"],
      blocked="Do not create disconnected UI code before the Peony toolkit and runtime exist.")
skill("investigating-architectural-change", "Investigating an Architectural Change", "Produce an evidence-based recommendation with invariants, alternatives, cross-architecture/security impact, prototype, and migration.",
      "Many durable interfaces remain unresolved; ADRs must not turn planned architecture into implementation claims.",
      ["Trace existing behavior/history", "State constraints/invariants", "Compare at least viable alternatives", "Map x86/ARM/security/compatibility", "Define smallest prototype and falsification criteria", "Recommend ADR, defer, or reject"],
      ["docs/proposals/adr/", ".agents/templates/investigation-template.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["repository-orientation", "cross-architecture-design", "threat-modelling", "architecture-documentation-adrs", "agent-handoff"], ["architecture investigation", "design proposal"])
skill("preparing-pull-request", "Preparing a Pull Request", "Prepare scoped issue linkage, diff evidence, tests, architecture/security impact, docs, reviewer guidance, and limitations.",
      "Protected main requires foundation/smoke and conversation resolution; approval count was zero at audit time.",
      ["Rebase/update without rewriting shared history", "Review complete diff and commits", "Run required gates", "Write current/desired/scope/non-goals and exact evidence", "Highlight unsafe/architecture/security and follow-ups", "Attach logs/screenshots only when relevant"],
      [".github/PULL_REQUEST_TEMPLATE.md", "CONTRIBUTING.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["git-commit-hygiene", "code-review", "documentation-maintenance", "agent-handoff"], ["prepare PR", "pull request"])
skill("reviewing-pull-request", "Reviewing a Pull Request", "Verify claims through local reproduction, invariants, unsafe, architecture, security, performance, docs, and scope before approval.",
      "Recent PR #17 has green latest checks but earlier failures and explicitly does not implement timer-ISR preemption.",
      ["Read issue/acceptance and all commits", "Reproduce baseline/change where feasible", "Audit diff and generated artifacts", "Challenge unsafe/bounds/ownership/architecture/security", "Evaluate negative tests and docs", "Approve only when claims match evidence"],
      ["CONTRIBUTING.md", ".agents/checklists/pre-commit.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["code-review", "test-strategy", "evidence-status-reporting", "agent-handoff"], ["review PR", "approve pull request"])
skill("agent-handoff", "Agent Handoff", "Leave durable objective, state, work, files, evidence, unknowns, blockers, risks, Git state, and next skills.",
      "Conversational memory is not durable; repository-local handoffs must distinguish worktree changes from integrated behavior.",
      ["Record starting and final Git state", "List work/files without claiming integration", "List exact commands/results and evidence class", "Name unknowns/blockers/risks", "Recommend one next action and required skills"],
      [".agents/templates/handoff-template.md", ".agents/README.md"], ["M0 Reproducible Build", "M8 Stable 1.0"],
      ["finnos-operating-rules"], ["handoff", "end session"],
      ["python3 .agents/scripts/new_handoff.py"])


# Normalize human-readable conditional notes into machine-readable skill names after the
# full registry exists. Notes remain available for context in generated skill bodies.
_skill_names = {item["name"] for item in SKILLS}
for _spec in SKILLS:
    _spec["conditional_skills"] = [
        name for name in sorted(_skill_names)
        if any(name in note for note in _spec["conditional_notes"])
    ]
