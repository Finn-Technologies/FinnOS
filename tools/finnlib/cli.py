"""Implementation of the FinnOS developer command."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from .build import build_boot, cargo
from .image import make_image, stage_esp
from .qemu import (
    EXCEPTION_MARKERS,
    FORBIDDEN_EXCEPTION_MARKERS,
    MARKERS,
    qemu_command,
    validate_exceptions,
    validate_memory_map,
    validate_page_allocator,
    validate_smoke,
)
from .toolchain import find_command, find_ovmf, find_tool, rust_target_installed

ROOT = Path(__file__).resolve().parents[2]

def doctor() -> int:
    required = ("git", "cargo", "rustc", "rustfmt", "python3")
    first_boot = ("qemu-system-x86_64", "qemu-img")
    missing = [tool for tool in required if not find_command(tool)]
    for tool in required: print(f"[{'ok' if tool not in missing else 'missing'}] {tool}")
    for tool in first_boot: print(f"[{'ok' if find_tool(tool) else 'missing'}] {tool}")
    for target in ("x86_64-unknown-none", "x86_64-unknown-uefi"):
        present = rust_target_installed(target)
        print(f"[{'ok' if present else 'missing'}] rust target: {target}")
        if not present: print(f"      install: rustup target add {target}")
    for relative in ("Cargo.toml", "Finnfile.toml", "boot/protocol/Cargo.toml", "boot/uefi/Cargo.toml", "kernel/Cargo.toml", "kernel/arch/x86_64/linker.ld"):
        print(f"[{'ok' if (ROOT / relative).is_file() else 'missing'}] repository file: {relative}")
    firmware = find_ovmf()
    print(f"[{'ok' if firmware else 'missing'}] OVMF firmware{': ' + str(firmware) if firmware else ''}")
    return 1 if missing or any(not find_tool(tool) for tool in first_boot) or not firmware or any(not rust_target_installed(target) for target in ("x86_64-unknown-none", "x86_64-unknown-uefi")) else 0

def command(name: str) -> int:
    if name == "help":
        print("FinnOS developer wrapper for the x86-64 UEFI First Boot milestone.")
        print("Commands: help doctor build test format format-check lint check build-boot image run run-headless test-python test-boot test-exceptions test-memory-map test-page-allocator check-all clean")
        return 0
    if name == "doctor": return doctor()
    if name == "build": cargo(ROOT, ["build", "--workspace"]); return 0
    if name == "test": cargo(ROOT, ["test", "--workspace"]); return 0
    if name == "format": cargo(ROOT, ["fmt", "--all"]); return 0
    if name == "format-check": cargo(ROOT, ["fmt", "--all", "--", "--check"]); return 0
    if name == "lint": cargo(ROOT, ["clippy", "--workspace", "--all-targets", "--", "-D", "warnings"]); return 0
    if name == "check":
        return run_steps(("format-check", "build", "lint", "test", "test-python"))
    if name == "build-boot":
        boot, kernel = build_boot(ROOT); stage_esp(ROOT / "build" / "out" / "x86_64-qemu", boot, kernel); return 0
    if name == "image":
        out = ROOT / "build" / "out" / "x86_64-qemu"; boot, kernel = build_boot(ROOT); esp = stage_esp(out, boot, kernel); make_image(esp, out / "finnos-x86_64-uefi.img"); return 0
    if name in ("run", "run-headless", "test-boot", "test-exceptions", "test-memory-map", "test-page-allocator"):
        test = name == "test-boot"
        exceptions = name == "test-exceptions"
        memory_map = name == "test-memory-map"
        page_allocator = name == "test-page-allocator"
        out = ROOT / "build" / "out" / ("x86_64-qemu-page-allocator" if page_allocator else "x86_64-qemu-memory-map" if memory_map else "x86_64-qemu-exceptions" if exceptions else "x86_64-qemu-test" if test else "x86_64-qemu")
        boot, kernel = build_boot(ROOT, test=test, exceptions=exceptions, memory_map=memory_map, page_allocator=page_allocator); esp = stage_esp(out, boot, kernel); image = make_image(esp, out / "finnos-x86_64-uefi.img"); firmware = find_ovmf(); qemu = find_tool("qemu-system-x86_64")
        if not firmware or not qemu: raise RuntimeError("QEMU and OVMF are required")
        args = qemu_command(qemu, str(firmware), image, headless=name != "run", test_exit=test or exceptions or memory_map or page_allocator); print("$ " + " ".join(args), flush=True)
        if test or exceptions or memory_map or page_allocator:
            result = subprocess.run(args, capture_output=True, text=True, timeout=float(os.environ.get("FINNOS_BOOT_TIMEOUT_SECONDS", "45")), check=False); output = result.stdout + result.stderr; print(output)
            print(f"qemu status: {result.returncode}")
            if exceptions:
                errors = validate_exceptions(result.returncode, output)
            elif memory_map:
                errors = validate_memory_map(result.returncode, output)
            elif page_allocator:
                errors = validate_page_allocator(result.returncode, output)
            else:
                errors = validate_smoke(result.returncode, output)
            if errors:
                print("smoke test failure:"); print("\n".join(f"- {error}" for error in errors)); print("serial log:"); print(output)
            return 1 if errors else 0
        subprocess.run(args, check=True); return 0
    if name == "test-python": subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tools/tests", "-p", "test_*.py"], cwd=ROOT, check=True); return 0
    if name == "check-all":
        return run_steps(("doctor", "check", "image", "test-boot", "test-exceptions", "test-memory-map", "test-page-allocator"))
    if name == "clean":
        for path in (ROOT / "target", ROOT / "build" / "out"):
            if path.exists() and ROOT in path.parents: print(f"removing {path}"); shutil.rmtree(path)
        return 0
    print(f"error: unknown command {name!r}; run './tools/finn help'", file=sys.stderr); return 2

def run_steps(steps: tuple[str, ...]) -> int:
    for step in steps:
        status = command(step)
        if status != 0:
            return status
    return 0

def main() -> int:
    try: return command(sys.argv[1] if len(sys.argv) > 1 else "help")
    except KeyboardInterrupt: print("\nInterrupted.", file=sys.stderr); return 130
    except subprocess.CalledProcessError as error: return error.returncode or 1
    except subprocess.TimeoutExpired as error: print(f"QEMU timed out after {error.timeout}s", file=sys.stderr); return 1
    except (OSError, RuntimeError) as error: print(f"error: {error}", file=sys.stderr); return 1
