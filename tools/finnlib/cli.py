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
from .image import make_data_image, make_image, stage_esp
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
    validate_arm64_timer,
    validate_arm64_cooperative_tasks,
    validate_arm64_smoke,
    validate_preemption_context,
    validate_userspace,
    validate_arm64_userspace,
    validate_ipc,
    validate_arm64_ipc,
    validate_elf_loader,
    validate_arm64_elf_loader,
    validate_init,
    validate_arm64_init,
    validate_desktop,
    validate_arm64_desktop,
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
    "test-preemption-context": BootMode.PREEMPTION_CONTEXT,
    "test-userspace": BootMode.USERSPACE,
    "test-ipc": BootMode.IPC,
    "test-elf-loader": BootMode.ELF_LOADER,
    "test-init": BootMode.INIT,
    "test-desktop": BootMode.DESKTOP,
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
    data_drive: Optional[str] = None,
) -> int:
    if name == "help":
        print("FinnOS developer wrapper for x86-64 and ARM64 UEFI development targets.")
        print("Commands: help doctor build test format format-check lint check build-boot image run run-headless test-python test-boot test-exceptions test-arm64-exception-fatal test-memory-map test-page-allocator test-page-tables test-arm64-gic test-heap test-timer-interrupts test-cooperative-tasks test-preemption-context test-userspace test-ipc test-elf-loader test-init test-desktop check-all clean")
        print("Build options: --target TARGET --profile development|release [--data-drive [PATH]]")
        return 0
    if (target_name or profile_name or data_drive) and name not in BUILD_OPTION_COMMANDS:
        raise ConfigurationError(f"{name!r} does not accept --target, --profile, or --data-drive")
    if name == "doctor":
        if profile_name is not None:
            raise ConfigurationError("'doctor' does not accept --profile")
        return doctor(target_name)
    if name == "build":
        target, profile = load_configuration(ROOT).select(target_name, profile_name)
        cargo(ROOT, ["build", "--workspace", "--all-targets", *profile.cargo_args])
        return 0
    if name == "test": cargo(ROOT, ["test", "--workspace", "--", "--test-threads=1"]); return 0
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
            BootMode.TIMER,
            BootMode.COOPERATIVE_TASKS,
            BootMode.USERSPACE,
            BootMode.IPC,
            BootMode.ELF_LOADER,
            BootMode.INIT,
            BootMode.DESKTOP,
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
        data_drive_path: Optional[Path] = None
        if data_drive or target.data_image:
            drive_file = Path(data_drive) if data_drive and data_drive != "auto" else out / target.data_image
            data_drive_path = make_data_image(drive_file)
        if name == "image":
            return 0
        firmware = find_firmware(target.architecture)
        qemu = find_tool(target.qemu_system)
        if not firmware or not qemu:
            firmware_name = "AAVMF" if target.architecture == "arm64" else "OVMF"
            raise RuntimeError(f"{target.qemu_system} and {firmware_name} are required")
        enable_gpu = (
            mode == BootMode.DESKTOP
            or name == "run"
            or os.environ.get("FINNOS_GPU", "").strip() in ("1", "true", "yes")
        )
        args = qemu_command(
            qemu, str(firmware), image, headless=name != "run",
            test_exit=mode.test_exit, machine=target.qemu_machine,
            architecture=target.architecture, cpu=target.qemu_cpu,
            data_drive=data_drive_path,
            gpu=enable_gpu,
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
                validate_arm64_desktop
                if target.architecture == "arm64" and mode == BootMode.DESKTOP
                else validate_arm64_init
                if target.architecture == "arm64" and mode == BootMode.INIT
                else validate_arm64_elf_loader
                if target.architecture == "arm64" and mode == BootMode.ELF_LOADER
                else validate_arm64_ipc
                if target.architecture == "arm64" and mode == BootMode.IPC
                else validate_arm64_userspace
                if target.architecture == "arm64" and mode == BootMode.USERSPACE
                else validate_arm64_cooperative_tasks
                if target.architecture == "arm64" and mode == BootMode.COOPERATIVE_TASKS
                else validate_arm64_timer
                if target.architecture == "arm64" and mode == BootMode.TIMER
                else validate_arm64_gic
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
                BootMode.DESKTOP: validate_desktop,
                BootMode.INIT: validate_init,
                BootMode.ELF_LOADER: validate_elf_loader,
                BootMode.IPC: validate_ipc,
                BootMode.USERSPACE: validate_userspace,
                BootMode.COOPERATIVE_TASKS: validate_cooperative_tasks,
                BootMode.PREEMPTION_CONTEXT: validate_preemption_context,
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
        return run_steps(("doctor", "check", "image", "test-boot", "test-exceptions", "test-memory-map", "test-page-allocator", "test-page-tables", "test-heap", "test-timer-interrupts", "test-cooperative-tasks", "test-preemption-context", "test-userspace", "test-ipc", "test-elf-loader", "test-init", "test-desktop"))
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

class ParsedArguments(tuple):
    def __new__(cls, name: str, target: Optional[str], profile: Optional[str], data_drive: Optional[str] = None):
        return super().__new__(cls, (name, target, profile))

    def __init__(self, name: str, target: Optional[str], profile: Optional[str], data_drive: Optional[str] = None):
        self.name = name
        self.target = target
        self.profile = profile
        self.data_drive = data_drive


def parse_arguments(arguments: list[str]) -> ParsedArguments:
    if not arguments:
        return ParsedArguments("help", None, None)
    name = arguments[0]
    target_name: Optional[str] = None
    profile_name: Optional[str] = None
    data_drive: Optional[str] = None
    index = 1
    while index < len(arguments):
        option = arguments[index]
        if option not in ("--target", "--profile", "--data-drive"):
            raise ConfigurationError(f"unknown argument {option!r}")
        if option == "--data-drive":
            if data_drive is not None:
                raise ConfigurationError("--data-drive may be provided only once")
            if index + 1 < len(arguments) and not arguments[index + 1].startswith("--"):
                data_drive = arguments[index + 1]
                index += 2
            else:
                data_drive = "auto"
                index += 1
            continue
        if index + 1 >= len(arguments):
            raise ConfigurationError(f"{option} requires a value")
        value = arguments[index + 1]
        if option == "--target":
            if target_name is not None:
                raise ConfigurationError("--target may be provided only once")
            target_name = value
        elif option == "--profile":
            if profile_name is not None:
                raise ConfigurationError("--profile may be provided only once")
            profile_name = value
        index += 2
    return ParsedArguments(name, target_name, profile_name, data_drive)


def main() -> int:
    try:
        args = parse_arguments(sys.argv[1:])
        return command(args.name, args.target, args.profile, args.data_drive)
    except KeyboardInterrupt: print("\nInterrupted.", file=sys.stderr); return 130
    except subprocess.CalledProcessError as error:
        print(f"error: command failed ({error.returncode}): {_command_text(error.cmd)}", file=sys.stderr)
        _print_captured("stdout", error.stdout)
        _print_captured("stderr", error.stderr)
        return error.returncode or 1
    except subprocess.TimeoutExpired as error: print(f"QEMU timed out after {error.timeout}s", file=sys.stderr); return 1
    except (OSError, RuntimeError) as error: print(f"error: {error}", file=sys.stderr); return 1


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
