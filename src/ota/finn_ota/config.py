"""Configuration management for finn-update."""

from __future__ import annotations

import json
import os
import platform
from dataclasses import asdict, dataclass
from typing import Optional

from .rollout import validate_channel

DEFAULT_CONFIG_PATH = "/etc/finn/ota.conf"
DEFAULT_STATE_DIR = "/var/lib/finn/ota"
DEFAULT_PUBKEY_PATH = "/etc/finn/ota/pubkey.pem"
DEFAULT_VERSION_PATH = "/usr/share/finn/version"
DEFAULT_BASE_URL = "https://ota.finnos.org"
DEFAULT_TRIES = 3


def detect_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    if machine in ("aarch64", "arm64"):
        return "aarch64"
    return machine


@dataclass
class OtaConfig:
    channel: str = "stable"
    base_url: str = DEFAULT_BASE_URL
    manifest_url: Optional[str] = None
    auto_check: bool = True
    auto_download: bool = False
    pubkey_path: str = DEFAULT_PUBKEY_PATH
    state_dir: str = DEFAULT_STATE_DIR
    version_path: str = DEFAULT_VERSION_PATH
    machine_id_path: str = "/etc/machine-id"
    arch: str = ""
    backend: str = "auto"
    boot_tries: int = DEFAULT_TRIES

    def __post_init__(self) -> None:
        validate_channel(self.channel)
        if not self.arch:
            self.arch = detect_arch()

    def effective_manifest_url(self, channel_override: Optional[str] = None) -> str:
        ch = validate_channel(channel_override or self.channel)
        if self.manifest_url and channel_override is None:
            return self.manifest_url
        base = self.base_url.rstrip("/")
        return f"{base}/{ch}/{self.arch}/latest.json"


def load_config(path: str = DEFAULT_CONFIG_PATH) -> OtaConfig:
    if not os.path.isfile(path):
        return OtaConfig()
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read().strip()
    if not raw:
        return OtaConfig()

    if raw.lstrip().startswith("{"):
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise ValueError(f"config {path} must be a JSON object")
        known = {k: v for k, v in data.items() if k in OtaConfig.__dataclass_fields__}
        return OtaConfig(**known)

    # Also support simple key=value INI lines without section headers
    parsed: dict[str, object] = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        k, v = [p.strip() for p in line.split("=", 1)]
        if k in ("auto_check", "auto_download"):
            parsed[k] = v.lower() in ("1", "true", "yes", "on")
        elif k == "boot_tries":
            parsed[k] = int(v)
        elif k in OtaConfig.__dataclass_fields__:
            parsed[k] = v if v != "" else None
    return OtaConfig(**parsed)


def save_config(cfg: OtaConfig, path: str = DEFAULT_CONFIG_PATH) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(asdict(cfg), f, indent=2, sort_keys=True)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
