"""Delta patching and decompression via zstd (--patch-from / -d)."""

from __future__ import annotations

import os
import shutil
import subprocess
from typing import Optional

from .download import sha256_file


class DeltaError(Exception):
    """Delta application or image decompression error."""


def _find_zstd() -> str:
    zstd_bin = shutil.which("zstd")
    if not zstd_bin:
        raise DeltaError("zstd binary not found in PATH")
    return zstd_bin


def materialise_slot_image(
    artifact_path: str,
    output_image_path: str,
    artifact_meta: dict,
    base_slot_path: Optional[str] = None,
) -> str:
    """Produce a raw, SHA-256-verified slot image at output_image_path.

    Handles:
      - delta artifacts (zstd --patch-from=<base_slot_path>)
      - zstd-compressed full artifacts (zstd -d)
      - uncompressed raw .img artifacts
    Verifies uncompressed_sha256 and uncompressed_size when present.
    """
    os.makedirs(os.path.dirname(os.path.abspath(output_image_path)), exist_ok=True)
    tmp_out = output_image_path + ".tmp"
    if os.path.exists(tmp_out):
        os.remove(tmp_out)

    delta_from = artifact_meta.get("delta_from")
    compression = artifact_meta.get("compression")
    if compression is None:
        if delta_from is not None or artifact_path.endswith(".zst") or artifact_path.endswith(".zstd-delta"):
            compression = "zstd"
        else:
            compression = "none"

    try:
        if delta_from is not None:
            if not base_slot_path or not os.path.exists(base_slot_path):
                raise DeltaError(
                    f"delta artifact requires base slot {base_slot_path!r} (delta_from={delta_from})"
                )
            zstd_bin = _find_zstd()
            proc = subprocess.run(
                [zstd_bin, "-d", "-f", f"--patch-from={base_slot_path}", artifact_path, "-o", tmp_out],
                capture_output=True,
                text=True,
            )
            if proc.returncode != 0:
                raise DeltaError(
                    f"zstd delta patch failed (exit {proc.returncode}): {proc.stderr.strip()}"
                )
        elif compression == "zstd":
            zstd_bin = _find_zstd()
            proc = subprocess.run(
                [zstd_bin, "-d", "-f", artifact_path, "-o", tmp_out],
                capture_output=True,
                text=True,
            )
            if proc.returncode != 0:
                raise DeltaError(
                    f"zstd decompression failed (exit {proc.returncode}): {proc.stderr.strip()}"
                )
        elif compression == "none":
            shutil.copyfile(artifact_path, tmp_out)
        else:
            raise DeltaError(f"unsupported compression type: {compression!r}")

        expected_usz = artifact_meta.get("uncompressed_size")
        if expected_usz is not None:
            actual_usz = os.path.getsize(tmp_out)
            if actual_usz != expected_usz:
                raise DeltaError(
                    f"uncompressed image size mismatch: expected {expected_usz}, got {actual_usz}"
                )

        expected_usha = artifact_meta.get("uncompressed_sha256")
        if expected_usha is not None:
            actual_usha = sha256_file(tmp_out).lower()
            if actual_usha != expected_usha.lower():
                raise DeltaError(
                    f"uncompressed image sha256 mismatch: expected {expected_usha}, got {actual_usha}"
                )

        with open(tmp_out, "rb") as f:
            os.fsync(f.fileno())
        os.replace(tmp_out, output_image_path)
        return output_image_path
    except Exception:
        if os.path.exists(tmp_out):
            os.remove(tmp_out)
        raise
