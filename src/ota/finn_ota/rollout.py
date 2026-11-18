"""Channels and deterministic staged-rollout bucketing."""

from __future__ import annotations

import hashlib
import os
from typing import Optional

VALID_CHANNELS = ("stable", "beta", "dev")
DEFAULT_MACHINE_ID_PATH = "/etc/machine-id"


def validate_channel(channel: str) -> str:
    if channel not in VALID_CHANNELS:
        raise ValueError(
            f"invalid channel {channel!r}; expected one of {', '.join(VALID_CHANNELS)}"
        )
    return channel


def read_machine_id(path: str = DEFAULT_MACHINE_ID_PATH) -> Optional[str]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            mid = f.read().strip()
    except OSError:
        return None
    if not mid:
        return None
    return mid


def compute_rollout_bucket(machine_id: str, channel: str, version: str) -> int:
    """Return a deterministic bucket in 0..99 for (machine_id, channel, version)."""
    if not machine_id:
        raise ValueError("machine_id must be non-empty")
    validate_channel(channel)
    if not version:
        raise ValueError("version must be non-empty")
    payload = f"{machine_id}:{channel}:{version}".encode("utf-8")
    digest = hashlib.sha256(payload).digest()
    return int.from_bytes(digest[:4], "big") % 100


def evaluate_rollout(
    rollout_percent: int,
    machine_id: Optional[str],
    channel: str,
    version: str,
    force: bool = False,
) -> tuple[bool, Optional[int], Optional[str]]:
    """Return (eligible, bucket, deferral_reason)."""
    if not isinstance(rollout_percent, int) or isinstance(rollout_percent, bool):
        raise ValueError("rollout must be an integer 0..100")
    if rollout_percent < 0 or rollout_percent > 100:
        raise ValueError(f"rollout must be between 0 and 100, got {rollout_percent}")

    bucket: Optional[int] = None
    if machine_id:
        bucket = compute_rollout_bucket(machine_id, channel, version)

    if force:
        return True, bucket, None
    if rollout_percent == 0:
        return False, bucket, "rollout_paused"
    if rollout_percent == 100:
        return True, bucket, None
    if bucket is None:
        return False, None, "missing_machine_id"
    if bucket < rollout_percent:
        return True, bucket, None
    return False, bucket, "staged_rollout"
