"""Boot health verification for FinnOS OTA slot commitment.

Verifies that the booted system is genuinely healthy (version file readable,
finn-comp.service and finn-session.service active, and Wayland display socket
/run/finn/wayland-0 present) before finn-boot-health.service commits a pending
A/B slot.
"""

from __future__ import annotations

import os
import subprocess
import time
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional


DEFAULT_WAYLAND_SOCKET = "/run/finn/wayland-0"
REQUIRED_SERVICES = ("finn-comp.service", "finn-session.service")


@dataclass
class HealthCheckResult:
    healthy: bool
    checks: Dict[str, bool]
    details: Dict[str, str]
    elapsed_sec: float


def _default_is_service_active(unit: str) -> bool:
    try:
        proc = subprocess.run(
            ["systemctl", "is-active", "--quiet", unit],
            capture_output=True,
            text=True,
            timeout=5,
        )
        return proc.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def _default_socket_exists(path: str) -> bool:
    return os.path.exists(path)


def run_health_check(
    version_path: str = "/usr/share/finn/version",
    wayland_socket: str = DEFAULT_WAYLAND_SOCKET,
    services: tuple[str, ...] = REQUIRED_SERVICES,
    wait_seconds: float = 0.0,
    poll_interval: float = 1.0,
    skip_systemd: bool = False,
    service_probe: Optional[Callable[[str], bool]] = None,
    socket_probe: Optional[Callable[[str], bool]] = None,
    sleep_fn: Callable[[float], None] = time.sleep,
) -> HealthCheckResult:
    """Evaluate boot health checks, optionally polling up to wait_seconds."""
    svc_check = service_probe or _default_is_service_active
    sock_check = socket_probe or _default_socket_exists

    start = time.monotonic()
    deadline = start + max(0.0, wait_seconds)

    while True:
        checks: Dict[str, bool] = {}
        details: Dict[str, str] = {}

        version_ok = os.path.isfile(version_path) and os.path.getsize(version_path) > 0
        checks["version_file"] = version_ok
        details["version_file"] = version_path if version_ok else f"missing or empty: {version_path}"

        if not skip_systemd:
            for unit in services:
                ok = svc_check(unit)
                checks[f"unit:{unit}"] = ok
                details[f"unit:{unit}"] = "active" if ok else "inactive"

            sock_ok = sock_check(wayland_socket)
            checks["wayland_socket"] = sock_ok
            details["wayland_socket"] = wayland_socket if sock_ok else f"missing: {wayland_socket}"

        if all(checks.values()):
            return HealthCheckResult(
                healthy=True,
                checks=checks,
                details=details,
                elapsed_sec=round(time.monotonic() - start, 3),
            )

        now = time.monotonic()
        if now >= deadline:
            return HealthCheckResult(
                healthy=False,
                checks=checks,
                details=details,
                elapsed_sec=round(now - start, 3),
            )
        sleep_fn(min(poll_interval, max(0.05, deadline - now)))
