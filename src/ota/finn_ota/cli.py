"""CLI entrypoint and orchestration logic for finn-update."""

from __future__ import annotations

import argparse
import datetime
import json
import os
import sys
from typing import Any, Dict, Optional

from . import __version__
from .backend import BackendError, BootBackend, SlotStatus, select_backend
from .config import (
    DEFAULT_CONFIG_PATH,
    OtaConfig,
    github_releases_base_url,
    load_config,
    save_config,
)
from .delta import DeltaError, materialise_slot_image
from .download import DownloadError, download_artifact, fetch_bytes
from .health import run_health_check
from .manifest import (
    ManifestError,
    VerificationError,
    load_public_keys,
    select_artifact,
    select_full_artifact,
    verify_manifest,
)
from .rollout import compute_rollout_bucket, evaluate_rollout, read_machine_id, validate_channel
from .version import SemVer

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_VERIFY = 2
EXIT_NETWORK = 3
EXIT_POLICY = 4


class PolicyError(Exception):
    """Update blocked by rollout, min_version, downgrade, or layout policy."""


def _utc_now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _current_version(cfg: OtaConfig, slot_status: SlotStatus) -> str:
    booted = slot_status.booted_slot
    if booted and slot_status.slot_versions.get(booted):
        return str(slot_status.slot_versions[booted])
    if slot_status.active_slot and slot_status.slot_versions.get(slot_status.active_slot):
        return str(slot_status.slot_versions[slot_status.active_slot])
    if os.path.isfile(cfg.version_path):
        with open(cfg.version_path, "r", encoding="utf-8") as f:
            v = f.read().strip()
            if v:
                return v
    return "0.0.0"


def _load_cache(state_dir: str) -> dict:
    p = os.path.join(state_dir, "status_cache.json")
    if os.path.isfile(p):
        try:
            with open(p, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, json.JSONDecodeError):
            pass
    return {}


def _save_cache(state_dir: str, data: dict) -> None:
    os.makedirs(state_dir, mode=0o755, exist_ok=True)
    p = os.path.join(state_dir, "status_cache.json")
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.chmod(tmp, 0o644)
    os.replace(tmp, p)


def _status_payload(
    cfg: OtaConfig,
    backend: BootBackend,
    machine_id_override: Optional[str] = None,
) -> Dict[str, Any]:
    st = backend.get_status()
    cur_ver = _current_version(cfg, st)
    mid = machine_id_override or read_machine_id(cfg.machine_id_path)
    cache = _load_cache(cfg.state_dir)
    bucket = None
    if mid:
        bucket = compute_rollout_bucket(mid, cfg.channel, cur_ver)
    return {
        "schema_version": 1,
        "client_version": __version__,
        "current_version": cur_ver,
        "channel": cfg.channel,
        "arch": cfg.arch,
        "github_repo": cfg.github_repo,
        "base_url": cfg.base_url,
        "manifest_url": cfg.effective_manifest_url(),
        "auto_check": cfg.auto_check,
        "auto_download": cfg.auto_download,
        "machine_id_bucket": bucket,
        "backend": st.backend,
        "layout": st.layout,
        "active_slot": st.active_slot,
        "booted_slot": st.booted_slot,
        "inactive_slot": st.inactive_slot,
        "pending_slot": st.pending_slot,
        "tries_left": st.tries_left,
        "slot_committed": st.slot_committed,
        "slots": {
            "a": {
                "version": st.slot_versions.get("a"),
                "state": _slot_label("a", st),
            },
            "b": {
                "version": st.slot_versions.get("b"),
                "state": _slot_label("b", st),
            },
        },
        "last_check": cache.get("last_check"),
        "available_update": cache.get("available_update"),
        "notes": st.notes,
        "error": None,
    }


def _slot_label(slot: str, st: SlotStatus) -> str:
    if st.layout != "ab":
        return "single" if slot == "a" else "absent"
    if st.pending_slot == slot:
        return "pending"
    if st.booted_slot == slot and st.active_slot == slot:
        return "active"
    if st.booted_slot == slot:
        return "trial"
    if st.active_slot == slot:
        return "active"
    return "inactive"


def _can_delta(backend: BootBackend, st: SlotStatus) -> bool:
    if st.layout != "ab" or not st.booted_slot:
        return False
    dev = backend.get_slot_device(st.booted_slot)
    return bool(dev and os.path.exists(dev) and os.path.getsize(dev) > 0)


