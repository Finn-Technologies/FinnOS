"""Implementation of the FinnOS developer command."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

from .build import BootMode, build_boot, cargo, output_directory
from .config import ConfigurationError, load_configuration
from .image import make_image, stage_esp
from .qemu import (
    EXCEPTION_MARKERS,
    FORBIDDEN_EXCEPTION_MARKERS,
    MARKERS,
    qemu_command,
    validate_exceptions,
    validate_memory_map,
    validate_page_allocator,
    validate_page_tables,
    validate_heap,
    validate_timer,
    validate_cooperative_tasks,
    validate_smoke,
    validate_arm64_exceptions,
    validate_arm64_exception_fatal,
    validate_arm64_memory_map,
    validate_arm64_page_tables,
    validate_arm64_gic,
    validate_arm64_smoke,
)
from .toolchain import find_command, find_firmware, find_tool, rust_target_installed

ROOT = Path(__file__).resolve().parents[2]

BOOT_MODES = {
    "test-boot": BootMode.FIRST_BOOT,
    "test-exceptions": BootMode.EXCEPTIONS,
    "test-arm64-exception-fatal": BootMode.ARM64_EXCEPTION_FATAL,
    "test-memory-map": BootMode.MEMORY_MAP,
    "test-page-allocator": BootMode.PAGE_ALLOCATOR,
    "test-page-tables": BootMode.PAGE_TABLES,
    "test-arm64-gic": BootMode.ARM64_GIC,
    "test-heap": BootMode.HEAP,
    "test-timer-interrupts": BootMode.TIMER,
    "test-cooperative-tasks": BootMode.COOPERATIVE_TASKS,
}
BUILD_OPTION_COMMANDS = {"doctor", "build", "build-boot", "image", "run", "run-headless", *BOOT_MODES}


def doctor(target_name: Optional[str] = None) -> int:
    target, _profile = load_configuration(ROOT).select(target_name, "development")
    required = ("git", "cargo", "rustc", "rustfmt", "python3")
    first_boot = (target.qemu_system, "qemu-img")
    missing = [tool for tool in required if not find_command(tool)]
    for tool in required: print(f"[{'ok' if tool not in missing else 'missing'}] {tool}")
    for tool in first_boot: print(f"[{'ok' if find_tool(tool) else 'missing'}] {tool}")
    cargo_targets = (target.kernel_cargo_target, target.boot_cargo_target)
    for cargo_target in cargo_targets:
        present = rust_target_installed(cargo_target)
        print(f"[{'ok' if present else 'missing'}] rust target: {cargo_target}")
        if not present: print(f"      install: rustup target add {cargo_target}")
    for relative in ("Cargo.toml", "Finnfile.toml", "boot/protocol/Cargo.toml", "boot/uefi/Cargo.toml", "kernel/Cargo.toml"):
        print(f"[{'ok' if (ROOT / relative).is_file() else 'missing'}] repository file: {relative}")
    firmware = find_firmware(target.architecture)
    firmware_name = "AAVMF" if target.architecture == "arm64" else "OVMF"
    print(f"[{'ok' if firmware else 'missing'}] {firmware_name} firmware{': ' + str(firmware) if firmware else ''}")
    return 1 if missing or any(not find_tool(tool) for tool in first_boot) or not firmware or any(not rust_target_installed(cargo_target) for cargo_target in cargo_targets) else 0

def command(
    name: str,
    target_name: Optional[str] = None,
    profile_name: Optional[str] = None,
) -> int:
    if name == "help":
        print("FinnOS developer wrapper for x86-64 and ARM64 UEFI development targets.")
        print("Commands: help doctor build test format format-check lint check build-boot image run run-headless test-python test-boot test-exceptions test-arm64-exception-fatal test-memory-map test-page-allocator test-page-tables test-arm64-gic test-heap test-timer-interrupts test-cooperative-tasks check-all clean")
        print("Build options: --target TARGET --profile development|release")
        return 0
    if (target_name or profile_name) and name not in BUILD_OPTION_COMMANDS:
        raise ConfigurationError(f"{name!r} does not accept --target or --profile")
    if name == "doctor":
        if profile_name is not None:
            raise ConfigurationError("'doctor' does not accept --profile")
        return doctor(target_name)
    if name == "build":
        _target, profile = load_configuration(ROOT).select(target_name, profile_name)
        cargo(ROOT, ["build", "--workspace", *profile.cargo_args])
        return 0
    if name == "test": cargo(ROOT, ["test", "--workspace"]); return 0
    if name == "format": cargo(ROOT, ["fmt", "--all"]); return 0
    if name == "format-check": cargo(ROOT, ["fmt", "--all", "--", "--check"]); return 0
    if name == "lint": cargo(ROOT, ["clippy", "--workspace", "--all-targets", "--", "-D", "warnings"]); return 0
    if name == "check":
        return run_steps(("format-check", "build", "lint", "test", "test-python"))
    if name in ("build-boot", "image", "run", "run-headless", *BOOT_MODES):
        target, profile = load_configuration(ROOT).select(target_name, profile_name)
        mode = BOOT_MODES.get(name, BootMode.NORMAL)
        if target.architecture == "arm64" and mode not in (
            BootMode.NORMAL,
            BootMode.FIRST_BOOT,
            BootMode.EXCEPTIONS,
            BootMode.ARM64_EXCEPTION_FATAL,
            BootMode.MEMORY_MAP,
            BootMode.PAGE_TABLES,
            BootMode.ARM64_GIC,
        ):
            raise ConfigurationError(
                f"{name!r} is not implemented for target {target.name!r}; "
                "this mode is not implemented for ARM64"
            )
        if target.architecture != "arm64" and mode in (
            BootMode.ARM64_EXCEPTION_FATAL,
            BootMode.ARM64_GIC,
        ):
            raise ConfigurationError(f"{name!r} is implemented only for arm64-qemu")
        out = output_directory(ROOT, target, profile, mode)
        boot, kernel = build_boot(ROOT, target, profile, mode)
        esp = stage_esp(out, boot, kernel, target.boot_filename, target.kernel_filename)
        if name == "build-boot":
            return 0
        image = make_image(esp, out / target.image_filename)
        if name == "image":
            return 0
        firmware = find_firmware(target.architecture)
        qemu = find_tool(target.qemu_system)
        if not firmware or not qemu:
            firmware_name = "AAVMF" if target.architecture == "arm64" else "OVMF"
            raise RuntimeError(f"{target.qemu_system} and {firmware_name} are required")
        args = qemu_command(
            qemu, str(firmware), image, headless=name != "run",
            test_exit=mode.test_exit, machine=target.qemu_machine,
            architecture=target.architecture, cpu=target.qemu_cpu,
        )
        print("$ " + " ".join(args), flush=True)
        if mode.test_exit:
            try:
                result = subprocess.run(
                    args, capture_output=True, text=True,
                    timeout=float(os.environ.get("FINNOS_BOOT_TIMEOUT_SECONDS", "45")),
                    check=False,
                )
            except subprocess.TimeoutExpired as error:
                partial = _captured_text(error.stdout) + _captured_text(error.stderr)
                (out / "serial.log").write_text(partial, encoding="utf-8")
                print(f"serial log: {out / 'serial.log'}", file=sys.stderr)
                raise
            output = result.stdout + result.stderr
            (out / "serial.log").write_text(output, encoding="utf-8")
            print(output)
            print(f"qemu status: {result.returncode}")
            validator = (
                validate_arm64_gic
                if target.architecture == "arm64" and mode == BootMode.ARM64_GIC
                else validate_arm64_page_tables
                if target.architecture == "arm64" and mode == BootMode.PAGE_TABLES
                else validate_arm64_memory_map
                if target.architecture == "arm64" and mode == BootMode.MEMORY_MAP
                else validate_arm64_exception_fatal
                if target.architecture == "arm64" and mode == BootMode.ARM64_EXCEPTION_FATAL
                else validate_arm64_exceptions
                if target.architecture == "arm64" and mode == BootMode.EXCEPTIONS
                else validate_arm64_smoke
                if target.architecture == "arm64"
                else {
                BootMode.COOPERATIVE_TASKS: validate_cooperative_tasks,
                BootMode.EXCEPTIONS: validate_exceptions,
                BootMode.MEMORY_MAP: validate_memory_map,
                BootMode.PAGE_ALLOCATOR: validate_page_allocator,
                BootMode.PAGE_TABLES: validate_page_tables,
                BootMode.HEAP: validate_heap,
                BootMode.TIMER: validate_timer,
                BootMode.FIRST_BOOT: validate_smoke,
                }[mode]
            )
            errors = validator(result.returncode, output)
            if errors:
                print("smoke test failure:")
                print("\n".join(f"- {error}" for error in errors))
                print(f"serial log: {out / 'serial.log'}")
            return 1 if errors else 0
        subprocess.run(args, check=True)
        return 0
    if name == "test-python": subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tools/tests", "-p", "test_*.py"], cwd=ROOT, check=True); return 0
    if name == "check-all":
        return run_steps(("doctor", "check", "image", "test-boot", "test-exceptions", "test-memory-map", "test-page-allocator", "test-page-tables", "test-heap", "test-timer-interrupts", "test-cooperative-tasks"))
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
    try:
        name, target_name, profile_name = parse_arguments(sys.argv[1:])
        return command(name, target_name, profile_name)
    except KeyboardInterrupt: print("\nInterrupted.", file=sys.stderr); return 130
    except subprocess.CalledProcessError as error:
        print(f"error: command failed ({error.returncode}): {_command_text(error.cmd)}", file=sys.stderr)
        _print_captured("stdout", error.stdout)
        _print_captured("stderr", error.stderr)
        return error.returncode or 1
    except subprocess.TimeoutExpired as error: print(f"QEMU timed out after {error.timeout}s", file=sys.stderr); return 1
    except (OSError, RuntimeError) as error: print(f"error: {error}", file=sys.stderr); return 1


def parse_arguments(arguments: list[str]) -> tuple[str, Optional[str], Optional[str]]:
    if not arguments:
        return "help", None, None
    name = arguments[0]
    target_name: Optional[str] = None
    profile_name: Optional[str] = None
    index = 1
    while index < len(arguments):
        option = arguments[index]
        if option not in ("--target", "--profile"):
            raise ConfigurationError(f"unknown argument {option!r}")
        if index + 1 >= len(arguments):
            raise ConfigurationError(f"{option} requires a value")
        value = arguments[index + 1]
        if option == "--target":
            if target_name is not None:
                raise ConfigurationError("--target may be provided only once")
            target_name = value
        else:
            if profile_name is not None:
                raise ConfigurationError("--profile may be provided only once")
            profile_name = value
        index += 2
    return name, target_name, profile_name


def _command_text(command_value: object) -> str:
    if isinstance(command_value, (list, tuple)):
        return " ".join(str(part) for part in command_value)
    return str(command_value)


def _print_captured(label: str, value: object) -> None:
    if not value:
        return
    if isinstance(value, bytes):
        rendered = value.decode(errors="replace")
    else:
        rendered = str(value)
    print(f"{label}:\n{rendered.rstrip()}", file=sys.stderr)


def _captured_text(value: object) -> str:
    if not value:
        return ""
    if isinstance(value, bytes):
        return value.decode(errors="replace")
    return str(value)
