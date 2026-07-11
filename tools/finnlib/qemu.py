"""QEMU command and smoke-log interpretation."""
from __future__ import annotations

MARKERS = (
    "FINNOS:BOOTLOADER:START", "FINNOS:BOOTLOADER:KERNEL_FOUND", "FINNOS:BOOTLOADER:KERNEL_VALID",
    "FINNOS:BOOTLOADER:KERNEL_LOADED", "FINNOS:BOOTLOADER:FRAMEBUFFER_READY", "FINNOS:BOOTLOADER:EXIT_BOOT_SERVICES",
    "FINNOS:KERNEL:ENTRY", "FINNOS:KERNEL:GDT_OK", "FINNOS:KERNEL:TSS_OK", "FINNOS:KERNEL:IDT_OK",
    "FINNOS:KERNEL:EXCEPTIONS_READY", "FINNOS:KERNEL:BOOTINFO_OK", "FINNOS:KERNEL:MEMORY_MAP_OK",
    "FINNOS:KERNEL:MEMORY_MAP_PARSED", "FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED", "FINNOS:KERNEL:PAGE_ALLOCATOR_READY",
    "FINNOS:KERNEL:FRAMEBUFFER_OK", "FINNOS:KERNEL:FIRST_BOOT_COMPLETE",
)

EXCEPTION_MARKERS = (
    "FINNOS:KERNEL:GDT_OK", "FINNOS:KERNEL:TSS_OK", "FINNOS:KERNEL:IDT_OK",
    "FINNOS:KERNEL:EXCEPTIONS_READY", "FINNOS:TEST:EXCEPTIONS:BEGIN",
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

def qemu_command(qemu: str, firmware: str, image: Path, headless: bool = False, test_exit: bool = False) -> list[str]:
    # Homebrew's code-only OVMF image is a pflash image; using -bios makes
    # QEMU 11 reject it before the guest starts.
    command = [qemu, "-machine", "q35", "-m", "256M", "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}", "-drive", f"if=ide,format=raw,file={image}", "-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none"]
    if headless: command.extend(["-display", "none"])
    if test_exit: command.extend(["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"])
    return command