def _check_manifest(
    cfg: OtaConfig,
    backend: BootBackend,
    channel: Optional[str] = None,
    manifest_url: Optional[str] = None,
    pubkey_path: Optional[str] = None,
    machine_id_override: Optional[str] = None,
    force_rollout: bool = False,
    allow_downgrade: bool = False,
) -> tuple[Dict[str, Any], Optional[Dict[str, Any]]]:
    ch = validate_channel(channel or cfg.channel)
    url = manifest_url or cfg.effective_manifest_url(channel_override=channel)
    st = backend.get_status()
    cur_ver_str = _current_version(cfg, st)
    cur_ver = SemVer.parse(cur_ver_str)

    raw_bytes = fetch_bytes(url)
    try:
        manifest = json.loads(raw_bytes.decode("utf-8"))
    except Exception as exc:
        raise ManifestError(f"invalid JSON in manifest {url}: {exc}") from exc

    keys = load_public_keys(pubkey_path or cfg.pubkey_path)
    matched_kid = verify_manifest(manifest, keys)

    if manifest["channel"] != ch:
        raise ManifestError(
            f"manifest channel {manifest['channel']!r} does not match requested channel {ch!r}"
        )
    m_arch = manifest.get("arch")
    if m_arch is not None and m_arch != cfg.arch:
        raise ManifestError(
            f"manifest arch {m_arch!r} does not match host arch {cfg.arch!r}"
        )

    target_ver = SemVer.parse(str(manifest["version"]))
    min_ver = SemVer.parse(str(manifest["min_version"]))

    mid = machine_id_override or read_machine_id(cfg.machine_id_path)
    rollout_pct = int(manifest["rollout"])
    eligible, bucket, deferral_reason = evaluate_rollout(
        rollout_pct, mid, ch, str(manifest["version"]), force=force_rollout
    )

    can_delta = _can_delta(backend, st)
    selected = select_artifact(manifest, cur_ver_str, can_delta_from_current=can_delta)

    if cur_ver < min_ver:
        result = {
            "status": "blocked_min_version",
            "update_available": True,
            "eligible": False,
            "reason": f"current version {cur_ver} is below manifest min_version {min_ver}",
            "current_version": str(cur_ver),
            "target_version": str(target_ver),
            "channel": ch,
            "min_version": str(min_ver),
            "rollout": rollout_pct,
            "machine_id_bucket": bucket,
            "verified_key_id": matched_kid,
            "release_notes": manifest.get("release_notes"),
            "selected_artifact": selected,
        }
        return result, manifest

    if target_ver <= cur_ver and not allow_downgrade:
        result = {
            "status": "up_to_date",
            "update_available": False,
            "eligible": False,
            "reason": None,
            "current_version": str(cur_ver),
            "target_version": str(target_ver),
            "channel": ch,
            "min_version": str(min_ver),
            "rollout": rollout_pct,
            "machine_id_bucket": bucket,
            "verified_key_id": matched_kid,
            "release_notes": manifest.get("release_notes"),
            "selected_artifact": None,
        }
        cache = _load_cache(cfg.state_dir)
        cache["last_check"] = _utc_now_iso()
        cache["available_update"] = None
        _save_cache(cfg.state_dir, cache)
        return result, manifest

    status_label = "update_available" if eligible else "deferred_rollout"
    result = {
        "status": status_label,
        "update_available": True,
        "eligible": eligible,
        "reason": deferral_reason,
        "current_version": str(cur_ver),
        "target_version": str(target_ver),
        "channel": ch,
        "min_version": str(min_ver),
        "rollout": rollout_pct,
        "machine_id_bucket": bucket,
        "verified_key_id": matched_kid,
        "release_notes": manifest.get("release_notes"),
        "selected_artifact": selected,
    }
    cache = _load_cache(cfg.state_dir)
    cache["last_check"] = _utc_now_iso()
    cache["available_update"] = (
        {
            "version": str(target_ver),
            "channel": ch,
            "eligible": eligible,
            "reason": deferral_reason,
            "size": selected["size"],
            "kind": selected.get("kind") or ("delta" if selected.get("delta_from") else "full"),
        }
        if eligible
        else None
    )
    _save_cache(cfg.state_dir, cache)
    return result, manifest


def _staging_artifact_path(state_dir: str, version: str, art: dict) -> str:
    fname = os.path.basename(art["url"].split("?", 1)[0]) or f"artifact-{version}.bin"
    return os.path.join(state_dir, "downloads", version, fname)


