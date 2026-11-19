"""Pluggable A/B slot and bootloader state backends for finn-update.

Supports:
  - FakeBootBackend: file-backed slots + bootloader simulator for host tests
  - SystemdBootBackend: systemd-boot BLS entries with +tries boot assessment
  - GrubBootBackend: GRUB2 grubenv boot-counting and automatic fallback
"""

from __future__ import annotations

import abc
import json
import os
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass, field
from typing import Dict, List, Optional

DEFAULT_SLOT_A = "/dev/disk/by-partlabel/FINNOS_A"
DEFAULT_SLOT_B = "/dev/disk/by-partlabel/FINNOS_B"
_BLS_TRIES_RE = re.compile(r"^finnos-([ab])(?:\+(\d+)(?:-(\d+))?)?\.conf$")
_CMDLINE_SLOT_RE = re.compile(r"(?:^|\s)finn\.slot=([ab])(?:\s|$)")
_CMDLINE_PARTLABEL_RE = re.compile(r"(?:^|\s)root=PARTLABEL=FINNOS_([AB])(?:\s|$)")


class BackendError(Exception):
    """Bootloader or slot staging failure."""


@dataclass
class SlotStatus:
    backend: str
    layout: str  # "ab" or "single"
    active_slot: Optional[str]
    booted_slot: Optional[str]
    inactive_slot: Optional[str]
    pending_slot: Optional[str]
    tries_left: int
    slot_committed: bool
    slot_versions: Dict[str, Optional[str]] = field(default_factory=lambda: {"a": None, "b": None})
    slot_devices: Dict[str, Optional[str]] = field(default_factory=lambda: {"a": None, "b": None})
    notes: Optional[str] = None


def other_slot(slot: str) -> str:
    if slot == "a":
        return "b"
    if slot == "b":
        return "a"
    raise BackendError(f"invalid slot name: {slot!r}")


def read_booted_slot_from_cmdline(cmdline_path: str = "/proc/cmdline") -> Optional[str]:
    try:
        with open(cmdline_path, "r", encoding="utf-8") as f:
            text = f.read().strip()
    except OSError:
        return None
    m = _CMDLINE_SLOT_RE.search(text)
    if m:
        return m.group(1)
    m = _CMDLINE_PARTLABEL_RE.search(text)
    if m:
        return m.group(1).lower()
    return None


def _read_version_file(version_path: str) -> Optional[str]:
    try:
        with open(version_path, "r", encoding="utf-8") as f:
            val = f.read().strip()
        return val or None
    except OSError:
        return None


