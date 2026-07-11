# QEMU development

> Status: Planned first-boot workflow
> Implementation: Command construction and marker parsing are tested; QEMU is unavailable in the current environment

`tools/finn` discovers `FINNOS_OVMF_CODE` first, then standard Homebrew and Linux OVMF paths. `FINNOS_QEMU_X86_64` overrides QEMU and `FINNOS_BOOT_TIMEOUT_SECONDS` controls the smoke-test timeout, defaulting to 45 seconds.

An x86-64 guest on Apple silicon must use software emulation; the command must not request KVM or HVF. Homebrew’s code-only OVMF image is passed as a read-only pflash drive. Serial is attached to stdio. Test mode adds `isa-debug-exit` at port `0xf4`: value `0x10` maps to host status 33 and `0x11` maps to 35. Manual runs keep the display open and can be stopped with Ctrl+C.
