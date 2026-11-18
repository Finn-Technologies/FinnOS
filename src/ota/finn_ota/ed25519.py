"""Pure-Python RFC 8032 Ed25519 signing, verification, and OpenSSL PEM support.

Zero external dependencies: relies only on Python's standard library (`hashlib`,
`base64`, `os`, `secrets`). Keys use standard PKCS#8 (private) and SPKI (public)
ASN.1 DER/PEM framing so they interoperate directly with OpenSSL (`pkeyutl`).
"""

from __future__ import annotations

import base64
import hashlib
import secrets
from typing import Tuple

_P = 2**255 - 19
_Q = 2**252 + 27742317777372353535851937790883648493
_D = (-121665 * pow(121666, _P - 2, _P)) % _P
_I = pow(2, (_P - 1) // 4, _P)

# RFC 8032 Section 5.1 base point B
_BY = (4 * pow(5, _P - 2, _P)) % _P


def _recover_x(y: int, sign: int) -> int:
    if y >= _P:
        raise ValueError("y coordinate out of range")
    x2 = ((y * y - 1) * pow((_D * y * y + 1) % _P, _P - 2, _P)) % _P
    if x2 == 0:
        if sign:
            raise ValueError("invalid point encoding")
        return 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P != 0:
        x = (x * _I) % _P
    if (x * x - x2) % _P != 0:
        raise ValueError("point is not on Ed25519 curve")
    if (x & 1) != sign:
        x = _P - x
    return x


_BX = _recover_x(_BY, 0)
_B = (_BX, _BY, 1, (_BX * _BY) % _P)

# Standard ASN.1 DER prefixes for Ed25519 (OID 1.3.101.112)
_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
_PKCS8_PREFIX = bytes.fromhex("302e020100300506032b657004220420")


def _point_add(
    p1: Tuple[int, int, int, int], p2: Tuple[int, int, int, int]
) -> Tuple[int, int, int, int]:
    x1, y1, z1, t1 = p1
    x2, y2, z2, t2 = p2
    a = ((y1 - x1) * (y2 - x2)) % _P
    b = ((y1 + x1) * (y2 + x2)) % _P
    c = (2 * _D * t1 * t2) % _P
    d = (2 * z1 * z2) % _P
    e = (b - a) % _P
    f = (d - c) % _P
    g = (d + c) % _P
    h = (b + a) % _P
    return ((e * f) % _P, (g * h) % _P, (f * g) % _P, (e * h) % _P)


def _point_mul(s: int, p: Tuple[int, int, int, int]) -> Tuple[int, int, int, int]:
    res = (0, 1, 1, 0)
    cur = p
    while s > 0:
        if s & 1:
            res = _point_add(res, cur)
        cur = _point_add(cur, cur)
        s >>= 1
    return res


def _point_compress(p: Tuple[int, int, int, int]) -> bytes:
    x, y, z, _ = p
    z_inv = pow(z, _P - 2, _P)
    nx = (x * z_inv) % _P
    ny = (y * z_inv) % _P
    val = ny | ((nx & 1) << 255)
    return val.to_bytes(32, "little")


def _point_decompress(data: bytes) -> Tuple[int, int, int, int]:
    if len(data) != 32:
        raise ValueError("Ed25519 point must be 32 bytes")
    val = int.from_bytes(data, "little")
    sign = (val >> 255) & 1
    y = val & ((1 << 255) - 1)
    x = _recover_x(y, sign)
    return (x, y, 1, (x * y) % _P)


def _clamp_scalar(digest_prefix: bytes) -> int:
    buf = bytearray(digest_prefix)
    buf[0] &= 248
    buf[31] &= 127
    buf[31] |= 64
    return int.from_bytes(buf, "little")


def _sha512(data: bytes) -> bytes:
    return hashlib.sha512(data).digest()


def public_key_from_seed(seed: bytes) -> bytes:
    """Derive the 32-byte Ed25519 public key from a 32-byte private seed."""
    if len(seed) != 32:
        raise ValueError("Ed25519 private seed must be 32 bytes")
    h = _sha512(seed)
    a = _clamp_scalar(h[:32])
    return _point_compress(_point_mul(a, _B))


def generate_keypair() -> Tuple[bytes, bytes]:
    """Generate a random (seed_32, pubkey_32) Ed25519 keypair."""
    seed = secrets.token_bytes(32)
    return seed, public_key_from_seed(seed)


def sign(seed: bytes, message: bytes) -> bytes:
    """Sign message with a 32-byte Ed25519 private seed, returning 64-byte signature."""
    if len(seed) != 32:
        raise ValueError("Ed25519 private seed must be 32 bytes")
    h = _sha512(seed)
    a = _clamp_scalar(h[:32])
    prefix = h[32:]
    pub = _point_compress(_point_mul(a, _B))

    r = int.from_bytes(_sha512(prefix + message), "little") % _Q
    r_point = _point_compress(_point_mul(r, _B))
    k = int.from_bytes(_sha512(r_point + pub + message), "little") % _Q
    s = (r + k * a) % _Q
    return r_point + s.to_bytes(32, "little")


def verify(pubkey: bytes, message: bytes, signature: bytes) -> bool:
    """Verify a 64-byte Ed25519 signature against a 32-byte public key."""
    if len(pubkey) != 32 or len(signature) != 64:
        return False
    try:
        a_point = _point_decompress(pubkey)
        r_bytes = signature[:32]
        r_point = _point_decompress(r_bytes)
        s = int.from_bytes(signature[32:], "little")
    except ValueError:
        return False
    if s >= _Q:
        return False
    k = int.from_bytes(_sha512(r_bytes + pubkey + message), "little") % _Q
    lhs = _point_mul(s, _B)
    rhs = _point_add(r_point, _point_mul(k, a_point))
    return _point_compress(lhs) == _point_compress(rhs)


def key_id(pubkey: bytes) -> str:
    """Return a short stable fingerprint (16 hex chars) for a 32-byte public key."""
    if len(pubkey) != 32:
        raise ValueError("Ed25519 public key must be 32 bytes")
    return hashlib.sha256(pubkey).hexdigest()[:16]


def _decode_pem(pem_text: str, header: str, footer: str) -> bytes:
    lines = [line.strip() for line in pem_text.strip().splitlines()]
    try:
        start = lines.index(header)
        end = lines.index(footer, start + 1)
    except ValueError as exc:
        raise ValueError(f"missing PEM markers {header!r} / {footer!r}") from exc
    body = "".join(lines[start + 1 : end])
    return base64.b64decode(body, validate=True)


def _encode_pem(der: bytes, label: str) -> str:
    b64 = base64.b64encode(der).decode("ascii")
    wrapped = "\n".join(b64[i : i + 64] for i in range(0, len(b64), 64))
    return f"-----BEGIN {label}-----\n{wrapped}\n-----END {label}-----\n"


def encode_public_pem(pubkey: bytes) -> str:
    if len(pubkey) != 32:
        raise ValueError("Ed25519 public key must be 32 bytes")
    return _encode_pem(_SPKI_PREFIX + pubkey, "PUBLIC KEY")


def decode_public_pem(pem_text: str) -> bytes:
    der = _decode_pem(pem_text, "-----BEGIN PUBLIC KEY-----", "-----END PUBLIC KEY-----")
    if len(der) != len(_SPKI_PREFIX) + 32 or not der.startswith(_SPKI_PREFIX):
        raise ValueError("unsupported public key format (expected Ed25519 SPKI PEM)")
    return der[len(_SPKI_PREFIX) :]


def encode_private_pem(seed: bytes) -> str:
    if len(seed) != 32:
        raise ValueError("Ed25519 private seed must be 32 bytes")
    return _encode_pem(_PKCS8_PREFIX + seed, "PRIVATE KEY")


def decode_private_pem(pem_text: str) -> bytes:
    der = _decode_pem(pem_text, "-----BEGIN PRIVATE KEY-----", "-----END PRIVATE KEY-----")
    if len(der) != len(_PKCS8_PREFIX) + 32 or not der.startswith(_PKCS8_PREFIX):
        raise ValueError("unsupported private key format (expected Ed25519 PKCS#8 PEM)")
    return der[len(_PKCS8_PREFIX) :]
