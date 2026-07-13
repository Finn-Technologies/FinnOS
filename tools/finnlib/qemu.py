"""QEMU command and smoke-log interpretation."""
from __future__ import annotations

MARKERS = (
    "FINNOS:BOOTLOADER:START", "FINNOS:BOOTLOADER:KERNEL_FOUND", "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED", "FINNOS:BOOTLOADER:FRAMEBUFFER_READY", "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ENTRY", "FINNOS:KERNEL:GDT_OK", "FINNOS:KERNEL:TSS_OK", "FINNOS:KERNEL:IDT_OK",
    "FINNOS:KERNEL:EXCEPTIONS_READY", "FINNOS:KERNEL:BOOTINFO_OK", "FINNOS:KERNEL:MEMORY_MAP_OK",
    "FINNOS:KERNEL:MEMORY_MAP_PARSED", "FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED", "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:KERNEL:PAGE_TABLES_BUILT", "FINNOS:KERNEL:PAGE_TABLES_ACTIVATING", "FINNOS:KERNEL:PAGE_TABLES_ACTIVE", "FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED", "FINNOS:KERNEL:HEAP_MAPPED", "FINNOS:KERNEL:HEAP_READY", "FINNOS:KERNEL:INTERRUPT_IDT_READY", "FINNOS:KERNEL:PIC_REMAPPED", "FINNOS:KERNEL:PIC_MASKED", "FINNOS:KERNEL:LOCAL_APIC_MAPPED", "FINNOS:KERNEL:LOCAL_APIC_READY", "FINNOS:KERNEL:TIMER_CALIBRATED", "FINNOS:KERNEL:TIMER_STARTED", "FINNOS:KERNEL:INTERRUPTS_ENABLED", "FINNOS:KERNEL:TIMER_READY", "FINNOS:KERNEL:FRAMEBUFFER_OK", "FINNOS:KERNEL:FIRST_BOOT_COMPLETE",
)

PAGE_TABLE_MARKERS = MARKERS + (
    "FINNOS:TEST:PAGE_TABLES:BEGIN", "FINNOS:TEST:PAGE_TABLES:CR3_OK",
    "FINNOS:TEST:PAGE_TABLES:PERMISSIONS_OK", "FINNOS:TEST:PAGE_TABLES:GUARD_PAGES_OK",
    "FINNOS:TEST:PAGE_TABLES:SCRATCH_MAP_OK", "FINNOS:TEST:PAGE_TABLES:SCRATCH_UNMAP_OK",
    "FINNOS:TEST:PAGE_TABLES:PAGE_FAULT_BEGIN", "FINNOS:EXCEPTION:PAGE_FAULT",
    "FINNOS:TEST:PAGE_TABLES:PAGE_FAULT_PASS",
)

EXCEPTION_MARKERS = (
    "FINNOS:KERNEL:GDT_OK", "FINNOS:KERNEL:TSS_OK", "FINNOS:KERNEL:IDT_OK",
    "FINNOS:KERNEL:EXCEPTIONS_READY",
    "FINNOS:KERNEL:PAGE_TABLES_BUILT", "FINNOS:KERNEL:PAGE_TABLES_ACTIVATING", "FINNOS:KERNEL:PAGE_TABLES_ACTIVE", "FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED", "FINNOS:KERNEL:HEAP_MAPPED", "FINNOS:KERNEL:HEAP_READY",
    "FINNOS:TEST:EXCEPTIONS:BEGIN",
    "FINNOS:EXCEPTION:BREAKPOINT", "FINNOS:TEST:BREAKPOINT:PASS",
    "FINNOS:TEST:INVALID_OPCODE:BEGIN", "FINNOS:EXCEPTION:INVALID_OPCODE",
    "FINNOS:TEST:INVALID_OPCODE:PASS",
)

FORBIDDEN_EXCEPTION_MARKERS = (
    "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:EXCEPTION:GENERAL_PROTECTION",
    "FINNOS:EXCEPTION:PAGE_FAULT", "FINNOS:EXCEPTION:UNHANDLED", "FINNOS:KERNEL:PANIC",
)