def _current_root_source() -> Optional[str]:
    try:
        proc = subprocess.run(
            ["findmnt", "-no", "SOURCE", "-T", "/"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if proc.returncode == 0 and proc.stdout.strip():
            return os.path.realpath(proc.stdout.strip())
    except (OSError, subprocess.TimeoutExpired):
        pass
    return None


def _write_slot_image(target_dev: str, image_path: str) -> None:
    """Stream image_path to block device or slot file safely with fsync."""
    if not os.path.isfile(image_path):
        raise BackendError(f"staged image does not exist: {image_path}")
    target_real = os.path.realpath(target_dev)
    root_src = _current_root_source()
    if root_src and target_real == root_src:
        raise BackendError(
            f"refusing to write to currently mounted root device {target_real}"
        )

    # For regular files (tests / file-backed), write atomically via temp or in-place
    if os.path.isfile(target_dev) or not os.path.exists(target_dev):
        os.makedirs(os.path.dirname(os.path.abspath(target_dev)), exist_ok=True)
        tmp = target_dev + ".staging"
        shutil.copyfile(image_path, tmp)
        with open(tmp, "rb") as f:
            os.fsync(f.fileno())
        os.replace(tmp, target_dev)
        return

    # Block device: stream chunks directly and fsync
    with open(image_path, "rb") as src, open(target_dev, "r+b") as dst:
        while True:
            buf = src.read(1024 * 1024)
            if not buf:
                break
            dst.write(buf)
        dst.flush()
        os.fsync(dst.fileno())


class BootBackend(abc.ABC):
    name: str = "base"

    @abc.abstractmethod
    def get_status(self) -> SlotStatus:
        """Return current slot and boot-assessment state."""

    @abc.abstractmethod
    def get_slot_device(self, slot: str) -> Optional[str]:
        """Return path to the block device or file backing `slot` ('a' or 'b')."""

    @abc.abstractmethod
    def stage_image(self, slot: str, image_path: str, version: str) -> None:
        """Write a verified slot image to the inactive `slot`."""

    @abc.abstractmethod
    def mark_bootable_once(self, slot: str, tries: int = 3, version: Optional[str] = None) -> None:
        """Arm `slot` for trial boot with `tries` attempts."""

    @abc.abstractmethod
    def commit_booted_slot(self) -> str:
        """Commit the currently booted slot as known-good active slot."""

    @abc.abstractmethod
    def rollback(self, reason: str = "") -> str:
        """Revert boot selection to the previous known-good slot."""


class FakeBootBackend(BootBackend):
    """File-backed A/B slot backend and bootloader simulator for host testing."""

    name = "fake"

    def __init__(self, state_dir: str, initial_version: str = "0.2.0") -> None:
        self.state_dir = os.path.abspath(state_dir)
        self.slots_dir = os.path.join(self.state_dir, "slots")
        self.state_file = os.path.join(self.state_dir, "fake_backend.json")
        self.initial_version = initial_version
        self._ensure_initialized()

    def _ensure_initialized(self) -> None:
        os.makedirs(self.slots_dir, exist_ok=True)
        for s in ("a", "b"):
            p = self.get_slot_device(s)
            if p and not os.path.exists(p):
                with open(p, "wb") as f:
                    f.write(b"")
        if not os.path.exists(self.state_file):
            state = {
                "active_slot": "a",
                "booted_slot": "a",
                "pending_slot": None,
                "tries_left": 0,
                "committed": True,
                "slot_versions": {"a": self.initial_version, "b": None},
                "staged_versions": {"a": None, "b": None},
                "boot_history": [],
            }
            self._save_state(state)

    def _load_state(self) -> dict:
        with open(self.state_file, "r", encoding="utf-8") as f:
            return json.load(f)

    def _save_state(self, state: dict) -> None:
        tmp = self.state_file + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2, sort_keys=True)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, self.state_file)

    def get_slot_device(self, slot: str) -> str:
        if slot not in ("a", "b"):
            raise BackendError(f"invalid slot: {slot!r}")
        return os.path.join(self.slots_dir, f"{slot}.img")

    def get_status(self) -> SlotStatus:
        st = self._load_state()
        booted = st["booted_slot"]
        return SlotStatus(
            backend=self.name,
            layout="ab",
            active_slot=st["active_slot"],
            booted_slot=booted,
            inactive_slot=other_slot(booted),
            pending_slot=st["pending_slot"],
            tries_left=int(st["tries_left"]),
            slot_committed=bool(st["committed"]),
            slot_versions=dict(st["slot_versions"]),
            slot_devices={"a": self.get_slot_device("a"), "b": self.get_slot_device("b")},
        )

    def stage_image(self, slot: str, image_path: str, version: str) -> None:
        st = self._load_state()
        if slot not in ("a", "b"):
            raise BackendError(f"invalid slot: {slot!r}")
        if slot == st["booted_slot"]:
            raise BackendError(f"refusing to stage onto currently booted slot {slot!r}")
        target = self.get_slot_device(slot)
        _write_slot_image(target, image_path)
        st.setdefault("staged_versions", {"a": None, "b": None})[slot] = version
        self._save_state(st)

    def mark_bootable_once(self, slot: str, tries: int = 3, version: Optional[str] = None) -> None:
        if slot not in ("a", "b"):
            raise BackendError(f"invalid slot: {slot!r}")
        if tries < 1:
            raise BackendError("tries must be >= 1")
        st = self._load_state()
        if slot == st["booted_slot"]:
            raise BackendError(f"slot {slot!r} is already booted")
        st["pending_slot"] = slot
        st["tries_left"] = int(tries)
        st["committed"] = False
        if version is not None:
            st.setdefault("staged_versions", {"a": None, "b": None})[slot] = version
        self._save_state(st)

    def simulate_boot(self) -> str:
        """Simulate firmware/bootloader power-on boot counting and rollback."""
        st = self._load_state()
        pending = st.get("pending_slot")
        tries = int(st.get("tries_left", 0))
        if pending in ("a", "b"):
            if tries > 0:
                st["tries_left"] = tries - 1
                st["booted_slot"] = pending
                st["committed"] = False
                st["boot_history"].append(
                    {"action": "trial_boot", "slot": pending, "tries_remaining": st["tries_left"]}
                )
            else:
                # Boot tries exhausted without health commit -> automatic rollback!
                prev = st["active_slot"]
                st["pending_slot"] = None
                st["tries_left"] = 0
                st["booted_slot"] = prev
                st["committed"] = True
                st["boot_history"].append(
                    {"action": "auto_rollback", "failed_slot": pending, "restored_slot": prev}
                )
        else:
            st["booted_slot"] = st["active_slot"]
            st["committed"] = True
            st["boot_history"].append({"action": "normal_boot", "slot": st["active_slot"]})

        self._save_state(st)
        return st["booted_slot"]

    def commit_booted_slot(self) -> str:
        st = self._load_state()
        booted = st["booted_slot"]
        st["active_slot"] = booted
        staged = st.get("staged_versions", {}).get(booted)
        if staged:
            st["slot_versions"][booted] = staged
            st["staged_versions"][booted] = None
        st["pending_slot"] = None
        st["tries_left"] = 0
        st["committed"] = True
        self._save_state(st)
        return booted

    def rollback(self, reason: str = "") -> str:
        st = self._load_state()
        pending = st.get("pending_slot")
        if pending:
            target = st["active_slot"]
            st["pending_slot"] = None
            st["tries_left"] = 0
            st["boot_history"].append(
                {"action": "manual_rollback", "from": pending, "to": target, "reason": reason}
            )
            self._save_state(st)
            return target

        prev = other_slot(st["active_slot"])
        if not st["slot_versions"].get(prev):
            raise BackendError(f"cannot rollback: alternate slot {prev!r} has no recorded version")
        st["active_slot"] = prev
        st["pending_slot"] = None
        st["tries_left"] = 0
        st["boot_history"].append(
            {"action": "manual_rollback", "from": st["booted_slot"], "to": prev, "reason": reason}
        )
        self._save_state(st)
        return prev


