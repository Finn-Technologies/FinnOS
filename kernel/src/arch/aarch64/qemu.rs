//! `AArch64` QEMU test termination using the semihosting exit operation.

#[repr(C, align(8))]
struct ExitBlock {
    reason: u64,
    subcode: u64,
}

const APPLICATION_EXIT: u64 = 0x0002_0026;
static SUCCESS: ExitBlock = ExitBlock {
    reason: APPLICATION_EXIT,
    subcode: 0,
};
static FAILURE: ExitBlock = ExitBlock {
    reason: APPLICATION_EXIT,
    subcode: 1,
};

/// Terminate a semihosting-enabled QEMU test with success.
pub fn success() -> ! {
    exit(&SUCCESS)
}

/// Terminate a semihosting-enabled QEMU test with failure.
pub fn failure() -> ! {
    exit(&FAILURE)
}

fn exit(block: &'static ExitBlock) -> ! {
    // SAFETY: The test QEMU command explicitly enables AArch64 semihosting.
    // SYS_EXIT_EXTENDED (0x20) reads the aligned, static two-word block.
    unsafe {
        core::arch::asm!(
            "dsb sy",
            "hlt #0xf000",
            in("x0") 0x20_u64,
            in("x1") block,
            options(noreturn)
        );
    }
}
