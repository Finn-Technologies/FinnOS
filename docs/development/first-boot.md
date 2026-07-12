# First Boot development

> Status: In-progress milestone guide
> Implementation: x86-64 QEMU smoke test passes

Install Rust stable, the `x86_64-unknown-none` and `x86_64-unknown-uefi` targets, QEMU, `qemu-img`, and OVMF. Run `./tools/finn doctor`, then `./tools/finn build-boot`, `./tools/finn image`, and `./tools/finn run` for the intended workflow. `./tools/finn test-boot` is the headless debug-exit smoke test.

Expected serial output is the ordered bootloader and kernel marker sequence listed in `tools/finnlib/qemu.py`. After `FINNOS:KERNEL:ENTRY`, the kernel now prints `FINNOS:KERNEL:TSS_INIT`, `FINNOS:KERNEL:GDT_LOADED`, `FINNOS:KERNEL:SEGMENTS_RELOADED`, `FINNOS:KERNEL:GDT_OK`, `FINNOS:KERNEL:TSS_OK`, `FINNOS:KERNEL:IDT_OK`, and `FINNOS:KERNEL:EXCEPTIONS_READY` before validating `BootInfo` and parsing the UEFI memory map. Successful memory-map parsing emits `FINNOS:KERNEL:MEMORY_MAP_PARSED`, `FINNOS:KERNEL:MEMORY_MAP_CLASSIFIED`, `FINNOS:KERNEL:PAGE_ALLOCATOR_READY`, and deterministic `FINNOS:MEMORY:*` summary markers before `FINNOS:KERNEL:FIRST_BOOT_COMPLETE`. The diagnostic screen is a kernel framebuffer test, not Peony. Common failures are missing Rust targets, missing OVMF, missing QEMU tools, invalid ELF metadata, unavailable GOP format, malformed descriptor tables, invalid protected memory ranges, and allocator exhaustion or malformed extents.
Successful boot now includes `PAGE_TABLES_BUILT`, `PAGE_TABLES_ACTIVATING`, `PAGE_TABLES_ACTIVE`, `ADDRESS_SPACE_VALIDATED`, `HEAP_MAPPED`, and `HEAP_READY` before framebuffer output.
# First Boot timer order

After heap initialization, FinnOS installs the external IDT gates, remaps and
masks the PIC, maps and enables the BSP xAPIC, calibrates and starts its 100 Hz
timer, enables IF, and observes a real tick before `FIRST_BOOT_COMPLETE`.