def _do_download(
    cfg: OtaConfig,
    backend: BootBackend,
    args: argparse.Namespace,
) -> tuple[Dict[str, Any], Dict[str, Any], str]:
    check_res, manifest = _check_manifest(
        cfg,
        backend,
        channel=args.channel,
        manifest_url=args.manifest_url,
        pubkey_path=args.pubkey,
        machine_id_override=args.machine_id,
        force_rollout=args.force_rollout,
        allow_downgrade=args.allow_downgrade,
    )
    assert manifest is not None
    if not check_res["update_available"]:
        raise PolicyError(f"already up to date ({check_res['current_version']})")
    if check_res["status"] == "blocked_min_version":
        raise PolicyError(str(check_res["reason"]))
    if not check_res["eligible"]:
        raise PolicyError(
            f"update {check_res['target_version']} deferred by rollout policy ({check_res['reason']})"
        )

    art = check_res["selected_artifact"]
    dest = _staging_artifact_path(cfg.state_dir, check_res["target_version"], art)
    dl = download_artifact(art["url"], dest, art["sha256"], art["size"])
    out = {
        "status": "downloaded",
        "version": check_res["target_version"],
        "channel": check_res["channel"],
        "artifact": art,
        "path": dl.path,
        "bytes_downloaded": dl.bytes_downloaded,
        "total_bytes": dl.total_bytes,
        "resumed_from": dl.resumed_from,
        "cached": dl.cached,
        "sha256": dl.sha256,
    }
    return out, manifest, dl.path


def _do_apply(
    cfg: OtaConfig,
    backend: BootBackend,
    args: argparse.Namespace,
) -> Dict[str, Any]:
    st = backend.get_status()
    if st.layout != "ab" or not st.inactive_slot or not st.booted_slot:
        raise PolicyError(
            "cannot apply A/B update: system uses single-partition layout without FINNOS_A/FINNOS_B"
        )

    dl_res, manifest, artifact_path = _do_download(cfg, backend, args)
    art = dl_res["artifact"]
    target_ver = dl_res["version"]
    staging_img = os.path.join(cfg.state_dir, "staging", f"{target_ver}-{st.inactive_slot}.img")
    base_dev = backend.get_slot_device(st.booted_slot)

    used_fallback_full = False
    try:
        materialise_slot_image(artifact_path, staging_img, art, base_slot_path=base_dev)
    except DeltaError:
        if art.get("delta_from") is None:
            raise
        # Delta failed to apply against live slot; fall back to full artifact automatically.
        full_art = select_full_artifact(manifest)
        full_dest = _staging_artifact_path(cfg.state_dir, target_ver, full_art)
        dl_full = download_artifact(
            full_art["url"], full_dest, full_art["sha256"], full_art["size"]
        )
        materialise_slot_image(dl_full.path, staging_img, full_art)
        art = full_art
        used_fallback_full = True

    try:
        backend.stage_image(st.inactive_slot, staging_img, target_ver)
        tries = args.tries if args.tries is not None else cfg.boot_tries
        backend.mark_bootable_once(st.inactive_slot, tries=tries, version=target_ver)
    finally:
        if os.path.exists(staging_img):
            os.remove(staging_img)

    cache = _load_cache(cfg.state_dir)
    cache["available_update"] = None
    _save_cache(cfg.state_dir, cache)

    return {
        "status": "reboot_required",
        "target_version": target_ver,
        "staged_slot": st.inactive_slot,
        "tries_armed": args.tries if args.tries is not None else cfg.boot_tries,
        "used_delta": art.get("delta_from") is not None,
        "delta_fallback_to_full": used_fallback_full,
        "sha256": art["sha256"],
    }


def _emit(payload: Dict[str, Any], as_json: bool) -> None:
    if as_json:
        print(json.dumps(payload, indent=2, sort_keys=True))
        return
    status = payload.get("status")
    if "active_slot" in payload and "backend" in payload:
        print(
            f"FinnOS {payload['current_version']} ({payload['channel']}, {payload['arch']})\n"
            f"  backend:       {payload['backend']} (layout: {payload['layout']})\n"
            f"  active slot:   {payload['active_slot']} (booted: {payload['booted_slot']}, committed: {payload['slot_committed']})\n"
            f"  pending slot:  {payload['pending_slot']} (tries left: {payload['tries_left']})\n"
            f"  last check:    {payload['last_check'] or 'never'}"
        )
        return
    if status == "error":
        print(f"error: {payload.get('error')}", file=sys.stderr)
        return
    print(json.dumps(payload, indent=2, sort_keys=True))


