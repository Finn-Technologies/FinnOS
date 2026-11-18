"""Signed OTA manifest schema, canonical serialization, and artifact selection."""

from __future__ import annotations

import base64
import copy
import hashlib
import json
import os
from typing import Any, Dict, List, Optional

from . import ed25519
from .rollout import validate_channel
from .version import SemVer

SCHEMA_VERSION = 1
VALID_ARCHES = ("x86_64", "aarch64")


class ManifestError(Exception):
    """Manifest parsing or schema validation error."""


class VerificationError(Exception):
    """Cryptographic signature or digest verification failure."""


def canonical_manifest_bytes(manifest: Dict[str, Any]) -> bytes:
    """Serialize manifest without 'signature' into canonical UTF-8 JSON bytes."""
    payload = {k: v for k, v in manifest.items() if k != "signature"}
    return json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _is_hex_sha256(val: Any) -> bool:
    if not isinstance(val, str) or len(val) != 64:
        return False
    try:
        int(val, 16)
    except ValueError:
        return False
    return True


def validate_manifest_schema(manifest: Any) -> Dict[str, Any]:
    """Validate structure and types of a parsed OTA manifest."""
    if not isinstance(manifest, dict):
        raise ManifestError("manifest root must be a JSON object")

    schema_ver = manifest.get("schema_version", 1)
    if schema_ver != SCHEMA_VERSION:
        raise ManifestError(f"unsupported manifest schema_version: {schema_ver!r}")

    for field in ("version", "channel", "min_version", "rollout", "artifacts", "signature"):
        if field not in manifest:
            raise ManifestError(f"manifest missing required field: {field}")

    try:
        SemVer.parse(str(manifest["version"]))
        SemVer.parse(str(manifest["min_version"]))
    except ValueError as exc:
        raise ManifestError(str(exc)) from exc

    try:
        validate_channel(str(manifest["channel"]))
    except ValueError as exc:
        raise ManifestError(str(exc)) from exc

    arch = manifest.get("arch")
    if arch is not None and arch not in VALID_ARCHES:
        raise ManifestError(f"unsupported manifest arch: {arch!r}")

    rollout = manifest["rollout"]
    if not isinstance(rollout, int) or isinstance(rollout, bool) or not (0 <= rollout <= 100):
        raise ManifestError(f"manifest rollout must be integer 0..100, got {rollout!r}")

    artifacts = manifest["artifacts"]
    if not isinstance(artifacts, list) or not artifacts:
        raise ManifestError("manifest artifacts must be a non-empty list")

    has_full = False
    for idx, art in enumerate(artifacts):
        if not isinstance(art, dict):
            raise ManifestError(f"artifact[{idx}] must be an object")
        for key in ("url", "sha256", "size", "delta_from"):
            if key not in art:
                raise ManifestError(f"artifact[{idx}] missing required field: {key}")
        if not isinstance(art["url"], str) or not art["url"]:
            raise ManifestError(f"artifact[{idx}].url must be a non-empty string")
        if not _is_hex_sha256(art["sha256"]):
            raise ManifestError(f"artifact[{idx}].sha256 must be 64 hex chars")
        size = art["size"]
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise ManifestError(f"artifact[{idx}].size must be a positive integer")
        delta_from = art["delta_from"]
        if delta_from is not None:
            try:
                SemVer.parse(str(delta_from))
            except ValueError as exc:
                raise ManifestError(f"artifact[{idx}].delta_from invalid: {exc}") from exc
        else:
            has_full = True

        if "uncompressed_sha256" in art and art["uncompressed_sha256"] is not None:
            if not _is_hex_sha256(art["uncompressed_sha256"]):
                raise ManifestError(f"artifact[{idx}].uncompressed_sha256 invalid")
        if "uncompressed_size" in art and art["uncompressed_size"] is not None:
            usz = art["uncompressed_size"]
            if not isinstance(usz, int) or isinstance(usz, bool) or usz <= 0:
                raise ManifestError(f"artifact[{idx}].uncompressed_size invalid")

    if not has_full:
        raise ManifestError("manifest must contain at least one full artifact (delta_from: null)")

    return manifest


