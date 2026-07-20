"""Cargo build orchestration."""
from __future__ import annotations

import hashlib
import subprocess
from enum import Enum
from pathlib import Path

from .config import BuildProfile, BuildTarget


class BootMode(Enum):
    NORMAL = ("", "kernel-bin", False)
    FIRST_BOOT = ("test", "kernel-bin,qemu-test-exit", True)
    EXCEPTIONS = ("exceptions", "kernel-bin,qemu-test-exit,qemu-test-exceptions", True)
    ARM64_EXCEPTION_FATAL = (
        "arm64-exception-fatal",
        "kernel-bin,qemu-test-exit,qemu-test-arm64-exception-fatal",
        True,
    )
    MEMORY_MAP = ("memory-map", "kernel-bin,qemu-test-exit,qemu-test-memory-map", True)
    PAGE_ALLOCATOR = ("page-allocator", "kernel-bin,qemu-test-exit,qemu-test-page-allocator", True)
    PAGE_TABLES = ("page-tables", "kernel-bin,qemu-test-exit,qemu-test-page-tables", True)
    ARM64_GIC = ("arm64-gic", "kernel-bin,qemu-test-exit,qemu-test-arm64-gic", True)
    HEAP = ("heap", "kernel-bin,qemu-test-exit,qemu-test-heap", True)
    TIMER = ("timer-interrupts", "kernel-bin,qemu-test-exit,qemu-test-timer-interrupts", True)
    COOPERATIVE_TASKS = (
        "cooperative-tasks",
        "kernel-bin,qemu-test-exit,qemu-test-cooperative-tasks",
        True,
    )

    def __init__(self, suffix: str, kernel_features: str, test_exit: bool) -> None:
        self.suffix = suffix
        self.kernel_features = kernel_features
        self.test_exit = test_exit


def cargo(root: Path, args: list[str]) -> None:
    subprocess.run(["cargo", *args], cwd=root, check=True)


def output_directory(root: Path, target: BuildTarget, profile: BuildProfile, mode: BootMode) -> Path:
    parts = [target.name]
    if mode.suffix:
        parts.append(mode.suffix)
    if profile.cargo_profile != "debug":
        parts.append(profile.name)
    return root / "build" / "out" / "-".join(parts)


def build_boot(
    root: Path,
    target: BuildTarget,
    profile: BuildProfile,
    mode: BootMode = BootMode.NORMAL,
) -> tuple[Path, Path]:
    output = output_directory(root, target, profile, mode)
    output.mkdir(parents=True, exist_ok=True)
    # Use a feature-specific target directory so Cargo does not reuse a binary built with
    # different feature flags. The profile remains a Cargo subdirectory within that isolation.
    mode_name = mode.suffix or "normal"
    target_dir = root / "target" / f"{target.kernel_cargo_target}-{mode_name}"
    cargo(root, [
        "build", "-p", target.kernel_package, "--bin", target.kernel_binary,
        "--features", mode.kernel_features, "--target", target.kernel_cargo_target,
        "--target-dir", str(target_dir), *profile.cargo_args,
    ])
    cargo(root, [
        "build", "-p", target.boot_package, "--bin", target.boot_binary,
        "--features", "uefi-app", "--target", target.boot_cargo_target,
        *profile.cargo_args,
    ])
    artifact_profile = profile.cargo_profile
    kernel = target_dir / target.kernel_cargo_target / artifact_profile / target.kernel_binary
    boot = root / "target" / target.boot_cargo_target / artifact_profile / f"{target.boot_binary}.efi"
    if not boot.exists():
        boot = root / "target" / target.boot_cargo_target / artifact_profile / target.boot_binary
    if not kernel.is_file() or not boot.is_file(): raise RuntimeError("expected boot artifacts were not produced")
    manifest = output / "manifest.txt"
    manifest.write_text(
        f"target = {target.name}\nprofile = {profile.name}\ncargo_profile = {artifact_profile}\n"
        + artifact_manifest("kernel", kernel)
        + artifact_manifest("boot_manager", boot),
        encoding="utf-8",
    )
    return boot, kernel

def artifact_manifest(name: str, path: Path) -> str:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return f"{name}.path = {path.name}\n{name}.size = {path.stat().st_size}\n{name}.sha256 = {digest}\n"
