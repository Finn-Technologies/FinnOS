# Debugging FinnOS

FinnOS uses COM1 serial markers as its primary runtime evidence. Run `./tools/finn run-headless` for serial output or a bounded `test-*` command for automatic validation. Set `FINNOS_BOOT_TIMEOUT_SECONDS` when a slow emulator needs a larger limit.

For an integration failure:

1. Preserve the complete QEMU command, serial output, host status, tool versions, OVMF path, and source revision.
2. Find the last ordered `FINNOS:` marker and trace it in `kernel/src/bin/x86_64.rs` or `boot/uefi/src/main.rs`.
3. Distinguish intentional exception tests from unexpected `PANIC`, `FATAL`, bootloader error, timeout, or marker reordering.
4. Re-run only the failing feature-specific command; Cargo uses separate target directories to avoid feature contamination.
5. Use `llvm-addr2line`/`rust-objdump` from the active Rust LLVM tools against the exact unstripped kernel when resolving addresses. Record the command and artifact hash.

The project does not yet provide a maintained GDB stub workflow, persistent crash dump, symbol server, or real-hardware debugger guide. Do not infer source locations from a differently built ELF because fixed addresses and feature images differ.
