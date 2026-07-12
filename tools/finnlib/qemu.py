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
    "FINNOS:TEST:TIMER_INTERRUPTS:BEGIN", "FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_BEGIN",
    "FINNOS:TEST:TIMER_INTERRUPTS:REAL_TICKS_OK", "FINNOS:TEST:TIMER_INTERRUPTS:MONOTONIC_OK",
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
    for marker in ("FINNOS:APIC:PHYSICAL_BASE=", "FINNOS:APIC:VIRTUAL_BASE=0x0000300000000000", "FINNOS:APIC:ID=", "FINNOS:APIC:VERSION=", "FINNOS:TIMER:APIC_COUNTS_PER_TICK="):
        if marker not in output: errors.append(f"missing numeric hardware evidence: {marker}")
    import re
    values = {key: int(value) for key, value in re.findall(r"FINNOS:TIMER:(TEST_START_TICKS|TEST_END_TICKS|TEST_ELAPSED_TICKS|TEST_UPTIME_MS)=(\d+)", output)}
    if values.get("TEST_END_TICKS", 0) <= values.get("TEST_START_TICKS", 0): errors.append("timer ticks did not increase")
    if values.get("TEST_ELAPSED_TICKS", 0) < 8: errors.append("fewer than eight elapsed ticks")
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

def qemu_command(qemu: str, firmware: str, image: Path, headless: bool = False, test_exit: bool = False) -> list[str]:
    # Homebrew's code-only OVMF image is a pflash image; using -bios makes
    # QEMU 11 reject it before the guest starts.
    command = [qemu, "-machine", "q35", "-m", "256M", "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}", "-drive", f"if=ide,format=raw,file={image}", "-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none"]
    if headless: command.extend(["-display", "none"])
    if test_exit: command.extend(["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"])
    return command
