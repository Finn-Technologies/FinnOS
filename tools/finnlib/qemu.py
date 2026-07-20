"""QEMU command and smoke-log interpretation."""
from __future__ import annotations

from pathlib import Path
import re

ARM64_MARKERS = (
    "FINNOS:BOOTLOADER:START",
    "FINNOS:BOOTLOADER:KERNEL_FOUND",
    "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED",
    "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ARM64_ENTRY",
    "FINNOS:KERNEL:ARM64_SERIAL_READY",
)

ARM64_EXCEPTION_MARKERS = (
    "FINNOS:BOOTLOADER:START",
    "FINNOS:BOOTLOADER:KERNEL_FOUND",
    "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED",
    "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ARM64_ENTRY",
    "FINNOS:KERNEL:ARM64_CURRENT_EL=1",
    "FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY",
    "FINNOS:KERNEL:ARM64_SERIAL_READY",
    "FINNOS:TEST:ARM64_EXCEPTIONS:BEGIN",
    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_BEGIN",
    "FINNOS:EXCEPTION:ARM64_BREAKPOINT",
    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS",
    "FINNOS:TEST:ARM64_EXCEPTIONS:PASS",
)

ARM64_MEMORY_MAP_MARKERS = (
    "FINNOS:BOOTLOADER:START",
    "FINNOS:BOOTLOADER:KERNEL_FOUND",
    "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED",
    "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ARM64_ENTRY",
    "FINNOS:KERNEL:ARM64_CURRENT_EL=1",
    "FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY",
    "FINNOS:TEST:ARM64_MEMORY_MAP:BEGIN",
    "FINNOS:KERNEL:BOOTINFO_OK",
    "FINNOS:KERNEL:MEMORY_MAP_OK",
    "FINNOS:KERNEL:MEMORY_MAP_PARSED",
    "FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED",
    "FINNOS:KERNEL:MEMORY_MAP_TABLE_VALID",
    "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK",
    "FINNOS:TEST:ARM64_MEMORY_MAP:PROTECTED_OK",
    "FINNOS:TEST:ARM64_MEMORY_MAP:FREE_OK",
    "FINNOS:TEST:ARM64_MEMORY_MAP:INVARIANTS_OK",
    "FINNOS:TEST:ARM64_MEMORY_MAP:PASS",
    "FINNOS:KERNEL:ARM64_SERIAL_READY",
)

ARM64_PAGE_TABLE_MARKERS = (
    "FINNOS:BOOTLOADER:START",
    "FINNOS:BOOTLOADER:KERNEL_FOUND",
    "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED",
    "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ARM64_ENTRY",
    "FINNOS:KERNEL:ARM64_CURRENT_EL=1",
    "FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY",
    "FINNOS:KERNEL:BOOTINFO_OK",
    "FINNOS:KERNEL:MEMORY_MAP_OK",
    "FINNOS:KERNEL:MEMORY_MAP_PARSED",
    "FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED",
    "FINNOS:KERNEL:MEMORY_MAP_TABLE_VALID",
    "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:TEST:ARM64_PAGE_TABLES:BEGIN",
    "FINNOS:KERNEL:PAGE_TABLES_BUILT",
    "FINNOS:KERNEL:PAGE_TABLES_ACTIVATING",
    "FINNOS:KERNEL:PAGE_TABLES_ACTIVE",
    "FINNOS:KERNEL:ADDRESS_SPACE_VALIDATED",
    "FINNOS:TEST:ARM64_PAGE_TABLES:PERMISSIONS_OK",
    "FINNOS:TEST:ARM64_PAGE_TABLES:NULL_UNMAPPED",
    "FINNOS:TEST:ARM64_PAGE_TABLES:GUARDS_UNMAPPED",
    "FINNOS:TEST:ARM64_PAGE_TABLES:UART_DEVICE_OK",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:BEGIN",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_BEGIN",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_PASS",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:LOW_GUARD_READ_BEGIN",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:LOW_GUARD_READ_PASS",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:TEXT_WRITE_BEGIN",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:TEXT_WRITE_PASS",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:DATA_EXECUTE_BEGIN",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:DATA_EXECUTE_PASS",
    "FINNOS:TEST:ARM64_PAGE_FAULTS:PASS",
    "FINNOS:TEST:ARM64_PAGE_TABLES:PASS",
    "FINNOS:KERNEL:ARM64_SERIAL_READY",
)

