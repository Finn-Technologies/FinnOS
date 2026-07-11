# First Boot development

> Status: In-progress milestone guide
> Implementation: x86-64 QEMU smoke test passes

Install Rust stable, the `x86_64-unknown-none` and `x86_64-unknown-uefi` targets, QEMU, `qemu-img`, and OVMF. Run `./tools/finn doctor`, then `./tools/finn build-boot`, `./tools/finn image`, and `./tools/finn run` for the intended workflow. `./tools/finn test-boot` is the headless debug-exit smoke test.

Expected serial output is the ordered bootloader and kernel marker sequence listed in `tools/finnlib/qemu.py`. The diagnostic screen is a kernel framebuffer test, not Peony. Common failures are missing Rust targets, missing OVMF, missing QEMU tools, invalid ELF metadata, and unavailable GOP formats.
