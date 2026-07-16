"""Validated FinnOS target and profile configuration."""
from __future__ import annotations

import ast
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


class ConfigurationError(RuntimeError):
    """Raised when Finnfile or target metadata is invalid."""


@dataclass(frozen=True)
class BuildProfile:
    name: str
    cargo_profile: str

    @property
    def cargo_args(self) -> tuple[str, ...]:
        return () if self.cargo_profile == "debug" else ("--release",)


@dataclass(frozen=True)
class BuildTarget:
    name: str
    architecture: str
    platform: str
    firmware: str
    status: str
    bootable: bool
    kernel_package: str
    kernel_binary: str
    kernel_cargo_target: str
    boot_package: str
    boot_binary: str
    boot_cargo_target: str
    boot_filename: str
    kernel_filename: str
    image_filename: str
    qemu_system: str
    qemu_machine: str


@dataclass(frozen=True)
class BuildConfiguration:
    default_target: str
    targets: dict[str, BuildTarget]
    profiles: dict[str, BuildProfile]

    def select(self, target_name: Optional[str], profile_name: Optional[str]) -> tuple[BuildTarget, BuildProfile]:
        target_key = target_name or self.default_target
        profile_key = profile_name or "development"
        try:
            target = self.targets[target_key]
        except KeyError as error:
            raise ConfigurationError(
                f"unknown target {target_key!r}; choose one of: {', '.join(sorted(self.targets))}"
            ) from error
        try:
            profile = self.profiles[profile_key]
        except KeyError as error:
            raise ConfigurationError(
                f"unknown profile {profile_key!r}; choose one of: {', '.join(sorted(self.profiles))}"
            ) from error
        if not target.bootable:
            raise ConfigurationError(
                f"target {target.name!r} is {target.status} and is not bootable"
            )
        return target, profile


def load_configuration(root: Path) -> BuildConfiguration:
    finnfile = _load_toml(root / "Finnfile.toml")
    project = _table(finnfile, "project", root / "Finnfile.toml")
    default_target = _string(project, "default_target", "[project]")
    raw_targets = _table(finnfile, "targets", root / "Finnfile.toml")
    targets: dict[str, BuildTarget] = {}
    for name, metadata in raw_targets.items():
        if not isinstance(metadata, dict):
            raise ConfigurationError(f"[targets.{name}] must be a table")
        configuration_path = (root / _string(
            metadata, "configuration", f"[targets.{name}]"
        )).resolve()
        if root.resolve() not in configuration_path.parents:
            raise ConfigurationError(f"[targets.{name}].configuration must stay inside the repository")
        target_data = _load_toml(configuration_path)
        targets[name] = _target(name, metadata, target_data, configuration_path)

    raw_profiles = _table(finnfile, "profiles", root / "Finnfile.toml")
    profiles: dict[str, BuildProfile] = {}
    for name, metadata in raw_profiles.items():
        if not isinstance(metadata, dict):
            raise ConfigurationError(f"[profiles.{name}] must be a table")
        cargo_profile = _string(metadata, "cargo_profile", f"[profiles.{name}]")
        if cargo_profile not in ("debug", "release"):
            raise ConfigurationError(
                f"[profiles.{name}].cargo_profile must be 'debug' or 'release'"
            )
        profiles[name] = BuildProfile(name=name, cargo_profile=cargo_profile)

    if default_target not in targets:
        raise ConfigurationError(f"default target {default_target!r} is not defined")
    if "development" not in profiles:
        raise ConfigurationError("profile 'development' is required")
    return BuildConfiguration(default_target, targets, profiles)


