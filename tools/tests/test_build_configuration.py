import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.finnlib.build import BootMode, output_directory
from tools.finnlib.cli import command, main, parse_arguments
from tools.finnlib.config import ConfigurationError, load_configuration
from tools.finnlib.image import stage_esp


ROOT = Path(__file__).resolve().parents[2]


class BuildConfigurationTests(unittest.TestCase):
    def test_repository_configuration_selects_development_and_release(self):
        configuration = load_configuration(ROOT)
        target, development = configuration.select(None, None)
        self.assertEqual(target.name, "x86_64-qemu")
        self.assertEqual(target.kernel_cargo_target, "x86_64-unknown-none")
        self.assertEqual(development.cargo_args, ())

        _target, release = configuration.select("x86_64-qemu", "release")
        self.assertEqual(release.cargo_args, ("--release",))
        self.assertEqual(
            output_directory(ROOT, target, release, BootMode.FIRST_BOOT).name,
            "x86_64-qemu-test-release",
        )

    def test_arm64_and_unknown_targets_are_classified(self):
        configuration = load_configuration(ROOT)
        arm64, _profile = configuration.select("arm64-qemu", "development")
        self.assertEqual(arm64.boot_filename, "BOOTAA64.EFI")
        self.assertEqual(arm64.kernel_cargo_target, "aarch64-unknown-none")
        self.assertEqual(arm64.boot_cargo_target, "aarch64-unknown-uefi")
        self.assertEqual(arm64.qemu_system, "qemu-system-aarch64")
        self.assertEqual(arm64.qemu_machine, "virt")
        self.assertEqual(arm64.qemu_cpu, "cortex-a72")
        with self.assertRaisesRegex(ConfigurationError, "unknown target"):
            configuration.select("missing", "development")
        with self.assertRaisesRegex(ConfigurationError, "unknown profile"):
            configuration.select("x86_64-qemu", "fast")

    def test_duplicate_and_incomplete_cli_options_are_rejected(self):
        self.assertEqual(
            parse_arguments(["image", "--target", "x86_64-qemu", "--profile", "release"]),
            ("image", "x86_64-qemu", "release"),
        )
        with self.assertRaisesRegex(ConfigurationError, "only once"):
            parse_arguments(["image", "--profile", "release", "--profile", "development"])
        with self.assertRaisesRegex(ConfigurationError, "requires a value"):
            parse_arguments(["image", "--target"])
        with self.assertRaisesRegex(ConfigurationError, "unknown argument"):
            parse_arguments(["image", "--fast"])

    def test_arm64_rejects_post_r3_boot_modes(self):
        with self.assertRaisesRegex(ConfigurationError, "R3 supports serial first boot only"):
            command("test-exceptions", "arm64-qemu", "development")

    def test_target_metadata_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "target.toml").write_text(
                'name = "sample"\narchitecture = "arm64"\nplatform = "qemu"\nfirmware = "uefi"\n'
                'status = "planned"\nbootable = false\n',
                encoding="utf-8",
            )
            (root / "Finnfile.toml").write_text(
                '[project]\ndefault_target = "sample"\n'
                '[targets.sample]\narchitecture = "x86_64"\nplatform = "qemu"\n'
                'firmware = "uefi"\nstatus = "planned"\nbootable = false\n'
                'configuration = "target.toml"\n'
                '[profiles.development]\ncargo_profile = "debug"\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ConfigurationError, "disagrees"):
                load_configuration(root)

    def test_subprocess_failure_reports_command_and_captured_output(self):
        failure = subprocess.CalledProcessError(
            7, ["image-tool", "create"], output=b"tool output", stderr=b"tool error"
        )
        stderr = io.StringIO()
        with patch.object(sys, "argv", ["finn", "build"]), patch(
            "tools.finnlib.cli.command", side_effect=failure
        ), contextlib.redirect_stderr(stderr):
            self.assertEqual(main(), 7)
        rendered = stderr.getvalue()
        self.assertIn("image-tool create", rendered)
        self.assertIn("tool output", rendered)
        self.assertIn("tool error", rendered)

    def test_staged_esp_uses_configured_names_and_removes_stale_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            boot = root / "boot.efi"
            kernel = root / "kernel.elf"
            boot.write_bytes(b"boot")
            kernel.write_bytes(b"kernel")
            stale = root / "out" / "esp" / "stale"
            stale.mkdir(parents=True)
            (stale / "old").write_bytes(b"old")

            esp = stage_esp(root / "out", boot, kernel, "BOOTTEST.EFI", "TEST.ELF")
            self.assertFalse((esp / "stale").exists())
            self.assertEqual((esp / "EFI" / "BOOT" / "BOOTTEST.EFI").read_bytes(), b"boot")
            self.assertEqual(
                (esp / "EFI" / "FINNOS" / "TEST.ELF").read_bytes(), b"kernel"
            )


if __name__ == "__main__":
    unittest.main()
