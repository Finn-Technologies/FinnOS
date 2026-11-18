"""Host-runnable test suite for FinnOS Phase 5 OTA (finding B1)."""

from __future__ import annotations

import functools
import http.server
import json
import os
import shutil
import socketserver
import stat
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_OTA = os.path.join(ROOT, "src", "ota")
FINN_UPDATE = os.path.join(SRC_OTA, "finn-update")
OTA_PUBLISH = os.path.join(ROOT, "ota", "publish")
OTA_KEYGEN = os.path.join(ROOT, "ota", "keygen")

if SRC_OTA not in sys.path:
    sys.path.insert(0, SRC_OTA)

from finn_ota import ed25519  # noqa: E402
from finn_ota.backend import GrubBootBackend, SystemdBootBackend  # noqa: E402
from finn_ota.download import sha256_file  # noqa: E402
from finn_ota.health import run_health_check  # noqa: E402
from finn_ota.manifest import sign_manifest, verify_manifest  # noqa: E402
from finn_ota.rollout import compute_rollout_bucket, evaluate_rollout  # noqa: E402


class _RangeRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP server supporting RFC 7233 Range requests and optional fault injection."""

    disable_ranges = False
    truncate_first_get_bytes = None
    requests_log: list[tuple[str, str | None, int]] = []

    def log_message(self, format: str, *args) -> None:  # noqa: A002
        return

    def do_GET(self) -> None:  # noqa: N802
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            self.send_error(404, "Not Found")
            return

        total = os.path.getsize(path)
        range_hdr = self.headers.get("Range")

        if self.truncate_first_get_bytes is not None and range_hdr is None:
            limit = self.truncate_first_get_bytes
            self.__class__.truncate_first_get_bytes = None
            self.send_response(200)
            self.send_header("Content-Length", str(total))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            with open(path, "rb") as f:
                self.wfile.write(f.read(limit))
            self.requests_log.append((self.path, None, 200))
            return

        if range_hdr and not self.disable_ranges and range_hdr.startswith("bytes="):
            spec = range_hdr[len("bytes=") :].strip()
            start_s, _, end_s = spec.partition("-")
            start = int(start_s) if start_s else 0
            end = int(end_s) if end_s else total - 1
            if start >= total or start > end:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{total}")
                self.end_headers()
                self.requests_log.append((self.path, range_hdr, 416))
                return
            end = min(end, total - 1)
            length = end - start + 1
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{total}")
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            with open(path, "rb") as f:
                f.seek(start)
                self.wfile.write(f.read(length))
            self.requests_log.append((self.path, range_hdr, 206))
            return

        self.send_response(200)
        self.send_header("Content-Length", str(total))
        if not self.disable_ranges:
            self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        with open(path, "rb") as f:
            shutil.copyfileobj(f, self.wfile)
        self.requests_log.append((self.path, range_hdr, 200))


class _ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def _start_http_server(directory: str):
    class Handler(_RangeRequestHandler):
        requests_log = []

    bound = functools.partial(Handler, directory=directory)
    srv = _ThreadingHTTPServer(("127.0.0.1", 0), bound)
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()
    port = srv.server_address[1]
    return srv, Handler, f"http://127.0.0.1:{port}"


class OtaCryptoAndKeygenTest(unittest.TestCase):
    def test_ed25519_sign_verify_and_tamper_detection(self):
        seed, pub = ed25519.generate_keypair()
        priv_pem = ed25519.encode_private_pem(seed)
        pub_pem = ed25519.encode_public_pem(pub)

        self.assertEqual(ed25519.decode_private_pem(priv_pem), seed)
        self.assertEqual(ed25519.decode_public_pem(pub_pem), pub)

        manifest = {
            "schema_version": 1,
            "version": "0.3.0",
            "channel": "stable",
            "arch": "x86_64",
            "min_version": "0.1.0",
            "rollout": 100,
            "artifacts": [
                {
                    "url": "https://ota.finnos.org/stable/x86_64/0.3.0/finnos.img.zst",
                    "sha256": "a" * 64,
                    "size": 1024,
                    "delta_from": None,
                }
            ],
        }
        signed = sign_manifest(manifest, priv_pem)
        kid = verify_manifest(signed, [pub])
        self.assertEqual(kid, ed25519.key_id(pub))

        # Tamper a manifest field -> must fail verification
        tampered = json.loads(json.dumps(signed))
        tampered["rollout"] = 99
        with self.assertRaises(Exception):
            verify_manifest(tampered, [pub])

        # Wrong public key -> must fail verification
        _, other_pub = ed25519.generate_keypair()
        with self.assertRaises(Exception):
            verify_manifest(signed, [other_pub])

    def test_openssl_interop_when_available(self):
        openssl_bin = shutil.which("openssl")
        if not openssl_bin:
            self.skipTest("openssl not installed")
        ver = subprocess.run([openssl_bin, "version"], capture_output=True, text=True)
        if "LibreSSL" in ver.stdout:
            self.skipTest("system LibreSSL lacks Ed25519 pkeyutl -rawin")

        with tempfile.TemporaryDirectory() as d:
            priv = os.path.join(d, "priv.pem")
            pub = os.path.join(d, "pub.pem")
            msg = os.path.join(d, "msg.bin")
            sig = os.path.join(d, "msg.sig")
            payload = b"canonical-manifest-bytes"
            with open(msg, "wb") as f:
                f.write(payload)

            subprocess.run([openssl_bin, "genpkey", "-algorithm", "Ed25519", "-out", priv], check=True)
            subprocess.run([openssl_bin, "pkey", "-in", priv, "-pubout", "-out", pub], check=True)
            subprocess.run(
                [openssl_bin, "pkeyutl", "-sign", "-inkey", priv, "-rawin", "-in", msg, "-out", sig],
                check=True,
            )
            with open(pub, "r", encoding="utf-8") as f:
                pub_bytes = ed25519.decode_public_pem(f.read())
            with open(sig, "rb") as f:
                sig_bytes = f.read()
            self.assertTrue(ed25519.verify(pub_bytes, payload, sig_bytes))

    def test_keygen_refuses_git_repo_and_writes_outside(self):
        with tempfile.TemporaryDirectory() as fake_repo:
            os.makedirs(os.path.join(fake_repo, ".git"))
            inside = os.path.join(fake_repo, "ota", "keys")
            proc_bad = subprocess.run(
                [sys.executable, OTA_KEYGEN, "--out-dir", inside],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(proc_bad.returncode, 0)
            self.assertIn("refusing", proc_bad.stderr)

        with tempfile.TemporaryDirectory() as d:
            proc_ok = subprocess.run(
                [sys.executable, OTA_KEYGEN, "--out-dir", d, "--name", "testkey"],
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc_ok.returncode, 0, proc_ok.stderr)
            priv = os.path.join(d, "testkey-private.pem")
            pub = os.path.join(d, "testkey-public.pem")
            self.assertTrue(os.path.isfile(priv))
            self.assertTrue(os.path.isfile(pub))
            mode = stat.S_IMODE(os.stat(priv).st_mode)
            self.assertEqual(mode, 0o600)


class OtaEndToEndTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.keys_dir = os.path.join(self.root, "keys")
        self.repo_dir = os.path.join(self.root, "repo")
        self.state_dir = os.path.join(self.root, "state")
        self.cfg_path = os.path.join(self.root, "ota.conf")
        self.ver_path = os.path.join(self.root, "version")
        self.mid_path = os.path.join(self.root, "machine-id")
        self.wayland_sock = os.path.join(self.root, "wayland-0")

        os.makedirs(self.keys_dir, exist_ok=True)
        os.makedirs(self.repo_dir, exist_ok=True)
        os.makedirs(self.state_dir, exist_ok=True)

        with open(self.ver_path, "w", encoding="utf-8") as f:
            f.write("0.2.0\n")
        with open(self.mid_path, "w", encoding="utf-8") as f:
            f.write("finnos-test-machine-0001\n")
        with open(self.wayland_sock, "w", encoding="utf-8") as f:
            f.write("")

        subprocess.run(
            [sys.executable, OTA_KEYGEN, "--out-dir", self.keys_dir, "--name", "ota"],
            check=True,
            capture_output=True,
        )
        self.priv_key = os.path.join(self.keys_dir, "ota-private.pem")
        self.pub_key = os.path.join(self.keys_dir, "ota-public.pem")

        # Build two deterministic slot images (v0.2.0 and v0.3.0) with common prefix for delta
        self.img_v020 = os.path.join(self.root, "finnos-0.2.0.img")
        self.img_v030 = os.path.join(self.root, "finnos-0.3.0.img")
        shared_prefix = os.urandom(64 * 1024)
        with open(self.img_v020, "wb") as f:
            f.write(shared_prefix + b"FINNOS_VERSION=0.2.0\n" + b"\x11" * 16384)
        with open(self.img_v030, "wb") as f:
            f.write(shared_prefix + b"FINNOS_VERSION=0.3.0\n" + b"\x22" * 16384)

        # Seed fake backend slot A with exact v0.2.0 image bytes (for delta patching!)
        os.makedirs(os.path.join(self.state_dir, "slots"), exist_ok=True)
        shutil.copyfile(self.img_v020, os.path.join(self.state_dir, "slots", "a.img"))

        self.srv, self.handler_cls, self.base_url = _start_http_server(self.repo_dir)

        with open(self.cfg_path, "w", encoding="utf-8") as f:
            json.dump(
                {
                    "channel": "stable",
                    "base_url": self.base_url,
                    "arch": "x86_64",
                    "pubkey_path": self.pub_key,
                    "state_dir": self.state_dir,
                    "version_path": self.ver_path,
                    "machine_id_path": self.mid_path,
                    "backend": "fake",
                    "boot_tries": 3,
                },
                f,
            )

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.tmp.cleanup()

    def _run_cli(self, *args: str) -> tuple[int, dict]:
        proc = subprocess.run(
            [sys.executable, FINN_UPDATE, "--json", "--config", self.cfg_path, *args],
            capture_output=True,
            text=True,
        )
        data = json.loads(proc.stdout)
        return proc.returncode, data

    def _publish(self, version: str, image: str, rollout: int = 100, min_version: str = "0.1.0", prev_image: str | None = None, prev_version: str | None = None):
        cmd = [
            sys.executable,
            OTA_PUBLISH,
            "--version",
            version,
            "--channel",
            "stable",
            "--arch",
            "x86_64",
            "--image",
            image,
            "--key",
            self.priv_key,
            "--base-url",
            self.base_url,
            "--out-dir",
            self.repo_dir,
            "--rollout",
            str(rollout),
            "--min-version",
            min_version,
            "--notes",
            f"FinnOS {version}",
        ]
        if prev_image and prev_version:
            cmd.extend(["--prev-image", prev_image, "--prev-version", prev_version])
        subprocess.run(cmd, check=True, capture_output=True)

    def test_good_delta_update_installs_boots_and_commits(self):
        self._publish(
            "0.3.0",
            self.img_v030,
            prev_image=self.img_v020,
            prev_version="0.2.0",
        )

        rc, chk = self._run_cli("check")
        self.assertEqual(rc, 0)
        self.assertTrue(chk["update_available"])
        self.assertTrue(chk["eligible"])
        self.assertEqual(chk["selected_artifact"]["delta_from"], "0.2.0")

        rc, app = self._run_cli("apply")
        self.assertEqual(rc, 0, app)
        self.assertEqual(app["status"], "reboot_required")
        self.assertEqual(app["staged_slot"], "b")
        self.assertTrue(app["used_delta"])
        self.assertFalse(app["delta_fallback_to_full"])

        # Slot B now contains exact v0.3.0 bytes
        self.assertEqual(
            sha256_file(os.path.join(self.state_dir, "slots", "b.img")),
            sha256_file(self.img_v030),
        )

        # Boot into trial slot B
        rc, boot = self._run_cli("simulate-boot")
        self.assertEqual(rc, 0)
        self.assertEqual(boot["booted_slot"], "b")
        self.assertEqual(boot["tries_left"], 2)
        self.assertFalse(boot["slot_committed"])

        # Simulate healthy boot check & commit
        rc, hc = self._run_cli(
            "health-check", "--skip-systemd", "--wayland-socket", self.wayland_sock, "--commit"
        )
        self.assertEqual(rc, 0, hc)
        self.assertTrue(hc["healthy"])
        self.assertEqual(hc["committed_slot"], "b")

        rc, st = self._run_cli("status")
        self.assertEqual(rc, 0)
        self.assertEqual(st["active_slot"], "b")
        self.assertEqual(st["booted_slot"], "b")
        self.assertEqual(st["current_version"], "0.3.0")
        self.assertTrue(st["slot_committed"])
        self.assertEqual(st["tries_left"], 0)

    def test_broken_update_exhausts_boot_tries_and_rolls_back(self):
        self._publish("0.3.0", self.img_v030)

        rc, app = self._run_cli("apply")
        self.assertEqual(rc, 0, app)
        self.assertEqual(app["staged_slot"], "b")

        # Simulate 3 failed boots where health-check fails (missing version file -> unhealthy!)
        os.remove(self.ver_path)
        for expected_tries_left in (2, 1, 0):
            rc, boot = self._run_cli("simulate-boot")
            self.assertEqual(rc, 0)
            self.assertEqual(boot["booted_slot"], "b")
            self.assertEqual(boot["tries_left"], expected_tries_left)

            rc_hc, hc = self._run_cli("health-check", "--skip-systemd", "--commit")
            self.assertNotEqual(rc_hc, 0)
            self.assertFalse(hc["healthy"])
            self.assertIsNone(hc["committed_slot"])

        # 4th boot: tries exhausted -> automatic rollback to slot A (0.2.0)
        rc, boot_rb = self._run_cli("simulate-boot")
        self.assertEqual(rc, 0)
        self.assertEqual(boot_rb["booted_slot"], "a")
        self.assertEqual(boot_rb["active_slot"], "a")
        self.assertIsNone(boot_rb["pending_slot"])
        self.assertTrue(boot_rb["slot_committed"])

        rc, st = self._run_cli("status")
        self.assertEqual(rc, 0)
        self.assertEqual(st["active_slot"], "a")
        self.assertEqual(st["current_version"], "0.2.0")

    def test_tampered_artifact_and_bad_signature_rejected(self):
        self._publish("0.3.0", self.img_v030)
        manifest_path = os.path.join(self.repo_dir, "stable", "x86_64", "latest.json")

        # 1. Tamper manifest signature
        with open(manifest_path, "r", encoding="utf-8") as f:
            m = json.load(f)
        m_bad_sig = json.loads(json.dumps(m))
        m_bad_sig["rollout"] = 50
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(m_bad_sig, f)

        rc, err = self._run_cli("check")
        self.assertEqual(rc, 2)
        self.assertEqual(err["code"], "verification_failed")

        # Restore valid manifest, then tamper artifact bytes on server
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(m, f)
        art_file = os.path.join(
            self.repo_dir, "stable", "x86_64", "0.3.0", "finnos-0.3.0-x86_64.img.zst"
        )
        with open(art_file, "r+b") as f:
            f.seek(0)
            b0 = f.read(1)
            f.seek(0)
            f.write(bytes([b0[0] ^ 0xFF]))

        rc, err_dl = self._run_cli("apply")
        self.assertEqual(rc, 3)
        self.assertIn("sha256 mismatch", err_dl["error"])
        # Inactive slot b must remain empty/untouched
        self.assertEqual(os.path.getsize(os.path.join(self.state_dir, "slots", "b.img")), 0)

    def test_interrupted_download_resumes_with_http_range(self):
        self._publish("0.3.0", self.img_v030)
        # Force a partial download (.part file) and verify HTTP 206 resume completes it
        art_name = "finnos-0.3.0-x86_64.img.zst"
        src_art = os.path.join(self.repo_dir, "stable", "x86_64", "0.3.0", art_name)
        total = os.path.getsize(src_art)
        part_dir = os.path.join(self.state_dir, "downloads", "0.3.0")
        os.makedirs(part_dir, exist_ok=True)
        part_path = os.path.join(part_dir, art_name + ".part")

        split = max(1, total // 2)
        with open(src_art, "rb") as src, open(part_path, "wb") as dst:
            dst.write(src.read(split))

        self.handler_cls.requests_log.clear()
        rc, dl = self._run_cli("download")
        self.assertEqual(rc, 0, dl)
        self.assertEqual(dl["resumed_from"], split)
        self.assertEqual(dl["bytes_downloaded"], total - split)
        self.assertEqual(dl["total_bytes"], total)
        self.assertEqual(dl["sha256"], sha256_file(src_art))
        # Verify server saw Range header and returned 206
        art_reqs = [r for r in self.handler_cls.requests_log if art_name in r[0]]
        self.assertTrue(any(r[1] == f"bytes={split}-" and r[2] == 206 for r in art_reqs))

    def test_staged_rollout_bucketing_and_min_version(self):
        # Find two machine IDs: one in bucket < 25, one in bucket >= 25
        low_id = None
        high_id = None
        for i in range(1000):
            mid = f"machine-{i:04d}"
            b = compute_rollout_bucket(mid, "stable", "0.3.0")
            if b < 25 and low_id is None:
                low_id = mid
            elif b >= 25 and high_id is None:
                high_id = mid
            if low_id and high_id:
                break
        self.assertIsNotNone(low_id)
        self.assertIsNotNone(high_id)
        # Deterministic check
        self.assertEqual(
            compute_rollout_bucket(low_id, "stable", "0.3.0"),
            compute_rollout_bucket(low_id, "stable", "0.3.0"),
        )
        self.assertTrue(evaluate_rollout(25, compute_rollout_bucket(low_id, "stable", "0.3.0") and 0 or low_id, "stable", "0.3.0")[0])

        self._publish("0.3.0", self.img_v030, rollout=25)

        # High bucket machine is deferred
        rc, chk_hi = self._run_cli("--machine-id", high_id, "check")
        self.assertEqual(rc, 0)
        self.assertEqual(chk_hi["status"], "deferred_rollout")
        self.assertFalse(chk_hi["eligible"])

        rc_ap_hi, ap_hi = self._run_cli("--machine-id", high_id, "apply")
        self.assertEqual(rc_ap_hi, 4)
        self.assertEqual(ap_hi["code"], "policy_refused")

        # Low bucket machine is eligible
        rc, chk_lo = self._run_cli("--machine-id", low_id, "check")
        self.assertEqual(rc, 0)
        self.assertEqual(chk_lo["status"], "update_available")
        self.assertTrue(chk_lo["eligible"])

        # min_version enforcement
        self._publish("0.4.0", self.img_v030, rollout=100, min_version="0.3.0")
        rc, chk_min = self._run_cli("check")
        self.assertEqual(rc, 0)
        self.assertEqual(chk_min["status"], "blocked_min_version")
        self.assertFalse(chk_min["eligible"])

    def test_delta_mismatch_falls_back_to_full_artifact(self):
        # Corrupt slot A so delta_from=0.2.0 fails zstd --patch-from, verifying automatic full fallback
        with open(os.path.join(self.state_dir, "slots", "a.img"), "wb") as f:
            f.write(b"UNEXPECTED_BASE_CONTENT" * 100)

        self._publish(
            "0.3.0",
            self.img_v030,
            prev_image=self.img_v020,
            prev_version="0.2.0",
        )
        rc, app = self._run_cli("apply")
        self.assertEqual(rc, 0, app)
        self.assertTrue(app["delta_fallback_to_full"])
        self.assertFalse(app["used_delta"])
        self.assertEqual(
            sha256_file(os.path.join(self.state_dir, "slots", "b.img")),
            sha256_file(self.img_v030),
        )

    def test_manual_rollback(self):
        self._publish("0.3.0", self.img_v030)
        self._run_cli("apply")
        self._run_cli("simulate-boot")
        self._run_cli("commit")

        rc, rb = self._run_cli("rollback", "--reason", "regression")
        self.assertEqual(rc, 0)
        self.assertEqual(rb["target_slot"], "a")

        # Verify flags work after the subcommand as well (e.g. `finn-update status --json`)
        proc = subprocess.run(
            [sys.executable, FINN_UPDATE, "status", "--json", "--config", self.cfg_path],
            capture_output=True,
            text=True,
            check=True,
        )
        st = json.loads(proc.stdout)
        self.assertEqual(st["current_version"], "0.3.0")

    def test_config_command_and_no_range_fallback_and_downgrade_ignored(self):
        # Change channel to beta and back to stable
        rc, cfg_out = self._run_cli("config", "--channel", "beta", "--auto-check", "false")
        self.assertEqual(rc, 0)
        self.assertEqual(cfg_out["channel"], "beta")
        self.assertFalse(cfg_out["auto_check"])
        rc, cfg_back = self._run_cli("config", "--channel", "stable", "--auto-check", "true")
        self.assertEqual(rc, 0)
        self.assertEqual(cfg_back["channel"], "stable")

        # Downgrade/same-version manifest reports up_to_date
        self._publish("0.1.0", self.img_v020)
        rc, chk = self._run_cli("check")
        self.assertEqual(rc, 0)
        self.assertEqual(chk["status"], "up_to_date")
        self.assertFalse(chk["update_available"])

        # Server ignores Range and sends 200 OK -> downloader restarts cleanly from 0
        self._publish("0.3.0", self.img_v030)
        art_name = "finnos-0.3.0-x86_64.img.zst"
        part_dir = os.path.join(self.state_dir, "downloads", "0.3.0")
        os.makedirs(part_dir, exist_ok=True)
        with open(os.path.join(part_dir, art_name + ".part"), "wb") as f:
            f.write(b"stale-partial-bytes")
        self.handler_cls.disable_ranges = True
        try:
            rc, dl = self._run_cli("download")
            self.assertEqual(rc, 0, dl)
            self.assertEqual(dl["resumed_from"], 0)
        finally:
            self.handler_cls.disable_ranges = False


class BootBackendsAndUnitsTest(unittest.TestCase):
    def test_systemd_boot_backend_and_grub_backend_with_temp_boot(self):
        with tempfile.TemporaryDirectory() as d:
            boot_dir = os.path.join(d, "boot")
            entries = os.path.join(boot_dir, "loader", "entries")
            os.makedirs(entries, exist_ok=True)
            os.makedirs(os.path.join(boot_dir, "grub"), exist_ok=True)

            cmdline = os.path.join(d, "cmdline")
            ver = os.path.join(d, "version")
            slot_a = os.path.join(d, "slot_a.img")
            slot_b = os.path.join(d, "slot_b.img")
            img_new = os.path.join(d, "new.img")
            with open(cmdline, "w", encoding="utf-8") as f:
                f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
            with open(ver, "w", encoding="utf-8") as f:
                f.write("0.2.0\n")
            with open(slot_a, "wb") as f:
                f.write(b"slot-a-0.2.0")
            with open(slot_b, "wb") as f:
                f.write(b"")
            with open(img_new, "wb") as f:
                f.write(b"slot-b-0.3.0")

            # Fake bootctl shim
            bootctl = os.path.join(d, "bootctl")
            with open(bootctl, "w", encoding="utf-8") as f:
                f.write("#!/bin/sh\nexit 0\n")
            os.chmod(bootctl, 0o755)

            sb = SystemdBootBackend(
                boot_dir=boot_dir,
                cmdline_path=cmdline,
                version_path=ver,
                slot_a=slot_a,
                slot_b=slot_b,
                bootctl_bin=bootctl,
                state_dir=os.path.join(d, "sb_state"),
            )
            st = sb.get_status()
            self.assertEqual(st.layout, "ab")
            self.assertEqual(st.booted_slot, "a")
            sb.stage_image("b", img_new, "0.3.0")
            sb.mark_bootable_once("b", tries=3, version="0.3.0")
            self.assertTrue(os.path.isfile(os.path.join(entries, "finnos-b+3.conf")))

            # Simulate booted on slot B and commit
            with open(cmdline, "w", encoding="utf-8") as f:
                f.write("root=PARTLABEL=FINNOS_B rw finn.slot=b quiet splash\n")
            sb.commit_booted_slot()
            self.assertTrue(os.path.isfile(os.path.join(entries, "finnos-b.conf")))
            self.assertFalse(os.path.exists(os.path.join(entries, "finnos-b+3.conf")))

            # GRUB backend test (uses internal 1024-byte grubenv writer when grub-editenv absent)
            with open(cmdline, "w", encoding="utf-8") as f:
                f.write("root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash\n")
            gb = GrubBootBackend(
                boot_dir=boot_dir,
                cmdline_path=cmdline,
                version_path=ver,
                slot_a=slot_a,
                slot_b=slot_b,
                grub_editenv_bin="/nonexistent/grub-editenv",
            )
            gb.mark_bootable_once("b", tries=3, version="0.3.0")
            self.assertEqual(os.path.getsize(os.path.join(boot_dir, "grub", "grubenv")), 1024)
            gst = gb.get_status()
            self.assertEqual(gst.pending_slot, "b")
            self.assertEqual(gst.tries_left, 3)

            # Single-slot detection check
            gb_single = GrubBootBackend(
                boot_dir=boot_dir,
                cmdline_path=cmdline,
                version_path=ver,
                slot_a="/nonexistent/FINNOS_A",
                slot_b="/nonexistent/FINNOS_B",
            )
            self.assertEqual(gb_single.get_status().layout, "single")

    def test_health_check_requires_compositor_session_and_wayland(self):
        with tempfile.TemporaryDirectory() as d:
            ver = os.path.join(d, "version")
            sock = os.path.join(d, "wayland-0")
            with open(ver, "w", encoding="utf-8") as f:
                f.write("0.2.0\n")
            with open(sock, "w", encoding="utf-8") as f:
                f.write("")

            ok = run_health_check(
                version_path=ver,
                wayland_socket=sock,
                service_probe=lambda u: True,
                socket_probe=os.path.exists,
            )
            self.assertTrue(ok.healthy)

            # Missing finn-comp -> unhealthy
            bad = run_health_check(
                version_path=ver,
                wayland_socket=sock,
                service_probe=lambda u: u != "finn-comp.service",
                socket_probe=os.path.exists,
            )
            self.assertFalse(bad.healthy)
            self.assertFalse(bad.checks["unit:finn-comp.service"])

    def test_systemd_units_and_grub_config_exist_and_wire_health_commit(self):
        with open(os.path.join(ROOT, "base", "finn-update-check.service"), encoding="utf-8") as f:
            chk_svc = f.read()
        with open(os.path.join(ROOT, "base", "finn-update-check.timer"), encoding="utf-8") as f:
            chk_tmr = f.read()
        with open(os.path.join(ROOT, "base", "finn-boot-health.service"), encoding="utf-8") as f:
            health_svc = f.read()
        with open(os.path.join(ROOT, "ota", "grub-ab.cfg"), encoding="utf-8") as f:
            grub_cfg = f.read()

        self.assertIn("finn-update check", chk_svc)
        self.assertIn("[Timer]", chk_tmr)
        self.assertIn("finn-comp.service", health_svc)
        self.assertIn("finn-session.service", health_svc)
        self.assertIn("finn-update health-check", health_svc)
        self.assertIn("--commit", health_svc)
        self.assertIn("finn_tries_left", grub_cfg)
        self.assertIn("finn_pending_slot", grub_cfg)


if __name__ == "__main__":
    unittest.main()