class SystemdBootBackend(BootBackend):
    """systemd-boot BLS Automatic Boot Assessment (+tries) backend."""

    name = "systemd-boot"

    def __init__(
        self,
        boot_dir: str = "/boot",
        cmdline_path: str = "/proc/cmdline",
        version_path: str = "/usr/share/finn/version",
        slot_a: str = DEFAULT_SLOT_A,
        slot_b: str = DEFAULT_SLOT_B,
        bootctl_bin: str = "bootctl",
        state_dir: str = "/var/lib/finn/ota",
    ) -> None:
        self.boot_dir = boot_dir
        self.entries_dir = os.path.join(boot_dir, "loader", "entries")
        self.cmdline_path = cmdline_path
        self.version_path = version_path
        self.slot_devices_cfg = {"a": slot_a, "b": slot_b}
        self.bootctl_bin = bootctl_bin
        self.state_dir = state_dir

    def _has_ab_layout(self) -> bool:
        return os.path.exists(self.slot_devices_cfg["a"]) and os.path.exists(
            self.slot_devices_cfg["b"]
        )

    def get_slot_device(self, slot: str) -> Optional[str]:
        if slot not in ("a", "b"):
            raise BackendError(f"invalid slot: {slot!r}")
        dev = self.slot_devices_cfg[slot]
        return dev if os.path.exists(dev) else None

    def _scan_entries(self) -> Dict[str, dict]:
        found: Dict[str, dict] = {}
        if not os.path.isdir(self.entries_dir):
            return found
        for fname in sorted(os.listdir(self.entries_dir)):
            m = _BLS_TRIES_RE.match(fname)
            if not m:
                continue
            slot = m.group(1)
            tries = int(m.group(2)) if m.group(2) is not None else None
            done = int(m.group(3)) if m.group(3) is not None else 0
            found[slot] = {
                "filename": fname,
                "path": os.path.join(self.entries_dir, fname),
                "tries": tries,
                "done": done,
            }
        return found

    def _load_meta(self) -> dict:
        p = os.path.join(self.state_dir, "slots_meta.json")
        if os.path.isfile(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    return json.load(f)
            except (OSError, json.JSONDecodeError):
                pass
        return {"slot_versions": {"a": None, "b": None}, "active_slot": None}

    def _save_meta(self, meta: dict) -> None:
        os.makedirs(self.state_dir, exist_ok=True)
        p = os.path.join(self.state_dir, "slots_meta.json")
        tmp = p + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2, sort_keys=True)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, p)

    def get_status(self) -> SlotStatus:
        ab = self._has_ab_layout()
        booted = read_booted_slot_from_cmdline(self.cmdline_path)
        entries = self._scan_entries()
        meta = self._load_meta()
        versions = dict(meta.get("slot_versions", {"a": None, "b": None}))

        if not ab:
            cur_ver = _read_version_file(self.version_path)
            return SlotStatus(
                backend=self.name,
                layout="single",
                active_slot=None,
                booted_slot=None,
                inactive_slot=None,
                pending_slot=None,
                tries_left=0,
                slot_committed=True,
                slot_versions={"a": cur_ver, "b": None},
                slot_devices={"a": None, "b": None},
                notes="Single-partition install detected; A/B slots FINNOS_A/FINNOS_B are not present.",
            )

        if booted is None:
            booted = meta.get("active_slot") or "a"
        if booted and not versions.get(booted):
            versions[booted] = _read_version_file(self.version_path)

        pending_slot = None
        tries_left = 0
        for s, info in entries.items():
            if info["tries"] is not None:
                pending_slot = s
                tries_left = info["tries"]

        active = meta.get("active_slot")
        if active is None:
            for s, info in entries.items():
                if info["tries"] is None:
                    active = s
                    break
        if active is None:
            active = booted

        committed = pending_slot != booted and (
            booted not in entries or entries[booted]["tries"] is None
        )

        return SlotStatus(
            backend=self.name,
            layout="ab",
            active_slot=active,
            booted_slot=booted,
            inactive_slot=other_slot(booted),
            pending_slot=pending_slot,
            tries_left=tries_left,
            slot_committed=committed,
            slot_versions=versions,
            slot_devices={
                "a": self.get_slot_device("a"),
                "b": self.get_slot_device("b"),
            },
        )

    def stage_image(self, slot: str, image_path: str, version: str) -> None:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot stage A/B image on single-slot installation")
        if slot == st.booted_slot:
            raise BackendError(f"refusing to overwrite currently booted slot {slot!r}")
        dev = self.get_slot_device(slot)
        if not dev:
            raise BackendError(f"slot {slot!r} block device not found")
        _write_slot_image(dev, image_path)
        meta = self._load_meta()
        meta.setdefault("staged_versions", {})[slot] = version
        self._save_meta(meta)

    def _run_bootctl(self, *args: str) -> None:
        if not (shutil.which(self.bootctl_bin) or (os.path.isabs(self.bootctl_bin) and os.path.isfile(self.bootctl_bin))):
            raise BackendError(f"{self.bootctl_bin} not found")
        proc = subprocess.run(
            [self.bootctl_bin, f"--esp-path={self.boot_dir}", *args],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            raise BackendError(
                f"bootctl {' '.join(args)} failed: {proc.stderr.strip() or proc.stdout.strip()}"
            )

    def mark_bootable_once(self, slot: str, tries: int = 3, version: Optional[str] = None) -> None:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot arm A/B boot on single-slot installation")
        if slot not in ("a", "b") or slot == st.booted_slot:
            raise BackendError(f"invalid target slot for trial boot: {slot!r}")
        if tries < 1:
            raise BackendError("tries must be >= 1")

        os.makedirs(self.entries_dir, exist_ok=True)
        entries = self._scan_entries()
        target_name = f"finnos-{slot}+{tries}.conf"
        target_path = os.path.join(self.entries_dir, target_name)

        if slot in entries:
            if entries[slot]["path"] != target_path:
                os.replace(entries[slot]["path"], target_path)
        else:
            partlabel = f"FINNOS_{slot.upper()}"
            with open(target_path, "w", encoding="utf-8") as f:
                f.write(
                    f"title FinnOS ({slot.upper()})\n"
                    f"linux /finnos/{slot}/vmlinuz\n"
                    f"initrd /finnos/{slot}/initramfs.img\n"
                    f"options root=PARTLABEL={partlabel} rw finn.slot={slot} "
                    f"quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles\n"
                )
        self._run_bootctl("set-oneshot", target_name)
        meta = self._load_meta()
        if version:
            meta.setdefault("staged_versions", {})[slot] = version
        self._save_meta(meta)

    def commit_booted_slot(self) -> str:
        st = self.get_status()
        if st.layout != "ab" or not st.booted_slot:
            raise BackendError("cannot commit slot on non-A/B layout")
        slot = st.booted_slot
        entries = self._scan_entries()
        blessed_name = f"finnos-{slot}.conf"
        blessed_path = os.path.join(self.entries_dir, blessed_name)
        if slot in entries and entries[slot]["path"] != blessed_path:
            os.replace(entries[slot]["path"], blessed_path)
        if os.path.exists(blessed_path):
            self._run_bootctl("set-default", blessed_name)

        meta = self._load_meta()
        meta["active_slot"] = slot
        staged = meta.get("staged_versions", {}).pop(slot, None)
        if staged:
            meta.setdefault("slot_versions", {})[slot] = staged
        elif not meta.get("slot_versions", {}).get(slot):
            meta.setdefault("slot_versions", {})[slot] = _read_version_file(self.version_path)
        self._save_meta(meta)
        return slot

    def rollback(self, reason: str = "") -> str:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot rollback on single-slot layout")
        entries = self._scan_entries()
        if st.pending_slot and st.pending_slot in entries:
            # Disarm pending trial boot and revert default to active slot
            pending = st.pending_slot
            clean_pending = os.path.join(self.entries_dir, f"finnos-{pending}.conf")
            if entries[pending]["path"] != clean_pending:
                os.replace(entries[pending]["path"], clean_pending)
            target = st.active_slot or other_slot(pending)
        else:
            target = other_slot(st.booted_slot or st.active_slot or "a")

        target_name = f"finnos-{target}.conf"
        if target in entries and entries[target]["path"] != os.path.join(self.entries_dir, target_name):
            os.replace(entries[target]["path"], os.path.join(self.entries_dir, target_name))
        if os.path.exists(os.path.join(self.entries_dir, target_name)):
            self._run_bootctl("set-default", target_name)

        meta = self._load_meta()
        meta["active_slot"] = target
        self._save_meta(meta)
        return target


class GrubBootBackend(BootBackend):
    """GRUB2 grubenv-backed A/B slot backend."""

    name = "grub"

    def __init__(
        self,
        boot_dir: str = "/boot",
        cmdline_path: str = "/proc/cmdline",
        version_path: str = "/usr/share/finn/version",
        slot_a: str = DEFAULT_SLOT_A,
        slot_b: str = DEFAULT_SLOT_B,
        grub_editenv_bin: str = "grub-editenv",
    ) -> None:
        self.boot_dir = boot_dir
        self.grubenv_path = os.path.join(boot_dir, "grub", "grubenv")
        self.cmdline_path = cmdline_path
        self.version_path = version_path
        self.slot_devices_cfg = {"a": slot_a, "b": slot_b}
        self.grub_editenv_bin = grub_editenv_bin

    def _has_ab_layout(self) -> bool:
        return os.path.exists(self.slot_devices_cfg["a"]) and os.path.exists(
            self.slot_devices_cfg["b"]
        )

    def get_slot_device(self, slot: str) -> Optional[str]:
        if slot not in ("a", "b"):
            raise BackendError(f"invalid slot: {slot!r}")
        dev = self.slot_devices_cfg[slot]
        return dev if os.path.exists(dev) else None

    def _read_grubenv(self) -> Dict[str, str]:
        env: Dict[str, str] = {}
        if not os.path.isfile(self.grubenv_path):
            return env
        with open(self.grubenv_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                env[k] = v
        return env

    def _write_grubenv_vars(self, updates: Dict[str, str]) -> None:
        if shutil.which(self.grub_editenv_bin) or (os.path.isabs(self.grub_editenv_bin) and os.path.isfile(self.grub_editenv_bin)):
            args = [self.grub_editenv_bin, self.grubenv_path, "set"]
            args.extend(f"{k}={v}" for k, v in updates.items())
            proc = subprocess.run(args, capture_output=True, text=True)
            if proc.returncode != 0:
                raise BackendError(
                    f"grub-editenv failed: {proc.stderr.strip() or proc.stdout.strip()}"
                )
            return

        # Spec-compliant 1024-byte GRUB environment block writer when grub-editenv is absent
        env = self._read_grubenv()
        env.update(updates)
        header = "# GRUB Environment Block\n"
        body = "".join(f"{k}={v}\n" for k, v in sorted(env.items()))
        raw = (header + body).encode("utf-8")
        if len(raw) > 1024:
            raise BackendError("grubenv block exceeds 1024 bytes")
        padded = raw + b"#" * (1024 - len(raw))
        os.makedirs(os.path.dirname(self.grubenv_path), exist_ok=True)
        tmp = self.grubenv_path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(padded)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, self.grubenv_path)

    def get_status(self) -> SlotStatus:
        ab = self._has_ab_layout()
        env = self._read_grubenv()
        booted = read_booted_slot_from_cmdline(self.cmdline_path)
        if not ab:
            cur_ver = _read_version_file(self.version_path)
            return SlotStatus(
                backend=self.name,
                layout="single",
                active_slot=None,
                booted_slot=None,
                inactive_slot=None,
                pending_slot=None,
                tries_left=0,
                slot_committed=True,
                slot_versions={"a": cur_ver, "b": None},
                slot_devices={"a": None, "b": None},
                notes="Single-partition install detected; A/B slots FINNOS_A/FINNOS_B are not present.",
            )

        active = env.get("finn_active_slot") or "a"
        if booted is None:
            booted = active
        pending = env.get("finn_pending_slot") or None
        tries_raw = env.get("finn_tries_left", "0")
        tries = int(tries_raw) if tries_raw.isdigit() else 0

        ver_a = env.get("finn_version_a") or None
        ver_b = env.get("finn_version_b") or None
        versions = {"a": ver_a, "b": ver_b}
        if booted in versions and not versions[booted]:
            versions[booted] = _read_version_file(self.version_path)

        committed = pending != booted
        return SlotStatus(
            backend=self.name,
            layout="ab",
            active_slot=active,
            booted_slot=booted,
            inactive_slot=other_slot(booted),
            pending_slot=pending,
            tries_left=tries,
            slot_committed=committed,
            slot_versions=versions,
            slot_devices={
                "a": self.get_slot_device("a"),
                "b": self.get_slot_device("b"),
            },
        )

    def stage_image(self, slot: str, image_path: str, version: str) -> None:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot stage A/B image on single-slot installation")
        if slot == st.booted_slot:
            raise BackendError(f"refusing to overwrite currently booted slot {slot!r}")
        dev = self.get_slot_device(slot)
        if not dev:
            raise BackendError(f"slot {slot!r} block device not found")
        _write_slot_image(dev, image_path)
        self._write_grubenv_vars({f"finn_staged_version_{slot}": version})

    def mark_bootable_once(self, slot: str, tries: int = 3, version: Optional[str] = None) -> None:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot arm A/B boot on single-slot installation")
        if slot not in ("a", "b") or slot == st.booted_slot:
            raise BackendError(f"invalid target slot for trial boot: {slot!r}")
        if tries < 1:
            raise BackendError("tries must be >= 1")
        updates = {
            "finn_active_slot": st.active_slot or st.booted_slot or "a",
            "finn_pending_slot": slot,
            "finn_tries_left": str(tries),
        }
        if version is not None:
            updates[f"finn_staged_version_{slot}"] = version
        self._write_grubenv_vars(updates)

    def commit_booted_slot(self) -> str:
        st = self.get_status()
        if st.layout != "ab" or not st.booted_slot:
            raise BackendError("cannot commit slot on non-A/B layout")
        env = self._read_grubenv()
        booted = st.booted_slot
        updates = {
            "finn_active_slot": booted,
            "finn_pending_slot": "",
            "finn_tries_left": "0",
        }
        staged_ver = env.get(f"finn_staged_version_{booted}") or _read_version_file(self.version_path)
        if staged_ver:
            updates[f"finn_version_{booted}"] = staged_ver
            updates[f"finn_staged_version_{booted}"] = ""
        self._write_grubenv_vars(updates)
        return booted

    def rollback(self, reason: str = "") -> str:
        st = self.get_status()
        if st.layout != "ab":
            raise BackendError("cannot rollback on single-slot layout")
        if st.pending_slot:
            target = st.active_slot or other_slot(st.pending_slot)
        else:
            target = other_slot(st.booted_slot or st.active_slot or "a")
            if not st.slot_versions.get(target):
                raise BackendError(
                    f"cannot rollback: alternate slot {target!r} has no recorded version"
                )
        self._write_grubenv_vars(
            {
                "finn_active_slot": target,
                "finn_pending_slot": "",
                "finn_tries_left": "0",
            }
        )
        return target


def select_backend(
    backend_name: str,
    state_dir: str,
    version_path: str = "/usr/share/finn/version",
    boot_dir: str = "/boot",
) -> BootBackend:
    if backend_name == "fake":
        cur_ver = _read_version_file(version_path) or "0.2.0"
        return FakeBootBackend(state_dir=state_dir, initial_version=cur_ver)
    if backend_name == "systemd-boot":
        return SystemdBootBackend(boot_dir=boot_dir, version_path=version_path, state_dir=state_dir)
    if backend_name == "grub":
        return GrubBootBackend(boot_dir=boot_dir, version_path=version_path)
    if backend_name == "auto":
        if os.path.isdir(os.path.join(boot_dir, "loader", "entries")) and (
            os.path.isdir("/sys/firmware/efi/efivars")
            or any(
                f.startswith("finnos-")
                for f in os.listdir(os.path.join(boot_dir, "loader", "entries"))
            )
        ):
            return SystemdBootBackend(boot_dir=boot_dir, version_path=version_path, state_dir=state_dir)
        return GrubBootBackend(boot_dir=boot_dir, version_path=version_path)
    raise BackendError(f"unknown backend: {backend_name!r}")
