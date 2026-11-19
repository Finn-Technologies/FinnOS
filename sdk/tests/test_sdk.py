from __future__ import annotations

import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FINN_SDK = ROOT / "tools" / "finn-sdk"
SCHEMA_PATH = ROOT / "sdk" / "schema" / "finn-app.schema.json"
TEMPLATE_DIR = ROOT / "sdk" / "template"


def run_cli(*args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        [sys.executable, str(FINN_SDK), *args],
        text=True,
        capture_output=True,
        env=merged_env,
    )


class FinnSdkTest(unittest.TestCase):
    def test_schema_and_template_manifest_are_valid(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertIn("permissions", schema["properties"])

        res = run_cli("validate", str(TEMPLATE_DIR), "--json")
        self.assertEqual(res.returncode, 0, res.stderr)
        out = json.loads(res.stdout)
        self.assertTrue(out["valid"])
        self.assertEqual(out["id"], "counter")

        ci_file = TEMPLATE_DIR / ".github" / "workflows" / "ci.yml"
        self.assertTrue(ci_file.is_file())
        ci_text = ci_file.read_text(encoding="utf-8")
        self.assertIn("finn-sdk validate", ci_text)
        self.assertIn("finn-sdk package", ci_text)

    def test_new_scaffolds_project_and_handles_path_target(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            app_dir = tmp_path / "notes-lite"
            res = run_cli("new", "notes-lite", "--dir", str(app_dir), "--name", "Notes Lite")
            self.assertEqual(res.returncode, 0, res.stderr)
            for rel in (
                "CMakeLists.txt",
                "Makefile",
                "main.cpp",
                "finn-app.desktop",
                "finn-app.json",
                ".github/workflows/ci.yml",
            ):
                self.assertTrue((app_dir / rel).is_file(), f"Missing {rel}")

            val = run_cli("validate", str(app_dir))
            self.assertEqual(val.returncode, 0, val.stderr)

            # Verify Finn Store style path argument works cleanly
            store_target = tmp_path / "Projects" / "finn-mail"
            res2 = run_cli("new", str(store_target))
            self.assertEqual(res2.returncode, 0, res2.stderr)
            manifest2 = json.loads((store_target / "finn-app.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest2["id"], "finn-mail")

    def test_validate_rejects_invalid_manifests(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "finn-app.desktop").write_text("[Desktop Entry]\nName=Test\n", encoding="utf-8")

            bad_cases = [
                {"schema_version": True, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "bin"},
                {"schema_version": 1, "id": "BadUpper", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "bin"},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "not-semver", "summary": "s", "executable": "bin"},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "../evil"},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "/abs/bin"},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "bin", "permissions": ["root:all"]},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "bin", "form_factors": ["watch"]},
                {"schema_version": 1, "id": "ok", "name": "Ok", "version": "0.1.0", "summary": "s", "executable": "bin", "extra_field": 1},
            ]
            for idx, payload in enumerate(bad_cases):
                m = d / "finn-app.json"
                m.write_text(json.dumps(payload), encoding="utf-8")
                res = run_cli("validate", str(d), "--json")
                self.assertNotEqual(res.returncode, 0, f"Case {idx} unexpectedly passed")
                parsed = json.loads(res.stdout)
                self.assertFalse(parsed["valid"])
                self.assertTrue(len(parsed["errors"]) > 0)

            # Strict mode fails when executable is missing
            good = {
                "schema_version": 1,
                "id": "valid-app",
                "name": "Valid App",
                "version": "0.1.0",
                "summary": "Valid summary",
                "executable": "finn-valid-app",
                "desktop_entry": "finn-app.desktop",
            }
            (d / "finn-app.json").write_text(json.dumps(good), encoding="utf-8")
            self.assertEqual(run_cli("validate", str(d)).returncode, 0)
            self.assertNotEqual(run_cli("validate", str(d), "--strict").returncode, 0)

    def test_deterministic_signed_packaging_and_tamper_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            app_dir = tmp_path / "demo"
            self.assertEqual(run_cli("new", "demo", "--dir", str(app_dir), "--name", "Demo").returncode, 0)

            # Add local icon and asset
            (app_dir / "assets").mkdir()
            (app_dir / "assets" / "demo.svg").write_text("<svg></svg>\n", encoding="utf-8")
            (app_dir / "assets" / "data.txt").write_text("hello\n", encoding="utf-8")
            manifest = json.loads((app_dir / "finn-app.json").read_text(encoding="utf-8"))
            manifest["icon"] = "assets/demo.svg"
            manifest["assets"] = ["assets/data.txt"]
            manifest["permissions"] = ["filesystem:home-read"]
            (app_dir / "finn-app.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

            # Fake built binary
            build_dir = app_dir / "build"
            build_dir.mkdir()
            fake_bin = build_dir / "finn-demo"
            fake_bin.write_bytes(b"\x7fELFdummy-binary-payload")
            os.chmod(fake_bin, 0o755)

            # Refuses key generation inside the git repository
            in_repo = run_cli("keygen", str(ROOT / "sdk" / "should-not-exist"))
            self.assertNotEqual(in_repo.returncode, 0)

            # Keygen
            key_prefix = tmp_path / "keys" / "pub"
            kg = run_cli("keygen", str(key_prefix))
            self.assertEqual(kg.returncode, 0, kg.stderr)
            priv_key = Path(f"{key_prefix}.key.pem")
            pub_key = Path(f"{key_prefix}.pub.pem")
            self.assertEqual(priv_key.stat().st_mode & 0o777, 0o600)

            # Refuses overwrite without --force
            self.assertNotEqual(run_cli("keygen", str(key_prefix)).returncode, 0)

            # Second keypair for wrong-key test
            wrong_prefix = tmp_path / "keys" / "wrong"
            self.assertEqual(run_cli("keygen", str(wrong_prefix)).returncode, 0)
            wrong_pub = Path(f"{wrong_prefix}.pub.pem")

            # Deterministic packaging twice
            pkg1 = tmp_path / "out1.finnapp"
            pkg2 = tmp_path / "out2.finnapp"
            p1 = run_cli("package", str(app_dir), "--key", str(priv_key), "--arch", "x86_64", "-o", str(pkg1))
            p2 = run_cli("package", str(app_dir), "--key", str(priv_key), "--arch", "x86_64", "-o", str(pkg2))
            self.assertEqual(p1.returncode, 0, p1.stderr)
            self.assertEqual(p2.returncode, 0, p2.stderr)
            self.assertEqual(
                hashlib.sha256(pkg1.read_bytes()).hexdigest(),
                hashlib.sha256(pkg2.read_bytes()).hexdigest(),
                "Signed .finnapp output must be bit-for-bit deterministic",
            )

            # Verify valid signed package
            v_ok = run_cli("verify", str(pkg1), "--pubkey", str(pub_key), "--json")
            self.assertEqual(v_ok.returncode, 0, v_ok.stdout + v_ok.stderr)
            v_data = json.loads(v_ok.stdout)
            self.assertTrue(v_data["valid"])
            self.assertTrue(v_data["signed"])

            # Verify fails with wrong public key
            v_wrong = run_cli("verify", str(pkg1), "--pubkey", str(wrong_pub))
            self.assertNotEqual(v_wrong.returncode, 0)

            # Verify fails if payload is tampered
            tampered_payload = tmp_path / "tampered_payload.finnapp"
            with tarfile.open(pkg1, "r:") as src, tarfile.open(tampered_payload, "w") as dst:
                for m in src.getmembers():
                    data = src.extractfile(m).read()
                    if m.name == "payload.tar.zst":
                        data = data[:-4] + b"XXXX"
                        m.size = len(data)
                    dst.addfile(m, io.BytesIO(data))
            self.assertNotEqual(run_cli("verify", str(tampered_payload), "--pubkey", str(pub_key)).returncode, 0)

            # Verify fails if manifest is tampered
            tampered_manifest = tmp_path / "tampered_manifest.finnapp"
            with tarfile.open(pkg1, "r:") as src, tarfile.open(tampered_manifest, "w") as dst:
                for m in src.getmembers():
                    data = src.extractfile(m).read()
                    if m.name == "manifest.json":
                        mj = json.loads(data.decode("utf-8"))
                        mj["summary"] = "Tampered summary"
                        data = (json.dumps(mj, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
                        m.size = len(data)
                    dst.addfile(m, io.BytesIO(data))
            self.assertNotEqual(run_cli("verify", str(tampered_manifest), "--pubkey", str(pub_key)).returncode, 0)

            # Verify fails on path traversal inside archive
            traversal_pkg = tmp_path / "traversal.finnapp"
            with tarfile.open(pkg1, "r:") as src, tarfile.open(traversal_pkg, "w") as dst:
                for m in src.getmembers():
                    data = src.extractfile(m).read()
                    dst.addfile(m, io.BytesIO(data))
                bad = tarfile.TarInfo(name="../evil")
                bad_bytes = b"pwn"
                bad.size = len(bad_bytes)
                dst.addfile(bad, io.BytesIO(bad_bytes))
            self.assertNotEqual(run_cli("verify", str(traversal_pkg), "--pubkey", str(pub_key)).returncode, 0)

            # Unsigned workflow
            pkg_uns = tmp_path / "unsigned.finnapp"
            self.assertEqual(
                run_cli("package", str(app_dir), "--unsigned", "--arch", "aarch64", "-o", str(pkg_uns)).returncode,
                0,
            )
            self.assertNotEqual(run_cli("verify", str(pkg_uns)).returncode, 0)
            self.assertEqual(run_cli("verify", str(pkg_uns), "--allow-unsigned").returncode, 0)

    def test_simulate_dry_run_presets(self) -> None:
        expectations = {
            "phone": (390, 844, 2.0, "portrait"),
            "tablet": (768, 1024, 2.0, "portrait"),
            "desktop": (1440, 900, 1.0, "landscape"),
        }
        for ff, (w, h, scale, orient) in expectations.items():
            res = run_cli("simulate", "--form-factor", ff, "--dry-run", "--json", str(TEMPLATE_DIR))
            self.assertEqual(res.returncode, 0, res.stderr)
            info = json.loads(res.stdout)
            self.assertEqual(info["form_factor"], ff)
            self.assertEqual(info["orientation"], orient)
            self.assertEqual(info["width"], w)
            self.assertEqual(info["height"], h)
            self.assertEqual(info["scale"], scale)
            self.assertEqual(info["env"]["FINN_FORM_FACTOR"], ff)
            self.assertEqual(info["env"]["FINN_OUTPUT_MODE"], f"{w}x{h}")

        # Landscape phone override
        res_land = run_cli(
            "simulate", "--form-factor", "phone", "--orientation", "landscape", "--dry-run", "--json", str(TEMPLATE_DIR)
        )
        self.assertEqual(res_land.returncode, 0)
        info_land = json.loads(res_land.stdout)
        self.assertEqual((info_land["width"], info_land["height"]), (844, 390))

    def test_register_and_check(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            res = run_cli(
                "register",
                "my-tool",
                "My Tool",
                "/usr/local/bin/finn-my-tool",
                "terminal",
                "Test note",
                env={"XDG_DATA_HOME": tmp},
            )
            self.assertEqual(res.returncode, 0, res.stderr)
            desktop = Path(tmp) / "applications" / "finn-my-tool.desktop"
            self.assertTrue(desktop.is_file())
            text = desktop.read_text(encoding="utf-8")
            self.assertIn("X-Finn-Id=my-tool", text)
            self.assertIn("Exec=/usr/local/bin/finn-my-tool", text)

        chk = run_cli("check")
        self.assertEqual(chk.returncode, 0, chk.stderr)
        self.assertIn("openssl", chk.stdout)
        self.assertIn("zstd", chk.stdout)


if __name__ == "__main__":
    unittest.main()