def _target(name: str, metadata: dict[str, Any], data: dict[str, Any], path: Path) -> BuildTarget:
    context = str(path)
    if _string(data, "name", context) != name:
        raise ConfigurationError(f"{context}: target name does not match {name!r}")
    for key in ("architecture", "platform", "firmware", "status", "bootable"):
        if key not in data:
            raise ConfigurationError(f"{context} is missing {key!r}")
    for key in ("architecture", "platform", "firmware", "status", "bootable"):
        if key in metadata and metadata[key] != data.get(key):
            raise ConfigurationError(f"[targets.{name}].{key} disagrees with {context}")
    bootable = data["bootable"]
    if not isinstance(bootable, bool):
        raise ConfigurationError(f"{context}: bootable must be a boolean")
    target = BuildTarget(
        name=name,
        architecture=_string(data, "architecture", context),
        platform=_string(data, "platform", context),
        firmware=_string(data, "firmware", context),
        status=_string(data, "status", context),
        bootable=bootable,
        kernel_package=_string(data, "kernel_package", context, required=bootable),
        kernel_binary=_string(data, "kernel_binary", context, required=bootable),
        kernel_cargo_target=_string(data, "kernel_cargo_target", context, required=bootable),
        boot_package=_string(data, "boot_package", context, required=bootable),
        boot_binary=_string(data, "boot_binary", context, required=bootable),
        boot_cargo_target=_string(data, "boot_cargo_target", context, required=bootable),
        boot_filename=_string(data, "boot_filename", context, required=bootable),
        kernel_filename=_string(data, "kernel_filename", context, required=bootable),
        image_filename=_string(data, "image_filename", context, required=bootable),
        qemu_system=_string(data, "qemu_system", context, required=bootable),
        qemu_machine=_string(data, "qemu_machine", context, required=bootable),
    )
    for field_name in ("boot_filename", "kernel_filename", "image_filename"):
        value = getattr(target, field_name)
        if value and Path(value).name != value:
            raise ConfigurationError(f"{context}: {field_name} must be a filename")
    return target


def _load_toml(path: Path) -> dict[str, Any]:
    try:
        import tomllib  # type: ignore[import-not-found]
    except ImportError:
        return _load_simple_toml(path)
    try:
        with path.open("rb") as stream:
            return tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ConfigurationError(f"cannot load {path}: {error}") from error


def _load_simple_toml(path: Path) -> dict[str, Any]:
    """Parse the scalar/table TOML subset used by FinnOS on Python 3.9/3.10."""
    result: dict[str, Any] = {}
    current = result
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ConfigurationError(f"cannot load {path}: {error}") from error
    for line_number, raw_line in enumerate(lines, 1):
        line = _strip_comment(raw_line).strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            current = result
            for part in line[1:-1].split("."):
                if not part:
                    raise ConfigurationError(f"{path}:{line_number}: empty table component")
                value = current.setdefault(part, {})
                if not isinstance(value, dict):
                    raise ConfigurationError(f"{path}:{line_number}: duplicate table {line}")
                current = value
            continue
        if "=" not in line:
            raise ConfigurationError(f"{path}:{line_number}: expected key = value")
        key, raw_value = (part.strip() for part in line.split("=", 1))
        if not key or key in current:
            raise ConfigurationError(f"{path}:{line_number}: invalid or duplicate key {key!r}")
        if raw_value in ("true", "false"):
            value: Any = raw_value == "true"
        else:
            try:
                value = ast.literal_eval(raw_value)
            except (SyntaxError, ValueError) as error:
                raise ConfigurationError(f"{path}:{line_number}: invalid scalar value") from error
        if not isinstance(value, (str, int, bool)):
            raise ConfigurationError(f"{path}:{line_number}: unsupported value type")
        current[key] = value
    return result


def _strip_comment(line: str) -> str:
    quote = ""
    escaped = False
    for index, character in enumerate(line):
        if escaped:
            escaped = False
            continue
        if character == "\\" and quote == '"':
            escaped = True
            continue
        if character in ("'", '"'):
            if not quote:
                quote = character
            elif quote == character:
                quote = ""
            continue
        if character == "#" and not quote:
            return line[:index]
    return line


def _table(data: dict[str, Any], key: str, path: Path) -> dict[str, Any]:
    value = data.get(key)
    if not isinstance(value, dict):
        raise ConfigurationError(f"{path}: [{key}] table is required")
    return value


def _string(data: dict[str, Any], key: str, context: str, required: bool = True) -> str:
    value = data.get(key)
    if value is None and not required:
        return ""
    if not isinstance(value, str) or not value:
        raise ConfigurationError(f"{context}: {key} must be a non-empty string")
    return value