ARM64_GIC_MARKERS = ARM64_MARKERS + (
    "FINNOS:TEST:ARM64_GIC:BEGIN",
    "FINNOS:TEST:ARM64_GIC:SPURIOUS_BEFORE_OK",
    "FINNOS:TEST:ARM64_GIC:SGI_BEGIN",
    "FINNOS:TEST:ARM64_GIC:SGI_DELIVERED",
    "FINNOS:TEST:ARM64_GIC:EOI_OK",
    "FINNOS:TEST:ARM64_GIC:FRAME_OK",
    "FINNOS:TEST:ARM64_GIC:SPURIOUS_AFTER_OK",
    "FINNOS:TEST:ARM64_GIC:PASS",
)

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

def validate_arm64_smoke(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 0:
        errors.append(f"expected ARM64 semihosting status 0, got {status}")
    positions = [output.find(marker) for marker in ARM64_MARKERS]
    if any(position < 0 for position in positions):
        errors.append(
            "missing ARM64 marker(s): "
            + ", ".join(
                marker for marker, position in zip(ARM64_MARKERS, positions) if position < 0
            )
        )
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 boot markers are out of order")
    for marker in ARM64_MARKERS:
        if output.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 marker: {marker}")
    for marker in (
        "FINNOS:BOOTLOADER:ERROR:",
        "FINNOS:KERNEL:PANIC",
        "FINNOS:TEST:ARM64_EXCEPTIONS:",
        "FINNOS:TEST:ARM64_EXCEPTION_FATAL:",
        "FINNOS:EXCEPTION:ARM64_BREAKPOINT",
        "FINNOS:TEST:ARM64_PAGE_TABLES:",
        "FINNOS:TEST:ARM64_PAGE_FAULTS:",
        "FINNOS:EXCEPTION:ARM64_PAGE_FAULT",
        "FINNOS:TEST:ARM64_GIC:",
    ):
        if marker in output:
            errors.append(f"forbidden marker found: {marker}")
    return errors

def validate_arm64_memory_map(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 0:
        errors.append(f"expected ARM64 semihosting status 0, got {status}")
    lines = tuple(output.splitlines())
    positions = [lines.index(marker) if marker in lines else -1 for marker in ARM64_MEMORY_MAP_MARKERS]
    if any(position < 0 for position in positions):
        errors.append("missing ARM64 memory-map marker(s)")
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 memory-map markers are out of order")
    for marker in ARM64_MEMORY_MAP_MARKERS:
        if lines.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 memory-map marker: {marker}")

    names = (
        "DESCRIPTORS", "REGIONS", "USABLE_BYTES", "RESERVED_BYTES",
        "KERNEL_BYTES", "BOOT_INFO_BYTES", "MEMORY_MAP_STORAGE_BYTES",
        "FRAMEBUFFER_BYTES", "TOTAL_PAGES", "FREE_PAGES",
        "ALLOCATED_PAGES", "MANAGED_EXTENTS", "FREE_EXTENTS",
        "TEST_ALLOCATED_PAGE",
    )
    values: dict[str, int] = {}
    for name in names:
        matches = re.findall(rf"^FINNOS:MEMORY:{name}=0x([0-9a-f]{{16}})$", output, re.MULTILINE)
        if len(matches) != 1:
            errors.append(f"expected exactly one fixed-width numeric field: {name}")
        else:
            values[name] = int(matches[0], 16)
    for name in (
        "DESCRIPTORS", "REGIONS", "USABLE_BYTES", "KERNEL_BYTES",
        "BOOT_INFO_BYTES", "MEMORY_MAP_STORAGE_BYTES", "TOTAL_PAGES",
        "FREE_PAGES", "MANAGED_EXTENTS", "FREE_EXTENTS",
        "TEST_ALLOCATED_PAGE",
    ):
        if values.get(name, 0) <= 0:
            errors.append(f"expected positive ARM64 memory value: {name}")
    if values.get("ALLOCATED_PAGES") != 0:
        errors.append("initial ARM64 allocator must have zero allocated pages")
    if values.get("TOTAL_PAGES") != values.get("FREE_PAGES"):
        errors.append("initial ARM64 total/free page counts differ")
    if values.get("MANAGED_EXTENTS") != values.get("FREE_EXTENTS"):
        errors.append("initial ARM64 managed/free extent counts differ")
    for marker in (
        "FINNOS:BOOTLOADER:ERROR:", "FINNOS:KERNEL:PANIC",
        "FINNOS:KERNEL:MEMORY_MAP_ERROR", "FINNOS:KERNEL:PAGE_ALLOCATOR_ERROR",
        "FINNOS:TEST:ARM64_EXCEPTIONS:", "FINNOS:TEST:ARM64_EXCEPTION_FATAL:",
        "FINNOS:TEST:ARM64_GIC:",
        "FINNOS:EXCEPTION:ARM64_",
    ):
        if marker in output:
            errors.append(f"forbidden marker found: {marker}")
    return errors

def validate_arm64_page_tables(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 0:
        errors.append(f"expected ARM64 semihosting status 0, got {status}")
    lines = tuple(output.splitlines())
    positions = [lines.index(marker) if marker in lines else -1 for marker in ARM64_PAGE_TABLE_MARKERS]
    if any(position < 0 for position in positions):
        errors.append("missing ARM64 page-table marker(s)")
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 page-table markers are out of order")
    for marker in ARM64_PAGE_TABLE_MARKERS:
        if lines.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 page-table marker: {marker}")
    fault_cases = (
        "NULL_READ",
        "LOW_GUARD_READ",
        "TEXT_WRITE",
        "DATA_EXECUTE",
    )
    fault_marker = "FINNOS:EXCEPTION:ARM64_PAGE_FAULT"
    if lines.count(fault_marker) != len(fault_cases):
        errors.append("expected exactly four armed ARM64 page faults")
    for name in fault_cases:
        begin_marker = f"FINNOS:TEST:ARM64_PAGE_FAULTS:{name}_BEGIN"
        pass_marker = f"FINNOS:TEST:ARM64_PAGE_FAULTS:{name}_PASS"
        begin = lines.index(begin_marker) if begin_marker in lines else -1
        passed = lines.index(pass_marker) if pass_marker in lines else -1
        observed = next(
            (index for index in range(begin + 1, len(lines)) if lines[index] == fault_marker),
            -1,
        )
        if begin < 0 or passed < 0 or not begin < observed < passed:
            errors.append(f"ARM64 {name} fault evidence is out of order")

    names = (
        "ROOT", "TTBR0", "TTBR1", "TCR", "MAIR", "SCTLR",
        "TABLE_PAGES_RESERVED", "TABLE_PAGES_USED", "MAPPED_PAGES",
    )
    values: dict[str, int] = {}
    for name in names:
        matches = re.findall(
            rf"^FINNOS:PAGING:{name}=0x([0-9a-f]{{16}})$",
            output,
            re.MULTILINE,
        )
        if len(matches) != 1:
            errors.append(f"expected exactly one ARM64 paging field: {name}")
        else:
            values[name] = int(matches[0], 16)
    for name in (name for name in names if name != "TTBR1"):
        if values.get(name, 0) <= 0:
            errors.append(f"expected positive ARM64 paging value: {name}")
    if values.get("ROOT") != values.get("TTBR0"):
        errors.append("ARM64 paging root does not match TTBR0")
    if values.get("TTBR1") != 0:
        errors.append("ARM64 TTBR1 is not disabled")
    if values.get("MAIR") != 0x0044_00FF:
        errors.append("ARM64 MAIR does not match the owned attribute regime")
    tcr_required_mask = (
        0x3F | (0b11 << 8) | (0b11 << 10) | (0b11 << 12)
        | (0x3F << 16) | (1 << 23) | (0b11 << 30)
    )
    tcr_required_value = (
        16 | (0b01 << 8) | (0b01 << 10) | (0b11 << 12)
        | (16 << 16) | (1 << 23) | (0b10 << 30)
    )
    if values.get("TCR", 0) & tcr_required_mask != tcr_required_value:
        errors.append("ARM64 TCR does not match the required 4 KiB TTBR0 regime")
    sctlr_required_bits = (1 << 0) | (1 << 2) | (1 << 3) | (1 << 12) | (1 << 19)
    if values.get("SCTLR", 0) & sctlr_required_bits != sctlr_required_bits:
        errors.append("ARM64 SCTLR is missing required MMU, cache, alignment, or WXN bits")
    if values.get("TABLE_PAGES_USED", 0) > values.get("TABLE_PAGES_RESERVED", 0):
        errors.append("ARM64 used table pages exceed the reserved table pool")
    if values.get("TTBR0", 0) & 0xfff:
        errors.append("ARM64 TTBR0 is not page aligned")
    forbidden_prefixes = (
        "FINNOS:BOOTLOADER:ERROR:",
        "FINNOS:KERNEL:PANIC",
        "FINNOS:KERNEL:PAGING_ERROR",
        "FINNOS:TEST:ARM64_EXCEPTIONS:",
        "FINNOS:TEST:ARM64_EXCEPTION_FATAL:",
        "FINNOS:TEST:ARM64_MEMORY_MAP:",
        "FINNOS:TEST:ARM64_GIC:",
    )
    for line in lines:
        if any(line.startswith(prefix) for prefix in forbidden_prefixes):
            errors.append(f"forbidden marker found: {line}")
        if line.startswith("FINNOS:EXCEPTION:ARM64_") and line != fault_marker:
            errors.append(f"forbidden marker found: {line}")
    return errors

def validate_arm64_gic(status: int, output: str) -> list[str]:
    errors: list[str] = []
    lines = tuple(output.splitlines())
    if status != 0:
        errors.append(f"expected ARM64 GIC semihosting status 0, got {status}")

    positions = [lines.index(marker) if marker in lines else -1 for marker in ARM64_GIC_MARKERS]
    if any(position < 0 for position in positions):
        errors.append("missing ARM64 GIC lifecycle marker(s)")
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 GIC lifecycle markers are out of order")
    for marker in ARM64_GIC_MARKERS:
        if lines.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 GIC marker: {marker}")

    names = (
        "DISTRIBUTOR_BASE",
        "CPU_INTERFACE_BASE",
        "TYPER",
        "IIDR",
        "IAR_RAW",
        "INTID",
        "DELIVERY_DELTA",
        "EOI_DELTA",
        "SPURIOUS_BEFORE",
        "SPURIOUS_AFTER",
        "INTERRUPT_DEPTH",
        "FRAME_SENTINEL",
        "DAIF_BEFORE",
        "IRQ_SPSR",
        "DAIF_AFTER",
    )
    values: dict[str, int] = {}
    for name in names:
        matches = re.findall(
            rf"^FINNOS:GIC:{name}=0x([0-9a-f]{{16}})$",
            output,
            re.MULTILINE,
        )
        if len(matches) != 1:
            errors.append(f"expected exactly one fixed-width ARM64 GIC field: {name}")
        else:
            values[name] = int(matches[0], 16)

    if values.get("DISTRIBUTOR_BASE") != 0x0800_0000:
        errors.append("ARM64 GIC distributor base does not match the pinned QEMU profile")
    if values.get("CPU_INTERFACE_BASE") != 0x0801_0000:
        errors.append("ARM64 GIC CPU-interface base does not match the pinned QEMU profile")
    for name in ("TYPER", "IIDR"):
        if values.get(name, 0) == 0:
            errors.append(f"expected nonzero ARM64 GIC identity field: {name}")
    if values.get("IAR_RAW", 0) & 0x3ff != 1 or values.get("INTID") != 1:
        errors.append("ARM64 GIC acknowledge token does not identify the armed SGI")
    if values.get("DELIVERY_DELTA") != 1:
        errors.append("ARM64 GIC delivery delta is not exactly one")
    if values.get("EOI_DELTA") != values.get("DELIVERY_DELTA"):
        errors.append("ARM64 GIC EOI count does not match delivery count")
    for name in ("SPURIOUS_BEFORE", "SPURIOUS_AFTER"):
        if values.get(name) != 1023:
            errors.append(f"ARM64 GIC {name.lower()} is not the no-pending special ID")
    if values.get("INTERRUPT_DEPTH") != 0:
        errors.append("ARM64 interrupt depth was not restored after ERET")
    if values.get("FRAME_SENTINEL") != 1:
        errors.append("ARM64 IRQ frame sentinel was not preserved")
    if values.get("DAIF_BEFORE", 0) & 0x3c0 != 0x3c0 or values.get("DAIF_AFTER", 0) & 0x3c0 != 0x3c0:
        errors.append("ARM64 IRQ mask was not set around the isolated delivery window")
    if values.get("IRQ_SPSR", 0) & 0x3c0 != 0x340:
        errors.append("ARM64 IRQ origin did not preserve D/A/F masking with I unmasked")

    forbidden_prefixes = (
        "FINNOS:BOOTLOADER:ERROR:",
        "FINNOS:KERNEL:PANIC",
        "FINNOS:KERNEL:GIC_ERROR",
        "FINNOS:INTERRUPT:ARM64_GIC_ERROR",
        "FINNOS:TEST:ARM64_EXCEPTIONS:",
        "FINNOS:TEST:ARM64_EXCEPTION_FATAL:",
        "FINNOS:TEST:ARM64_MEMORY_MAP:",
        "FINNOS:TEST:ARM64_PAGE_TABLES:",
        "FINNOS:TEST:ARM64_PAGE_FAULTS:",
        "FINNOS:EXCEPTION:ARM64_",
    )
    for line in lines:
        if any(line.startswith(prefix) for prefix in forbidden_prefixes):
            errors.append(f"forbidden marker found: {line}")
    return errors

def validate_arm64_exceptions(status: int, output: str) -> list[str]:
    errors: list[str] = []
    if status != 0:
        errors.append(f"expected ARM64 semihosting status 0, got {status}")
    lines = tuple(output.splitlines())
    positions = [lines.index(marker) if marker in lines else -1 for marker in ARM64_EXCEPTION_MARKERS]
    if any(position < 0 for position in positions):
        errors.append(
            "missing ARM64 exception marker(s): "
            + ", ".join(
                marker
                for marker, position in zip(ARM64_EXCEPTION_MARKERS, positions)
                if position < 0
            )
        )
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 exception markers are out of order")
    for marker in ARM64_EXCEPTION_MARKERS:
        if lines.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 exception marker: {marker}")
    for marker in (
        "FINNOS:BOOTLOADER:ERROR:",
        "FINNOS:KERNEL:PANIC",
        "FINNOS:EXCEPTION:ARM64_FATAL",
        "FINNOS:EXCEPTION:ARM64_TEST_STATE_ERROR",
        "FINNOS:EXCEPTION:ARM64_TEST_NOT_OBSERVED",
        "FINNOS:EXCEPTION:ARM64_ELR_OVERFLOW",
        "FINNOS:TEST:ARM64_GIC:",
    ):
        if marker in output:
            errors.append(f"forbidden marker found: {marker}")
    for line in lines:
        if line.startswith("FINNOS:EXCEPTION:ARM64_") and line != "FINNOS:EXCEPTION:ARM64_BREAKPOINT":
            errors.append(f"unexpected ARM64 exception marker found: {line}")
    return errors

def validate_arm64_exception_fatal(status: int, output: str) -> list[str]:
    errors: list[str] = []
    static_before = ARM64_MARKERS[:-1] + (
        "FINNOS:KERNEL:ARM64_CURRENT_EL=1",
        "FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY",
        "FINNOS:KERNEL:ARM64_SERIAL_READY",
        "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
    )
    numeric_names = ("SOURCE", "ESR", "ELR", "FAR", "SPSR", "X0")
    lines = tuple(output.splitlines())
    if status != 1:
        errors.append(f"expected ARM64 fatal semihosting status 1, got {status}")
    numeric_lines: list[str] = []
    values: dict[str, int] = {}
    for name in numeric_names:
        matches = re.findall(
            rf"^FINNOS:EXCEPTION:ARM64_{name}=0x([0-9a-f]{{16}})$",
            output,
            re.MULTILINE,
        )
        if len(matches) != 1:
            errors.append(f"expected exactly one fixed-width ARM64 fatal field: {name}")
            numeric_lines.append(f"<missing:{name}>")
        else:
            values[name] = int(matches[0], 16)
            numeric_lines.append(f"FINNOS:EXCEPTION:ARM64_{name}=0x{matches[0]}")
    required = static_before + tuple(numeric_lines) + ("FINNOS:EXCEPTION:ARM64_FATAL",)
    positions = [lines.index(marker) if marker in lines else -1 for marker in required]
    if any(position < 0 for position in positions):
        errors.append("missing ARM64 fatal marker(s)")
    if positions != sorted(position for position in positions if position >= 0):
        errors.append("ARM64 fatal markers are out of order")
    for marker in required:
        if lines.count(marker) != 1:
            errors.append(f"expected exactly one ARM64 fatal marker: {marker}")
    if values.get("SOURCE") != 4:
        errors.append("ARM64 fatal source must be current-EL SPx synchronous source 4")
    if values.get("ESR") != 0x00000000F200F101:
        errors.append("ARM64 fatal ESR must encode the armed BRK #0xf101 syndrome")
    if values.get("ELR", 0) == 0:
        errors.append("ARM64 fatal ELR must be nonzero")
    if values.get("SPSR", 0) & 0x3C0 != 0x3C0:
        errors.append("ARM64 fatal SPSR must preserve DAIF masking")
    for marker in (
        "FINNOS:BOOTLOADER:ERROR:",
        "FINNOS:KERNEL:PANIC",
        "FINNOS:EXCEPTION:ARM64_BREAKPOINT",
        "FINNOS:TEST:ARM64_EXCEPTIONS:PASS",
        "FINNOS:TEST:ARM64_GIC:",
    ):
        if marker in output:
            errors.append(f"forbidden marker found: {marker}")
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

def qemu_command(
    qemu: str,
    firmware: str,
    image: Path,
    headless: bool = False,
    test_exit: bool = False,
    machine: str = "q35",
    architecture: str = "x86_64",
    cpu: str = "",
) -> list[str]:
    # Homebrew's code-only OVMF image is a pflash image; using -bios makes
    # QEMU 11 reject it before the guest starts.
    command = [qemu, "-machine", machine]
    if cpu:
        command.extend(["-cpu", cpu])
    command.extend([
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}",
    ])
    if architecture == "arm64":
        command.extend([
            "-smp", "1",
            "-drive", f"if=none,format=raw,file={image},id=finnos-esp",
            "-device", "virtio-blk-pci,drive=finnos-esp",
        ])
    else:
        command.extend(["-drive", f"if=ide,format=raw,file={image}"])
    command.extend(["-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none"])
    if headless: command.extend(["-display", "none"])
    if test_exit and architecture == "arm64":
        command.extend(["-semihosting-config", "enable=on,target=native"])
    elif test_exit:
        command.extend(["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"])
    return command
