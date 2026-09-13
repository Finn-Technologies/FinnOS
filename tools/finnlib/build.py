"""Cargo build orchestration."""
from __future__ import annotations

import hashlib
import subprocess
import sys
from enum import Enum
from pathlib import Path

from .config import BuildProfile, BuildTarget
from .toolchain import find_firmware


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
    PREEMPTION_CONTEXT = (
        "preemption-context",
        "kernel-bin,qemu-test-preemption-context",
        True,
    )
    USERSPACE = (
        "userspace",
        "kernel-bin,qemu-test-exit,qemu-test-userspace",
        True,
    )
    IPC = ("ipc", "kernel-bin,qemu-test-exit,qemu-test-ipc", True)
    ELF_LOADER = ("elf-loader", "kernel-bin,qemu-test-exit,qemu-test-elf-loader", True)
    INIT = ("init", "kernel-bin,qemu-test-exit,qemu-test-init", True)
    DESKTOP = ("desktop", "kernel-bin,qemu-test-exit,qemu-test-desktop", True)

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
    data_image = output / target.data_image if target.data_image else None
    manifest.write_text(
        generate_reproducible_manifest(
            root, target, profile, artifact_profile, kernel, boot, data_image
        ),
        encoding="utf-8",
    )
    return boot, kernel

def generate_reproducible_manifest(
    root: Path,
    target: BuildTarget,
    profile: BuildProfile,
    artifact_profile: str,
    kernel: Path,
    boot: Path,
    data_image: Path | None = None,
) -> str:
    lines = [
        f"target = {target.name}",
        f"profile = {profile.name}",
        f"cargo_profile = {artifact_profile}",
    ]
    try:
        rev = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root, capture_output=True, text=True, check=False,
        ).stdout.strip()
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=root, capture_output=True, text=True, check=False,
        ).stdout.strip()
        dirty = "dirty" if status else "clean"
        lines.append(f"source.revision = {rev} ({dirty})")
    except Exception:
        lines.append("source.revision = unknown")

    lockfile = root / "Cargo.lock"
    if lockfile.is_file():
        lines.append(f"cargo_lock.sha256 = {hashlib.sha256(lockfile.read_bytes()).hexdigest()}")

    try:
        rustc_ver = subprocess.run(
            ["rustc", "--version"],
            capture_output=True, text=True, check=False,
        ).stdout.strip()
        lines.append(f"toolchain.rustc = {rustc_ver}")
    except Exception:
        pass

    try:
        cargo_ver = subprocess.run(
            ["cargo", "--version"],
            capture_output=True, text=True, check=False,
        ).stdout.strip()
        lines.append(f"toolchain.cargo = {cargo_ver}")
    except Exception:
        pass

    lines.append(f"toolchain.python = {sys.version.split()[0]}")

    firmware_path = find_firmware(target.architecture)
    if firmware_path and firmware_path.is_file():
        firmware_digest = hashlib.sha256(firmware_path.read_bytes()).hexdigest()
        lines.append(f"firmware.path = {firmware_path}")
        lines.append(f"firmware.sha256 = {firmware_digest}")

    lines.append(artifact_manifest("kernel", kernel).strip())
    lines.append(artifact_manifest("boot_manager", boot).strip())

    if data_image and data_image.is_file():
        lines.append(artifact_manifest("data_image", data_image).strip())

    return "\n".join(lines) + "\n"

def artifact_manifest(name: str, path: Path) -> str:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return f"{name}.path = {path.name}\n{name}.size = {path.stat().st_size}\n{name}.sha256 = {digest}\n"
