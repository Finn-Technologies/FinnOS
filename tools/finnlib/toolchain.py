"""Tool and firmware discovery helpers."""
from __future__ import annotations

import os
import shutil
from pathlib import Path

OVMF_CANDIDATES = (
    "/opt/homebrew/opt/qemu/share/qemu/edk2-x86_64-code.fd",
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",
    "/usr/local/share/qemu/edk2-x86_64-code.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/usr/share/edk2/x64/OVMF_CODE.fd",
    "/usr/share/qemu/edk2-x86_64-code.fd",
)

QEMU_CANDIDATES = (
    "/opt/homebrew/bin/qemu-system-x86_64",
    "/opt/homebrew/opt/qemu/bin/qemu-system-x86_64",
    "/usr/local/bin/qemu-system-x86_64",
    "/usr/local/opt/qemu/bin/qemu-system-x86_64",
)

QEMU_IMG_CANDIDATES = (
    "/opt/homebrew/bin/qemu-img",
    "/opt/homebrew/opt/qemu/bin/qemu-img",
    "/usr/local/bin/qemu-img",
    "/usr/local/opt/qemu/bin/qemu-img",
)

def find_command(name: str) -> str | None:
    return shutil.which(name)

def find_tool(name: str, environ: dict[str, str] | None = None) -> str | None:
    env = os.environ if environ is None else environ
    override_name = {"qemu-system-x86_64": "FINNOS_QEMU_X86_64", "qemu-img": "FINNOS_QEMU_IMG"}.get(name)
    if override_name and env.get(override_name):
        path = Path(env[override_name])
        return str(path) if path.is_file() and path.stat().st_mode & 0o111 else None
    direct = shutil.which(name)
    if direct:
        return direct
    candidates = QEMU_CANDIDATES if name == "qemu-system-x86_64" else QEMU_IMG_CANDIDATES if name == "qemu-img" else ()
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file() and path.stat().st_mode & 0o111:
            return str(path)
    return None

def find_ovmf(environ: dict[str, str] | None = None, candidates: tuple[str, ...] = OVMF_CANDIDATES) -> Path | None:
    env = os.environ if environ is None else environ
    override = env.get("FINNOS_OVMF_CODE")
    if override:
        path = Path(override)
        return path if path.is_file() else None
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file():
            return path
    return None

def rust_target_installed(target: str, rustup: str = "rustup") -> bool:
    import subprocess
    result = subprocess.run([rustup, "target", "list", "--installed"], capture_output=True, text=True, check=False)
    return result.returncode == 0 and target in result.stdout.split()