MEMORY_MAP_MARKERS = (
    "FINNOS:KERNEL:MEMORY_MAP_OK",
    "FINNOS:KERNEL:MEMORY_MAP_PARSED",
    "FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED",
    "FINNOS:MEMORY:DESCRIPTORS=",
    "FINNOS:MEMORY:REGIONS=",
    "FINNOS:MEMORY:USABLE_BYTES=",
    "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:KERNEL:PAGE_TABLES_BUILT", "FINNOS:KERNEL:PAGE_TABLES_ACTIVATING", "FINNOS:KERNEL:PAGE_TABLES_ACTIVE", "FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED", "FINNOS:KERNEL:HEAP_MAPPED", "FINNOS:KERNEL:HEAP_READY",
    "FINNOS:KERNEL:FIRST_BOOT_COMPLETE",
)

PAGE_ALLOCATOR_MARKERS = (
    "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:TEST:PAGE_ALLOCATOR:BEGIN",
    "FINNOS:TEST:PAGE_ALLOCATOR:SINGLE_ALLOC_OK",
    "FINNOS:TEST:PAGE_ALLOCATOR:CONTIGUOUS_ALLOC_OK",
    "FINNOS:TEST:PAGE_ALLOCATOR:REUSE_OK",
    "FINNOS:TEST:PAGE_ALLOCATOR:FREE_OK",
    "FINNOS:TEST:PAGE_ALLOCATOR:DOUBLE_FREE_REJECTED",
    "FINNOS:TEST:PAGE_ALLOCATOR:INVARIANTS_OK",
    "FINNOS:TEST:PAGE_ALLOCATOR:PASS",
)

MEMORY_MAP_FORBIDDEN_MARKERS = (
    "FINNOS:KERNEL:MEMORY_MAP_ERROR",
    "FINNOS:EXCEPTION:FATAL",
    "FINNOS:KERNEL:PANIC",
)

def validate_smoke(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in MARKERS]
    if any(position < 0 for position in positions): errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("boot markers are out of order")
    if "FINNOS:BOOTLOADER:ERROR:" in output: errors.append("bootloader error marker found")
    if "FINNOS:KERNEL:PANIC" in output: errors.append("kernel panic marker found")
    return errors