def sign_manifest(manifest: Dict[str, Any], private_pem: str) -> Dict[str, Any]:
    """Return a copy of manifest signed with an Ed25519 PKCS#8 PEM private key."""
    seed = ed25519.decode_private_pem(private_pem)
    pub = ed25519.public_key_from_seed(seed)
    kid = ed25519.key_id(pub)
    out = copy.deepcopy(manifest)
    out.pop("signature", None)
    payload = canonical_manifest_bytes(out)
    sig = ed25519.sign(seed, payload)
    out["signature"] = {
        "alg": "ed25519",
        "key_id": kid,
        "sig": base64.b64encode(sig).decode("ascii"),
    }
    validate_manifest_schema(out)
    return out


def _extract_signature_fields(sig_obj: Any) -> tuple[Optional[str], bytes]:
    if isinstance(sig_obj, dict):
        alg = sig_obj.get("alg")
        if alg != "ed25519":
            raise VerificationError(f"unsupported signature algorithm: {alg!r}")
        kid = sig_obj.get("key_id")
        sig_b64 = sig_obj.get("sig")
        if not isinstance(sig_b64, str) or not sig_b64:
            raise VerificationError("missing signature.sig")
        try:
            raw_sig = base64.b64decode(sig_b64, validate=True)
        except Exception as exc:
            raise VerificationError("invalid base64 in signature.sig") from exc
        if len(raw_sig) != 64:
            raise VerificationError("Ed25519 signature must be 64 bytes")
        return (str(kid) if kid is not None else None), raw_sig

    if isinstance(sig_obj, str):
        text = sig_obj.strip()
        if text.startswith("ed25519:"):
            text = text[len("ed25519:") :]
        try:
            raw_sig = base64.b64decode(text, validate=True)
        except Exception as exc:
            raise VerificationError("invalid base64 in signature") from exc
        if len(raw_sig) != 64:
            raise VerificationError("Ed25519 signature must be 64 bytes")
        return None, raw_sig

    raise VerificationError("invalid signature field type")


def load_public_keys(pubkey_path: str) -> List[bytes]:
    """Load one or more Ed25519 SPKI PEM public keys from a file or directory."""
    keys: List[bytes] = []
    if os.path.isdir(pubkey_path):
        for name in sorted(os.listdir(pubkey_path)):
            if name.endswith(".pem") or name.endswith(".pub"):
                with open(os.path.join(pubkey_path, name), "r", encoding="utf-8") as f:
                    keys.append(ed25519.decode_public_pem(f.read()))
    elif os.path.isfile(pubkey_path):
        with open(pubkey_path, "r", encoding="utf-8") as f:
            keys.append(ed25519.decode_public_pem(f.read()))
    else:
        raise VerificationError(f"public key path does not exist: {pubkey_path}")
    if not keys:
        raise VerificationError(f"no public keys found in {pubkey_path}")
    return keys


def verify_manifest(manifest: Dict[str, Any], public_keys: List[bytes]) -> str:
    """Validate manifest schema and verify Ed25519 signature against trusted keys.

    Returns the matched key_id on success; raises VerificationError or ManifestError
    on failure.
    """
    validate_manifest_schema(manifest)
    claimed_kid, raw_sig = _extract_signature_fields(manifest["signature"])
    payload = canonical_manifest_bytes(manifest)

    for pub in public_keys:
        kid = ed25519.key_id(pub)
        if claimed_kid is not None and claimed_kid != kid:
            continue
        if ed25519.verify(pub, payload, raw_sig):
            return kid

    raise VerificationError("manifest signature verification failed")


def select_artifact(
    manifest: Dict[str, Any],
    current_version: str,
    can_delta_from_current: bool = True,
) -> Dict[str, Any]:
    """Choose the best artifact: matching delta first (if usable), else full."""
    artifacts = manifest["artifacts"]
    if can_delta_from_current:
        for art in artifacts:
            if art.get("delta_from") == current_version:
                return art
    for art in artifacts:
        if art.get("delta_from") is None:
            return art
    raise ManifestError("no applicable full artifact found in manifest")


def select_full_artifact(manifest: Dict[str, Any]) -> Dict[str, Any]:
    """Return the full (non-delta) artifact from the manifest."""
    for art in manifest["artifacts"]:
        if art.get("delta_from") is None:
            return art
    raise ManifestError("no full artifact found in manifest")