def _add_global_flags(target: argparse.ArgumentParser, *, suppress_defaults: bool = False) -> None:
    d_none = argparse.SUPPRESS if suppress_defaults else None
    d_false = argparse.SUPPRESS if suppress_defaults else False
    d_cfg = argparse.SUPPRESS if suppress_defaults else DEFAULT_CONFIG_PATH
    d_boot = argparse.SUPPRESS if suppress_defaults else "/boot"

    target.add_argument("--json", action="store_true", default=d_false, help="emit machine-readable JSON")
    target.add_argument("--config", default=d_cfg, help="path to OTA config file")
    target.add_argument(
        "--backend",
        choices=("auto", "fake", "systemd-boot", "grub"),
        default=d_none,
        help="override bootloader backend",
    )
    target.add_argument("--state-dir", default=d_none, help="override OTA state/staging directory")
    target.add_argument("--boot-dir", default=d_boot, help="override /boot mount path")
    target.add_argument("--pubkey", default=d_none, help="override Ed25519 SPKI public key PEM or dir")
    target.add_argument("--version-file", default=d_none, help="override version file path")
    target.add_argument("--machine-id", default=d_none, help="override machine-id for rollout bucketing")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="finn-update", description="FinnOS A/B OTA update client")
    _add_global_flags(p, suppress_defaults=False)

    common_sub = argparse.ArgumentParser(add_help=False)
    _add_global_flags(common_sub, suppress_defaults=True)

    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("status", parents=[common_sub], help="report current slot, version, and update state")

    chk = sub.add_parser("check", parents=[common_sub], help="fetch and verify signed update manifest")
    chk.add_argument("--channel", choices=("stable", "beta", "dev"), default=None)
    chk.add_argument("--manifest-url", default=None)
    chk.add_argument("--force-rollout", action="store_true")
    chk.add_argument("--allow-downgrade", action="store_true")
    chk.add_argument("--quiet", action="store_true")

    dl = sub.add_parser("download", parents=[common_sub], help="download and verify update artifact")
    dl.add_argument("--channel", choices=("stable", "beta", "dev"), default=None)
    dl.add_argument("--manifest-url", default=None)
    dl.add_argument("--force-rollout", action="store_true")
    dl.add_argument("--allow-downgrade", action="store_true")

    ap = sub.add_parser("apply", parents=[common_sub], help="download, stage to inactive slot, and arm trial boot")
    ap.add_argument("--channel", choices=("stable", "beta", "dev"), default=None)
    ap.add_argument("--manifest-url", default=None)
    ap.add_argument("--force-rollout", action="store_true")
    ap.add_argument("--allow-downgrade", action="store_true")
    ap.add_argument("--tries", type=int, default=None, help="boot tries to arm (default 3)")

    sub.add_parser("commit", parents=[common_sub], help="commit currently booted slot as known-good")

    rb = sub.add_parser("rollback", parents=[common_sub], help="revert boot selection to previous good slot")
    rb.add_argument("--reason", default="", help="optional rollback reason")

    hc = sub.add_parser("health-check", parents=[common_sub], help="verify compositor+session health and optionally commit")
    hc.add_argument("--wait", type=float, default=0.0, help="seconds to wait for services/socket")
    hc.add_argument("--wayland-socket", default="/run/finn/wayland-0")
    hc.add_argument("--skip-systemd", action="store_true", help="skip systemd/wayland probes (test mode)")
    hc.add_argument("--commit", action="store_true", help="commit booted A/B slot if healthy")

    cfg_cmd = sub.add_parser("config", parents=[common_sub], help="show or update OTA configuration")
    cfg_cmd.add_argument("--channel", choices=("stable", "beta", "dev"), default=None)
    cfg_cmd.add_argument("--github-repo", default=None, help="GitHub OWNER/REPO for Releases OTA")
    cfg_cmd.add_argument("--base-url", default=None)
    cfg_cmd.add_argument("--manifest-url", default=None)
    cfg_cmd.add_argument("--auto-check", choices=("true", "false"), default=None)
    cfg_cmd.add_argument("--auto-download", choices=("true", "false"), default=None)

    # Test/simulation helper exposed cleanly for fake backend testing
    sim = sub.add_parser("simulate-boot", parents=[common_sub], help="simulate bootloader boot transition (fake backend only)")
    sim.add_argument("--count", type=int, default=1)

    return p


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        cfg = load_config(args.config)
        if args.backend:
            cfg.backend = args.backend
        if args.state_dir:
            cfg.state_dir = args.state_dir
        if args.pubkey:
            cfg.pubkey_path = args.pubkey
        if args.version_file:
            cfg.version_path = args.version_file

        if args.command == "config":
            changed = False
            if args.channel is not None:
                cfg.channel = validate_channel(args.channel)
                changed = True
            if args.github_repo is not None:
                cfg.base_url = github_releases_base_url(args.github_repo)
                cfg.github_repo = args.github_repo.strip().strip("/")
                changed = True
            if args.base_url is not None:
                cfg.base_url = args.base_url
                changed = True
            if args.manifest_url is not None:
                cfg.manifest_url = args.manifest_url or None
                changed = True
            if args.auto_check is not None:
                cfg.auto_check = args.auto_check == "true"
                changed = True
            if args.auto_download is not None:
                cfg.auto_download = args.auto_download == "true"
                changed = True
            if changed:
                save_config(cfg, args.config)
            _emit(
                {
                    "status": "ok",
                    "channel": cfg.channel,
                    "github_repo": cfg.github_repo,
                    "base_url": cfg.base_url,
                    "manifest_url": cfg.manifest_url,
                    "effective_manifest_url": cfg.effective_manifest_url(),
                    "auto_check": cfg.auto_check,
                    "auto_download": cfg.auto_download,
                    "arch": cfg.arch,
                    "backend": cfg.backend,
                },
                args.json,
            )
            return EXIT_OK

        backend = select_backend(
            cfg.backend,
            state_dir=cfg.state_dir,
            version_path=cfg.version_path,
            boot_dir=args.boot_dir,
        )

        if args.command == "status":
            _emit(_status_payload(cfg, backend, machine_id_override=args.machine_id), args.json)
            return EXIT_OK

        if args.command == "check":
            res, _ = _check_manifest(
                cfg,
                backend,
                channel=args.channel,
                manifest_url=args.manifest_url,
                pubkey_path=args.pubkey,
                machine_id_override=args.machine_id,
                force_rollout=args.force_rollout,
                allow_downgrade=args.allow_downgrade,
            )
            if not getattr(args, "quiet", False) or args.json:
                _emit(res, args.json)
            return EXIT_OK

        if args.command == "download":
            res, _, _ = _do_download(cfg, backend, args)
            _emit(res, args.json)
            return EXIT_OK

        if args.command == "apply":
            res = _do_apply(cfg, backend, args)
            _emit(res, args.json)
            return EXIT_OK

        if args.command == "commit":
            st = backend.get_status()
            if st.layout != "ab":
                _emit({"status": "skipped", "reason": "single-slot layout"}, args.json)
                return EXIT_OK
            slot = backend.commit_booted_slot()
            _emit({"status": "committed", "slot": slot}, args.json)
            return EXIT_OK

        if args.command == "rollback":
            slot = backend.rollback(reason=args.reason)
            _emit({"status": "rolled_back", "target_slot": slot, "reason": args.reason}, args.json)
            return EXIT_OK

        if args.command == "health-check":
            hc = run_health_check(
                version_path=cfg.version_path,
                wayland_socket=args.wayland_socket,
                wait_seconds=args.wait,
                skip_systemd=args.skip_systemd,
            )
            committed_slot = None
            if hc.healthy and args.commit:
                st = backend.get_status()
                if st.layout == "ab" and not st.slot_committed:
                    committed_slot = backend.commit_booted_slot()
            payload = {
                "status": "healthy" if hc.healthy else "unhealthy",
                "healthy": hc.healthy,
                "checks": hc.checks,
                "details": hc.details,
                "elapsed_sec": hc.elapsed_sec,
                "committed_slot": committed_slot,
            }
            _emit(payload, args.json)
            return EXIT_OK if hc.healthy else EXIT_ERROR

        if args.command == "simulate-boot":
            from .backend import FakeBootBackend

            if not isinstance(backend, FakeBootBackend):
                raise BackendError("simulate-boot is only supported on the fake backend")
            booted = None
            for _ in range(max(1, args.count)):
                booted = backend.simulate_boot()
            st = backend.get_status()
            _emit(
                {
                    "status": "booted",
                    "booted_slot": booted,
                    "active_slot": st.active_slot,
                    "pending_slot": st.pending_slot,
                    "tries_left": st.tries_left,
                    "slot_committed": st.slot_committed,
                },
                args.json,
            )
            return EXIT_OK

        raise BackendError(f"unhandled command: {args.command}")

    except VerificationError as exc:
        _emit({"status": "error", "code": "verification_failed", "error": str(exc)}, args.json)
        return EXIT_VERIFY
    except (DownloadError, ManifestError) as exc:
        _emit({"status": "error", "code": "manifest_or_download_error", "error": str(exc)}, args.json)
        return EXIT_NETWORK
    except PolicyError as exc:
        _emit({"status": "error", "code": "policy_refused", "error": str(exc)}, args.json)
        return EXIT_POLICY
    except (BackendError, DeltaError, ValueError, OSError) as exc:
        _emit({"status": "error", "code": "operational_error", "error": str(exc)}, args.json)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
