"""Resumable HTTP Range and local file artifact downloader with SHA-256 verification."""

from __future__ import annotations

import hashlib
import os
import re
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Callable, Optional

_CONTENT_RANGE_RE = re.compile(r"^bytes\s+(\d+)-(\d+)/(\d+|\*)$", re.IGNORECASE)
_CHUNK_SIZE = 65536


class DownloadError(Exception):
    """Download, range-resume, or integrity verification error."""


@dataclass
class DownloadResult:
    path: str
    bytes_downloaded: int
    total_bytes: int
    resumed_from: int
    cached: bool
    sha256: str


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(_CHUNK_SIZE)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def fetch_bytes(url: str, timeout: float = 30.0) -> bytes:
    """Fetch small metadata payloads (e.g. manifest JSON) over HTTP(S) or file://."""
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme in ("", "file"):
        local_path = urllib.parse.unquote(parsed.path) if parsed.scheme == "file" else url
        with open(local_path, "rb") as f:
            return f.read()
    req = urllib.request.Request(url, headers={"User-Agent": "finn-update/0.1"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read()
    except urllib.error.URLError as exc:
        raise DownloadError(f"failed to fetch {url}: {exc}") from exc


def download_artifact(
    url: str,
    dest_path: str,
    expected_sha256: str,
    expected_size: int,
    timeout: float = 60.0,
    progress_cb: Optional[Callable[[int, int], None]] = None,
) -> DownloadResult:
    """Download url to dest_path with HTTP Range resume and SHA-256 verification."""
    expected_sha256 = expected_sha256.lower()
    os.makedirs(os.path.dirname(os.path.abspath(dest_path)), exist_ok=True)

    # Already cached and verified?
    if os.path.isfile(dest_path):
        if os.path.getsize(dest_path) == expected_size:
            actual = sha256_file(dest_path)
            if actual == expected_sha256:
                return DownloadResult(
                    path=dest_path,
                    bytes_downloaded=0,
                    total_bytes=expected_size,
                    resumed_from=0,
                    cached=True,
                    sha256=actual,
                )
        os.remove(dest_path)

    parsed = urllib.parse.urlparse(url)
    part_path = dest_path + ".part"

    if parsed.scheme in ("", "file"):
        local_src = urllib.parse.unquote(parsed.path) if parsed.scheme == "file" else url
        if not os.path.isfile(local_src):
            raise DownloadError(f"local artifact not found: {local_src}")
        with open(local_src, "rb") as src, open(part_path, "wb") as dst:
            written = 0
            while True:
                buf = src.read(_CHUNK_SIZE)
                if not buf:
                    break
                dst.write(buf)
                written += len(buf)
                if progress_cb:
                    progress_cb(written, expected_size)
            dst.flush()
            os.fsync(dst.fileno())
        resumed_from = 0
        bytes_downloaded = written
    else:
        resumed_from, bytes_downloaded = _http_download_with_resume(
            url, part_path, expected_size, expected_sha256, timeout, progress_cb
        )

    actual_size = os.path.getsize(part_path)
    if actual_size != expected_size:
        os.remove(part_path)
        raise DownloadError(
            f"size mismatch for {url}: expected {expected_size} bytes, got {actual_size}"
        )

    actual_sha = sha256_file(part_path)
    if actual_sha != expected_sha256:
        os.remove(part_path)
        raise DownloadError(
            f"sha256 mismatch for {url}: expected {expected_sha256}, got {actual_sha}"
        )

    os.replace(part_path, dest_path)
    return DownloadResult(
        path=dest_path,
        bytes_downloaded=bytes_downloaded,
        total_bytes=expected_size,
        resumed_from=resumed_from,
        cached=False,
        sha256=actual_sha,
    )


def _http_download_with_resume(
    url: str,
    part_path: str,
    expected_size: int,
    expected_sha256: str,
    timeout: float,
    progress_cb: Optional[Callable[[int, int], None]],
) -> tuple[int, int]:
    existing_size = os.path.getsize(part_path) if os.path.isfile(part_path) else 0
    if existing_size == expected_size:
        if sha256_file(part_path) == expected_sha256:
            return existing_size, 0
        os.remove(part_path)
        existing_size = 0
    elif existing_size > expected_size:
        os.remove(part_path)
        existing_size = 0

    for attempt in range(2):
        headers = {"User-Agent": "finn-update/0.1"}
        resuming = existing_size > 0
        if resuming:
            headers["Range"] = f"bytes={existing_size}-"

        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                status = getattr(resp, "status", 200)
                if resuming and status == 206:
                    cr = resp.headers.get("Content-Range", "")
                    m = _CONTENT_RANGE_RE.match(cr.strip())
                    if not m or int(m.group(1)) != existing_size:
                        # Server sent an unexpected byte range; restart cleanly.
                        existing_size = 0
                        resuming = False
                        mode = "wb"
                    else:
                        mode = "ab"
                else:
                    # Server returned 200 OK instead of 206; restart from 0.
                    existing_size = 0
                    resuming = False
                    mode = "wb"

                written_now = 0
                with open(part_path, mode) as dst:
                    while True:
                        chunk = resp.read(_CHUNK_SIZE)
                        if not chunk:
                            break
                        dst.write(chunk)
                        written_now += len(chunk)
                        if progress_cb:
                            progress_cb(existing_size + written_now, expected_size)
                    dst.flush()
                    os.fsync(dst.fileno())
                return existing_size, written_now
        except urllib.error.HTTPError as exc:
            if exc.code == 416 and resuming and attempt == 0:
                if os.path.isfile(part_path):
                    os.remove(part_path)
                existing_size = 0
                continue
            raise DownloadError(f"HTTP {exc.code} downloading {url}: {exc.reason}") from exc
        except urllib.error.URLError as exc:
            raise DownloadError(f"network error downloading {url}: {exc}") from exc

    raise DownloadError(f"failed to download {url} after range retry")
