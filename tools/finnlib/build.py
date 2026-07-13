"""Cargo build orchestration."""
from __future__ import annotations

import hashlib
import shutil
import subprocess
from pathlib import Path

def cargo(root: Path, args: list[str]) -> None:
    subprocess.run(["cargo", *args], cwd=root, check=True)

def build_boot(root: Path, test: bool = False, exceptions: bool = False, memory_map: bool = False, page_allocator: bool = False, page_tables: bool = False, heap: bool = False, timer: bool = False) -> tuple[Path, Path]:
    output = root / "build" / "out" / ("x86_64-qemu-timer-interrupts" if timer else "x86_64-qemu-heap" if heap else "x86_64-qemu-page-tables" if page_tables else "x86_64-qemu-page-allocator" if page_allocator else "x86_64-qemu-memory-map" if memory_map else "x86_64-qemu-exceptions" if exceptions else "x86_64-qemu-test" if test else "x86_64-qemu")
    output.mkdir(parents=True, exist_ok=True)
    if timer:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-timer-interrupts"
    elif heap:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-heap"
    elif page_tables:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-page-tables"
    elif page_allocator:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-page-allocator"
    elif memory_map:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-memory-map"
    elif exceptions:
        kernel_features = "kernel-bin,qemu-test-exit,qemu-test-exceptions"
    elif test:
        kernel_features = "kernel-bin,qemu-test-exit"
    else:
        kernel_features = "kernel-bin"
    # Use a feature-specific target directory so Cargo does not reuse a binary built with
    # different feature flags. This is essential for the exception-test binary, which is the
    # only one compiled with `qemu-test-exceptions`.
    target_dir = root / "target" / ("x86_64-unknown-none-timer-interrupts" if timer else "x86_64-unknown-none-heap" if heap else "x86_64-unknown-none-page-tables" if page_tables else "x86_64-unknown-none-page-allocator" if page_allocator else "x86_64-unknown-none-memory-map" if memory_map else "x86_64-unknown-none-exceptions" if exceptions else "x86_64-unknown-none-test" if test else "x86_64-unknown-none-normal")
    cargo(root, ["build", "-p", "finn-kernel", "--bin", "finn-kernel-x86_64", "--features", kernel_features, "--target", "x86_64-unknown-none", "--target-dir", str(target_dir)])
    cargo(root, ["build", "-p", "finn-boot-uefi", "--bin", "finn-boot-x86_64", "--features", "uefi-app", "--target", "x86_64-unknown-uefi"])
    profile = "debug"
    kernel = target_dir / "x86_64-unknown-none" / profile / "finn-kernel-x86_64"
    boot = root / "target" / "x86_64-unknown-uefi" / profile / "finn-boot-x86_64.efi"
    if not boot.exists(): boot = root / "target" / "x86_64-unknown-uefi" / profile / "finn-boot-x86_64"
    if not kernel.is_file() or not boot.is_file(): raise RuntimeError("expected boot artifacts were not produced")
    manifest = output / "manifest.txt"
    manifest.write_text("profile = debug\n" + artifact_manifest("kernel", kernel) + artifact_manifest("boot_manager", boot), encoding="utf-8")
    return boot, kernel

def artifact_manifest(name: str, path: Path) -> str:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return f"{name}.path = {path.name}\n{name}.size = {path.stat().st_size}\n{name}.sha256 = {digest}\n"