def validate_memory_map(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in MEMORY_MAP_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(MEMORY_MAP_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("memory-map markers are out of order")
    for marker in MEMORY_MAP_FORBIDDEN_MARKERS:
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    if "FINNOS:BOOTLOADER:ERROR:" in output: errors.append("bootloader error marker found")
    # Require positive counts and sizes.
    if "FINNOS:MEMORY:USABLE_BYTES=0" in output: errors.append("expected positive usable bytes")
    if "FINNOS:MEMORY:KERNEL_BYTES=0" in output: errors.append("expected positive kernel bytes")
    if "FINNOS:MEMORY:FRAMEBUFFER_BYTES=0" in output: errors.append("expected positive framebuffer bytes")
    if "FINNOS:MEMORY:DESCRIPTORS=0" in output: errors.append("expected positive descriptor count")
    if "FINNOS:MEMORY:REGIONS=0" in output: errors.append("expected positive region count")
    return errors

def validate_exceptions(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in EXCEPTION_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(EXCEPTION_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("exception markers are out of order")
    for marker in FORBIDDEN_EXCEPTION_MARKERS:
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    if "FINNOS:BOOTLOADER:ERROR:" in output: errors.append("bootloader error marker found")
    return errors

def validate_page_allocator(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in PAGE_ALLOCATOR_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(PAGE_ALLOCATOR_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("page-allocator markers are out of order")
    for marker in ("FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR", "FINNOS:EXCEPTION:FATAL", "FINNOS:EXCEPTION:PAGE_FAULT", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:KERNEL:PANIC"):
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    return errors

def validate_page_tables(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in PAGE_TABLE_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(PAGE_TABLE_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("page-table markers are out of order")
    for marker in ("FINNOS:KERNEL:PAGE_TABLE_ERROR", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:INVALID_OPCODE", "FINNOS:EXCEPTION:UNHANDLED", "FINNOS:KERNEL:PANIC"):
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    fault = "FINNOS:EXCEPTION:PAGE_FAULT"
    fault_begin = "FINNOS:TEST:PAGE_TABLES:PAGE_FAULT_BEGIN"
    fault_pass = "FINNOS:TEST:PAGE_TABLES:PAGE_FAULT_PASS"
    fault_positions = [index for index in (output.find(fault), output.find(fault_pass)) if index >= 0]
    if output.count(fault) != 1:
        errors.append("expected exactly one page fault handler marker")
    if output.count(fault_pass) != 1:
        errors.append("expected exactly one page fault pass marker")
    if fault in output and fault_pass not in output:
        errors.append("unexpected page fault")
    if fault in output and output.find(fault_begin) > output.find(fault):
        errors.append("page fault occurred before page-fault test began")
    if fault_pass in output and fault not in output:
        errors.append("page fault pass marker has no handler marker")
    if len(fault_positions) == 2 and fault_positions != sorted(fault_positions):
        errors.append("page fault handler markers are out of order")
    if status != 33 and fault in output:
        errors.append("page fault handler did not produce success status")
    return errors

HEAP_MARKERS = MARKERS + (
    "FINNOS:TEST:HEAP:BEGIN", "FINNOS:TEST:HEAP:ALIGNMENT_OK",
    "FINNOS:TEST:HEAP:BOX_OK", "FINNOS:TEST:HEAP:VEC_OK",
    "FINNOS:TEST:HEAP:STRING_OK", "FINNOS:TEST:HEAP:FRAGMENTATION_OK",
    "FINNOS:TEST:HEAP:EXHAUSTION_OK", "FINNOS:TEST:HEAP:REUSE_OK",
    "FINNOS:TEST:HEAP:STATS_OK", "FINNOS:TEST:HEAP:INVARIANTS_OK",
    "FINNOS:TEST:HEAP:PASS",
)

TIMER_MARKERS = MARKERS + (
    "FINNOS:TEST:TIMER_INTERRUPTS:BEGIN", "FINNOS:TEST:TIMER_INTERRUPTS:PIC_MASK_OK", "FINNOS:TEST:TIMER_INTERRUPTS:APIC_MODE_OK", "FINNOS:TEST:TIMER_INTERRUPTS:IDT_GATES_OK", "FINNOS:TEST:TIMER_INTERRUPTS:IF_ENABLED_OK", "FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_BEGIN",
    "FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_OK", "FINNOS:TEST:TIMER_INTERRUPTS:FREQUENCY_OK", "FINNOS:TEST:TIMER_INTERRUPTS:MONOTONIC_OK",
    "FINNOS:TEST:TIMER_INTERRUPTS:EOI_OK", "FINNOS:TEST:TIMER_INTERRUPTS:SPURIOUS_OK",
    "FINNOS:TEST:TIMER_INTERRUPTS:INTERRUPT_CONTEXT_OK", "FINNOS:TEST:TIMER_INTERRUPTS:HEAP_INTERRUPT_GUARD_OK",
    "FINNOS:TEST:TIMER_INTERRUPTS:PASS",
)

def validate_timer(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in TIMER_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing timer marker(s)")
    if positions != sorted(position for position in positions if position >= 0): errors.append("timer markers are out of order")
    if output.count("FINNOS:KERNEL:TIMER_READY") != 1: errors.append("expected exactly one TIMER_READY")
    if output.count("FINNOS:TEST:TIMER_INTERRUPTS:PASS") != 1: errors.append("expected exactly one timer PASS")
    if "FINNOS:INTERRUPTS:PIC_MASTER_MASK=0xff" not in output or "FINNOS:INTERRUPTS:PIC_SLAVE_MASK=0xff" not in output:
        errors.append("PIC masks were not verified as 0xff")
    for marker in ("FINNOS:APIC:PHYSICAL_BASE=", "FINNOS:APIC:VIRTUAL_BASE=0x0000300000000000", "FINNOS:APIC:ID=", "FINNOS:APIC:VERSION=", "FINNOS:TIMER:APIC_CALIBRATION_ELAPSED_COUNTS=", "FINNOS:TIMER:APIC_INITIAL_COUNT=", "FINNOS:TIMER:FREQUENCY_WINDOW_MS=50", "FINNOS:TIMER:FREQUENCY_WINDOW_TICKS=", "FINNOS:INTERRUPTS:CALL_ALIGNMENT=0"):
        if marker not in output: errors.append(f"missing numeric hardware evidence: {marker}")
    import re
    numeric = {key: int(value) for key, value in re.findall(r"FINNOS:TIMER:(FREQUENCY_HZ|TICK_MILLISECONDS|PIT_REFERENCE_COUNT|APIC_CALIBRATION_ELAPSED_COUNTS|APIC_INITIAL_COUNT|FREQUENCY_WINDOW_TICKS)=(\d+)", output)}
    if numeric.get("FREQUENCY_HZ") != 100: errors.append("frequency is not 100 Hz")
    if numeric.get("TICK_MILLISECONDS") != 10: errors.append("tick duration is not 10 ms")
    if numeric.get("PIT_REFERENCE_COUNT") not in (11931, 11932): errors.append("invalid PIT reference count")
    if numeric.get("APIC_INITIAL_COUNT", 0) == 0: errors.append("APIC initial count is zero")
    if numeric.get("APIC_CALIBRATION_ELAPSED_COUNTS", 0) == 0: errors.append("APIC calibration elapsed count is zero")
    if not 3 <= numeric.get("FREQUENCY_WINDOW_TICKS", 0) <= 7: errors.append("frequency window is outside 3..7 ticks")
    values = {key: int(value) for key, value in re.findall(r"FINNOS:TIMER:(TEST_START_TICKS|TEST_END_TICKS|TEST_ELAPSED_TICKS|TEST_UPTIME_MS)=(\d+)", output)}
    if values.get("TEST_END_TICKS", 0) <= values.get("TEST_START_TICKS", 0): errors.append("timer ticks did not increase")
    if values.get("TEST_ELAPSED_TICKS", 0) < 8: errors.append("fewer than eight elapsed ticks")
    if values.get("TEST_ELAPSED_TICKS", 0) != values.get("TEST_END_TICKS", 0) - values.get("TEST_START_TICKS", 0): errors.append("elapsed tick value is inconsistent")
    if values.get("TEST_UPTIME_MS", 0) != values.get("TEST_END_TICKS", 0) * 10: errors.append("uptime conversion is inconsistent")
    forbidden = ("FINNOS:KERNEL:INTERRUPT_ERROR", "FINNOS:KERNEL:TIMER_ERROR", "FINNOS:INTERRUPT:UNEXPECTED", "FINNOS:EXCEPTION:PAGE_FAULT", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:KERNEL:PANIC")
    for marker in forbidden:
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    return errors

def validate_heap(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33:
        errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in HEAP_MARKERS]
    if any(position < 0 for position in positions):
        errors.append("missing marker(s): " + ", ".join(marker for marker, position in zip(HEAP_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("heap markers are out of order")
    if output.count("FINNOS:KERNEL:HEAP_READY") != 1:
        errors.append("expected exactly one HEAP_READY marker")
    for marker in ("FINNOS:KERNEL:HEAP_ERROR", "FINNOS:KERNEL:HEAP_OOM", "FINNOS:EXCEPTION:PAGE_FAULT", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:EXCEPTION:UNHANDLED", "FINNOS:KERNEL:PANIC"):
        if marker in output:
            errors.append(f"forbidden marker found: {marker}")
    return errors

COOPERATIVE_TASK_MARKERS = MARKERS[:-2] + (
    "FINNOS:KERNEL:TASK_STACKS_READY", "FINNOS:KERNEL:SCHEDULER_READY",
    "FINNOS:KERNEL:FRAMEBUFFER_OK", "FINNOS:KERNEL:FIRST_BOOT_COMPLETE",
    "FINNOS:TEST:COOPERATIVE_TASKS:BEGIN", "FINNOS:TEST:COOPERATIVE_TASKS:BOOTSTRAP_OK",
    "FINNOS:TEST:COOPERATIVE_TASKS:STACKS_OK", "FINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_BEGIN",
    "FINNOS:TEST:COOPERATIVE_TASKS:ROUND_ROBIN_OK", "FINNOS:TEST:COOPERATIVE_TASKS:REGISTER_STATE_OK", "FINNOS:TEST:COOPERATIVE_TASKS:STACK_ISOLATION_OK",
    "FINNOS:TEST:COOPERATIVE_TASKS:TASK_EXIT_OK", "FINNOS:TEST:COOPERATIVE_TASKS:STACK_RECLAIM_OK",
    "FINNOS:TEST:COOPERATIVE_TASKS:SLOT_REUSE_OK", "FINNOS:TEST:COOPERATIVE_TASKS:IDLE_CONTEXT_OK", "FINNOS:TEST:COOPERATIVE_TASKS:TIMER_CONTINUITY_OK", "FINNOS:TEST:COOPERATIVE_TASKS:INVARIANTS_OK", "FINNOS:TEST:COOPERATIVE_TASKS:PASS",
)

PREEMPTION_CONTEXT_MARKERS = MARKERS + (
    "FINNOS:TEST:PREEMPTION_CONTEXT:BEGIN",
    "FINNOS:TEST:PREEMPTION_CONTEXT:FRAME_LAYOUT_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:SOFTWARE_INTERRUPT_BEGIN",
    "FINNOS:TEST:PREEMPTION_CONTEXT:SOFTWARE_INTERRUPT_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:ALL_GPRS_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:EXACT_RIP_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:EXACT_RSP_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:REAL_TIMER_BEGIN",
    "FINNOS:TEST:PREEMPTION_CONTEXT:REAL_TIMER_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:TASK_ATTRIBUTION_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:IDLE_ATTRIBUTION_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:REQUEST_DEFERRED_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:REQUEST_CONSUMED_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:NO_SWITCH_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:INVARIANTS_OK",
    "FINNOS:TEST:PREEMPTION_CONTEXT:PASS",
)
def validate_preemption_context(status: int, output: str) -> list[str]:
    import re
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in PREEMPTION_CONTEXT_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing preemption-context marker(s)")
    if positions != sorted(position for position in positions if position >= 0): errors.append("preemption-context markers are out of order")
    for marker in dict.fromkeys(PREEMPTION_CONTEXT_MARKERS):
        if output.count(marker) != 1: errors.append(f"expected exactly one marker: {marker}")
    numeric_keys = (
        "FRAME_SIZE", "SOFTWARE_FRAME", "SOFTWARE_RETURN_FRAME", "SOFTWARE_SAVED_RIP", "SOFTWARE_EXPECTED_RIP",
        "SOFTWARE_INTERRUPTED_RSP", "SOFTWARE_EXPECTED_RSP", "SOFTWARE_POST_RSP", "TIMER_FRAME", "TIMER_RETURN_FRAME",
        "TIMER_SAVED_RIP", "TIMER_LOOP_START", "TIMER_LOOP_END", "TIMER_INTERRUPTED_RSP", "TIMER_EXPECTED_RSP", "TIMER_POST_RSP", "IDLE_FRAME", "IDLE_INTERRUPTED_RSP",
        "BOOTSTRAP_SLOT", "BOOTSTRAP_GENERATION", "WORKER_SLOT", "WORKER_GENERATION", "IDLE_SLOT", "IDLE_GENERATION",
        "DEPTH_NESTED", "DEPTH_INNER_DROPPED", "DEPTH_OUTER_DROPPED", "REQUEST_WHILE_NESTED", "REQUEST_AFTER_INNER_DROP",
        "REQUEST_AFTER_OUTER_DROP", "REQUEST_TAKEN", "REQUEST_AFTER_TAKE", "TICK_DELTA", "DELIVERY_DELTA", "EOI_DELTA",
        "SWITCHES_BEFORE", "SWITCHES_AFTER", "CR3_BEFORE", "CR3_AFTER", "IF_ENABLED", "INTERRUPT_DEPTH", "FAULTED",
    )
    values: dict[str, int] = {}
    for key in numeric_keys:
        matches = re.findall(rf"(?m)^FINNOS:PREEMPT:{key}=((?:0x)?[0-9a-fA-F]+)$", output)
        if len(matches) != 1:
            errors.append(f"expected exactly one numeric field {key}")
        else:
            try: values[key] = int(matches[0], 0)
            except ValueError: errors.append(f"invalid numeric field {key}")
    if values.get("FRAME_SIZE") != 160: errors.append("frame size is not 160")
    if values.get("SOFTWARE_FRAME") != values.get("SOFTWARE_RETURN_FRAME"): errors.append("software frame pointer changed")
    if values.get("SOFTWARE_SAVED_RIP") != values.get("SOFTWARE_EXPECTED_RIP"): errors.append("software RIP mismatch")
    if not (values.get("SOFTWARE_INTERRUPTED_RSP") == values.get("SOFTWARE_EXPECTED_RSP") and values.get("SOFTWARE_POST_RSP") == values.get("SOFTWARE_EXPECTED_RSP", 0) + 24): errors.append("software RSP mismatch")
    if values.get("TIMER_FRAME") != values.get("TIMER_RETURN_FRAME"): errors.append("timer frame pointer changed")
    if not (values.get("TIMER_LOOP_START", 0) <= values.get("TIMER_SAVED_RIP", 0) < values.get("TIMER_LOOP_END", 0)): errors.append("timer RIP is outside spin loop")
    if not (values.get("TIMER_INTERRUPTED_RSP") == values.get("TIMER_EXPECTED_RSP") and values.get("TIMER_POST_RSP") == values.get("TIMER_EXPECTED_RSP", 0) + 24): errors.append("timer RSP mismatch")
    if values.get("BOOTSTRAP_SLOT") != 0 or values.get("BOOTSTRAP_GENERATION", 0) == 0: errors.append("invalid bootstrap identity")
    if values.get("WORKER_SLOT", 0) < 2 or values.get("WORKER_GENERATION", 0) == 0: errors.append("invalid worker identity")
    if values.get("IDLE_SLOT") != 1 or values.get("IDLE_GENERATION", 0) == 0: errors.append("invalid idle identity")
    if (values.get("DEPTH_NESTED"), values.get("DEPTH_INNER_DROPPED"), values.get("DEPTH_OUTER_DROPPED")) != (2, 1, 0): errors.append("invalid depth transition")
    if [values.get(key) for key in ("REQUEST_WHILE_NESTED", "REQUEST_AFTER_INNER_DROP", "REQUEST_AFTER_OUTER_DROP", "REQUEST_TAKEN", "REQUEST_AFTER_TAKE")] != [1, 1, 1, 1, 0]: errors.append("invalid request transition")
    if values.get("TICK_DELTA", 0) <= 0 or values.get("DELIVERY_DELTA", 0) <= 0 or values.get("EOI_DELTA") != values.get("DELIVERY_DELTA"): errors.append("invalid timer deltas")
    if values.get("SWITCHES_BEFORE") != values.get("SWITCHES_AFTER"): errors.append("context switch count changed")
    if values.get("CR3_BEFORE") != values.get("CR3_AFTER"): errors.append("CR3 changed")
    if values.get("IF_ENABLED") != 1 or values.get("INTERRUPT_DEPTH") != 0 or values.get("FAULTED") != 0: errors.append("interrupt safety state invalid")
    register_keys = tuple(f"{phase}_R{index}" for phase in ("SOFTWARE_SAVED", "SOFTWARE_POST", "TIMER_SAVED", "TIMER_POST") for index in range(15))
    for key in register_keys:
        matches = re.findall(rf"(?m)^FINNOS:PREEMPT:{key}=((?:0x)?[0-9a-fA-F]+)$", output)
        if len(matches) != 1: errors.append(f"expected exactly one register field {key}")
    patterns = [0x1111111111111111, 0x2222222222222222, 0x3333333333333333, 0x4444444444444444, 0x5555555555555555, 0x6666666666666666, 0x7777777777777777, 0x8888888888888888, 0x9999999999999999, 0xaaaaaaaaaaaaaaaa, 0xbbbbbbbbbbbbbbbb, 0xcccccccccccccccc, 0xdddddddddddddddd, 0xeeeeeeeeeeeeeeee, 0xffffffffffffffff]
    for phase in ("SOFTWARE_SAVED", "SOFTWARE_POST", "TIMER_SAVED", "TIMER_POST"):
        for index, expected in enumerate(patterns):
            match = re.findall(rf"(?m)^FINNOS:PREEMPT:{phase}_R{index}=((?:0x)?[0-9a-fA-F]+)$", output)
            if match and int(match[0], 0) != expected: errors.append(f"wrong register field {phase}_R{index}")
    for marker in ("FINNOS:KERNEL:PANIC", "FINNOS:INTERRUPT:UNEXPECTED", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:EXCEPTION:UNHANDLED", "FINNOS:KERNEL:PREEMPTION_ERROR", "FINNOS:KERNEL:INTERRUPT_FRAME_ERROR", "FINNOS:KERNEL:TASK_ATTRIBUTION_ERROR"):
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    return errors

def validate_cooperative_tasks(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 33: errors.append(f"expected QEMU status 33, got {status}")
    positions = [output.find(marker) for marker in COOPERATIVE_TASK_MARKERS]
    if any(position < 0 for position in positions): errors.append("missing cooperative-task marker(s): " + ", ".join(marker for marker, position in zip(COOPERATIVE_TASK_MARKERS, positions) if position < 0))
    if positions != sorted(position for position in positions if position >= 0): errors.append("cooperative-task markers are out of order")
    for marker in COOPERATIVE_TASK_MARKERS:
        if output.count(marker) != 1: errors.append(f"expected exactly one cooperative-task marker: {marker}")
    import re
    events = [(int(index), int(value)) for index, value in re.findall(r"FINNOS:TASKS:EVENT_(\d+)=(\d+)", output)]
    if events != list(enumerate((11, 21, 31, 12, 22, 32, 13, 23, 33))): errors.append("worker event order is not A1/B1/C1/A2/B2/C2/A3/B3/C3")
    if "FINNOS:TASKS:EVENT_COUNT=9" not in output: errors.append("worker event count is not nine")
    numeric_keys = (
        "A_STACK_START", "A_STACK_END", "A_SENTINEL", "B_STACK_START", "B_STACK_END", "B_SENTINEL",
        "C_STACK_START", "C_STACK_END", "C_SENTINEL", "IDLE_STACK_START", "IDLE_STACK_END", "IDLE_RSP",
        "COMPLETED_DELTA", "EXITED_BEFORE_REAP", "QUEUE_LENGTH_BEFORE_REAP", "PHYSICAL_FREE_BASELINE",
        "PHYSICAL_FREE_AFTER_REAP", "MAPPED_BASELINE", "MAPPED_AFTER_REAP", "VACANT_BASELINE",
        "VACANT_AFTER_REAP", "REAPED_DELTA", "REUSED_SLOT", "OLD_GENERATION", "NEW_GENERATION",
        "STALE_ID_REJECTED", "REUSE_RUNS", "IDLE_TICK_DELTA", "TIMER_START_TICKS", "TIMER_END_TICKS",
        "TICK_DELTA", "DELIVERY_DELTA", "EOI_DELTA", "CR3_BEFORE", "CR3_AFTER", "SCHEDULER_ISR_ENTRIES",
    )
    values: dict[str, int] = {}
    for key in numeric_keys:
        matches = re.findall(rf"FINNOS:TASKS:{key}=(0x[0-9a-fA-F]+|\d+)", output)
        if len(matches) != 1:
            errors.append(f"expected exactly one numeric field {key}")
        else:
            try:
                values[key] = int(matches[0], 0)
                if values[key] > (1 << 64) - 1: errors.append(f"numeric field {key} exceeds u64")
            except ValueError: errors.append(f"invalid numeric field {key}")
    for name in ("A", "B", "C"):
        if not values.get(f"{name}_STACK_START", 0) <= values.get(f"{name}_SENTINEL", 0) < values.get(f"{name}_STACK_END", 0): errors.append(f"{name} sentinel is outside its stack")
    ranges = [(values.get(f"{name}_STACK_START", 0), values.get(f"{name}_STACK_END", 0)) for name in ("A", "B", "C")]
    if any(start >= end for start, end in ranges): errors.append("worker stack range is empty or reversed")
    if any(a_start < b_end and b_start < a_end for index, (a_start, a_end) in enumerate(ranges) for b_start, b_end in ranges[index + 1:]): errors.append("worker stack ranges overlap")
    idle_range = (values.get("IDLE_STACK_START", 0), values.get("IDLE_STACK_END", 0))
    if idle_range[0] >= idle_range[1]: errors.append("idle stack range is empty or reversed")
    if any(start < idle_range[1] and idle_range[0] < end for start, end in ranges): errors.append("idle stack overlaps a worker stack")
    if not idle_range[0] <= values.get("IDLE_RSP", 0) < idle_range[1]: errors.append("idle RSP is outside idle stack")
    expected = {"COMPLETED_DELTA": 4, "EXITED_BEFORE_REAP": 4, "QUEUE_LENGTH_BEFORE_REAP": 0, "REAPED_DELTA": 4, "REUSED_SLOT": 2, "STALE_ID_REJECTED": 1, "REUSE_RUNS": 1, "SCHEDULER_ISR_ENTRIES": 0}
    for key, value in expected.items():
        if values.get(key) != value: errors.append(f"unexpected {key}")
    for before, after, label in (("PHYSICAL_FREE_BASELINE", "PHYSICAL_FREE_AFTER_REAP", "physical pages"), ("MAPPED_BASELINE", "MAPPED_AFTER_REAP", "mapped leaves"), ("VACANT_BASELINE", "VACANT_AFTER_REAP", "vacant slots")):
        if values.get(before) != values.get(after): errors.append(f"{label} baseline was not restored")
    if values.get("NEW_GENERATION") != values.get("OLD_GENERATION", 0) + 1: errors.append("task slot generation did not advance exactly once")
    if values.get("TIMER_END_TICKS", 0) <= values.get("TIMER_START_TICKS", 0) or values.get("TICK_DELTA", 0) <= 0: errors.append("timer ticks did not advance across task switches")
    if values.get("TICK_DELTA") != values.get("TIMER_END_TICKS", 0) - values.get("TIMER_START_TICKS", 0): errors.append("timer tick delta is inconsistent")
    if values.get("DELIVERY_DELTA", 0) <= 0 or values.get("EOI_DELTA") != values.get("DELIVERY_DELTA"): errors.append("timer delivery and EOI deltas are inconsistent")
    if values.get("IDLE_TICK_DELTA", 0) <= 0: errors.append("idle did not observe a timer tick")
    if values.get("CR3_BEFORE") != values.get("CR3_AFTER"): errors.append("CR3 changed across cooperative scheduling")
    for marker in ("FINNOS:KERNEL:SCHEDULER_ERROR", "FINNOS:KERNEL:TASK_STACK_ERROR", "FINNOS:TASK:CONTEXT_ERROR", "FINNOS:INTERRUPT:UNEXPECTED", "FINNOS:EXCEPTION:GENERAL_PROTECTION", "FINNOS:EXCEPTION:DOUBLE_FAULT", "FINNOS:EXCEPTION:UNHANDLED", "FINNOS:KERNEL:PANIC"):
        if marker in output: errors.append(f"forbidden marker found: {marker}")
    return errors

def qemu_command(qemu: str, firmware: str, image: Path, headless: bool = False, test_exit: bool = False) -> list[str]:
    # Homebrew's code-only OVMF image is a pflash image; using -bios makes
    # QEMU 11 reject it before the guest starts.
    command = [qemu, "-machine", "q35", "-m", "256M", "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}", "-drive", f"if=ide,format=raw,file={image}", "-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none"]
    if headless: command.extend(["-display", "none"])
    if test_exit: command.extend(["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"])
    return command
