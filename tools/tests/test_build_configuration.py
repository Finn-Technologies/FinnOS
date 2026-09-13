import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.finnlib.build import BootMode, generate_reproducible_manifest, output_directory
from tools.finnlib.cli import BOOT_MODES, command, main, parse_arguments
from tools.finnlib.config import ConfigurationError, load_configuration
from tools.finnlib.image import make_data_image, stage_esp


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
        self.assertEqual(arm64.qemu_machine, "virt,gic-version=2,secure=off")
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

    def test_arm64_rejects_modes_not_yet_implemented(self):
        for mode in ("test-page-allocator", "test-heap", "test-preemption-context"):
            with self.subTest(mode=mode), self.assertRaisesRegex(
                ConfigurationError, "not implemented for ARM64"
            ):
                command(mode, "arm64-qemu", "development")

    def test_x86_rejects_arm64_only_modes(self):
        for mode in ("test-arm64-exception-fatal", "test-arm64-gic"):
            with self.subTest(mode=mode), self.assertRaisesRegex(
                ConfigurationError, "implemented only for arm64-qemu"
            ):
                command(mode, "x86_64-qemu", "development")

    def test_ipc_mode_is_defined_and_feature_isolated(self):
        self.assertIs(BOOT_MODES["test-ipc"], BootMode.IPC)
        self.assertEqual(BootMode.IPC.suffix, "ipc")
        self.assertTrue(BootMode.IPC.test_exit)
        self.assertIn("kernel-bin", BootMode.IPC.kernel_features)
        self.assertIn("qemu-test-exit", BootMode.IPC.kernel_features)
        self.assertIn("qemu-test-ipc", BootMode.IPC.kernel_features)
        self.assertNotIn("qemu-test-userspace", BootMode.IPC.kernel_features)
        self.assertNotIn("qemu-test-ipc", BootMode.USERSPACE.kernel_features)
        configuration = load_configuration(ROOT)
        x86, profile = configuration.select("x86_64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, x86, profile, BootMode.IPC).name,
            "x86_64-qemu-ipc",
        )
        arm64, _ = configuration.select("arm64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, arm64, profile, BootMode.IPC).name,
            "arm64-qemu-ipc",
        )
        self.assertNotIn("test-ipc", ("test-page-allocator", "test-heap", "test-preemption-context"))

    def test_elf_loader_mode_is_defined_and_feature_isolated(self):
        self.assertIs(BOOT_MODES["test-elf-loader"], BootMode.ELF_LOADER)
        self.assertEqual(BootMode.ELF_LOADER.suffix, "elf-loader")
        self.assertTrue(BootMode.ELF_LOADER.test_exit)
        self.assertIn("kernel-bin", BootMode.ELF_LOADER.kernel_features)
        self.assertIn("qemu-test-exit", BootMode.ELF_LOADER.kernel_features)
        self.assertIn("qemu-test-elf-loader", BootMode.ELF_LOADER.kernel_features)
        self.assertNotIn("qemu-test-ipc", BootMode.ELF_LOADER.kernel_features)
        configuration = load_configuration(ROOT)
        x86, profile = configuration.select("x86_64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, x86, profile, BootMode.ELF_LOADER).name,
            "x86_64-qemu-elf-loader",
        )
        arm64, _ = configuration.select("arm64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, arm64, profile, BootMode.ELF_LOADER).name,
            "arm64-qemu-elf-loader",
        )

    def test_init_mode_is_defined_and_feature_isolated(self):
        self.assertIs(BOOT_MODES["test-init"], BootMode.INIT)
        self.assertEqual(BootMode.INIT.suffix, "init")
        self.assertTrue(BootMode.INIT.test_exit)
        self.assertIn("kernel-bin", BootMode.INIT.kernel_features)
        self.assertIn("qemu-test-exit", BootMode.INIT.kernel_features)
        self.assertIn("qemu-test-init", BootMode.INIT.kernel_features)
        self.assertNotIn("qemu-test-ipc", BootMode.INIT.kernel_features)
        configuration = load_configuration(ROOT)
        x86, profile = configuration.select("x86_64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, x86, profile, BootMode.INIT).name,
            "x86_64-qemu-init",
        )
        arm64, _ = configuration.select("arm64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, arm64, profile, BootMode.INIT).name,
            "arm64-qemu-init",
        )

    def test_desktop_mode_is_defined_and_feature_isolated(self):
        self.assertIs(BOOT_MODES["test-desktop"], BootMode.DESKTOP)
        self.assertEqual(BootMode.DESKTOP.suffix, "desktop")
        self.assertTrue(BootMode.DESKTOP.test_exit)
        self.assertIn("kernel-bin", BootMode.DESKTOP.kernel_features)
        self.assertIn("qemu-test-exit", BootMode.DESKTOP.kernel_features)
        self.assertIn("qemu-test-desktop", BootMode.DESKTOP.kernel_features)
        self.assertNotIn("qemu-test-init", BootMode.DESKTOP.kernel_features)
        configuration = load_configuration(ROOT)
        x86, profile = configuration.select("x86_64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, x86, profile, BootMode.DESKTOP).name,
            "x86_64-qemu-desktop",
        )
        arm64, _ = configuration.select("arm64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, arm64, profile, BootMode.DESKTOP).name,
            "arm64-qemu-desktop",
        )

    def test_arm64_exception_output_is_feature_isolated(self):
        configuration = load_configuration(ROOT)
        target, profile = configuration.select("arm64-qemu", "development")
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.EXCEPTIONS).name,
            "arm64-qemu-exceptions",
        )
        self.assertNotIn("qemu-test-exceptions", BootMode.FIRST_BOOT.kernel_features)
        self.assertIn("qemu-test-exceptions", BootMode.EXCEPTIONS.kernel_features)
        self.assertIn(
            "qemu-test-arm64-exception-fatal",
            BootMode.ARM64_EXCEPTION_FATAL.kernel_features,
        )
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.MEMORY_MAP).name,
            "arm64-qemu-memory-map",
        )
        self.assertIn("qemu-test-memory-map", BootMode.MEMORY_MAP.kernel_features)
        self.assertNotIn("qemu-test-exceptions", BootMode.MEMORY_MAP.kernel_features)
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.PAGE_TABLES).name,
            "arm64-qemu-page-tables",
        )
        self.assertIn("qemu-test-page-tables", BootMode.PAGE_TABLES.kernel_features)
        self.assertNotIn("qemu-test-memory-map", BootMode.PAGE_TABLES.kernel_features)
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.ARM64_GIC).name,
            "arm64-qemu-arm64-gic",
        )
        self.assertIn("qemu-test-arm64-gic", BootMode.ARM64_GIC.kernel_features)
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.TIMER).name,
            "arm64-qemu-timer-interrupts",
        )
        self.assertIn("qemu-test-timer-interrupts", BootMode.TIMER.kernel_features)
        self.assertEqual(
            output_directory(ROOT, target, profile, BootMode.COOPERATIVE_TASKS).name,
            "arm64-qemu-cooperative-tasks",
        )
        self.assertIn("qemu-test-cooperative-tasks", BootMode.COOPERATIVE_TASKS.kernel_features)
        self.assertNotIn("qemu-test-cooperative-tasks", BootMode.TIMER.kernel_features)
        self.assertNotIn("qemu-test-timer-interrupts", BootMode.COOPERATIVE_TASKS.kernel_features)
        for other in (
            BootMode.FIRST_BOOT,
            BootMode.EXCEPTIONS,
            BootMode.ARM64_EXCEPTION_FATAL,
            BootMode.MEMORY_MAP,
            BootMode.PAGE_TABLES,
            BootMode.TIMER,
            BootMode.COOPERATIVE_TASKS,
        ):
            with self.subTest(other=other):
                self.assertNotIn("qemu-test-arm64-gic", other.kernel_features)

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

    def test_data_image_configuration_and_cli_plumbing(self):
        configuration = load_configuration(ROOT)
        x86, _ = configuration.select("x86_64-qemu", "development")
        self.assertEqual(x86.data_image, "finnos-x86_64-data.img")

        arm64, _ = configuration.select("arm64-qemu", "development")
        self.assertEqual(arm64.data_image, "finnos-arm64-data.img")

        args = parse_arguments(["run", "--data-drive", "custom-data.img"])
        self.assertEqual(args.data_drive, "custom-data.img")
        self.assertEqual(args, ("run", None, None))

        args_auto = parse_arguments(["run", "--data-drive"])
        self.assertEqual(args_auto.data_drive, "auto")

        with self.assertRaisesRegex(ConfigurationError, "only once"):
            parse_arguments(["run", "--data-drive", "a.img", "--data-drive", "b.img"])

    def test_make_data_image_creates_sparse_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            out_file = Path(temporary) / "data.img"
            result = make_data_image(out_file, size_mb=64)
            self.assertEqual(result, out_file)
            self.assertTrue(out_file.is_file())
            self.assertEqual(out_file.stat().st_size, 64 * 1024 * 1024)

    def test_reproducible_manifest_contains_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "kernel.elf"
            boot = root / "boot.efi"
            kernel.write_bytes(b"kernel-data")
            boot.write_bytes(b"boot-data")
            config = load_configuration(ROOT)
            target, profile = config.select(None, None)
            manifest = generate_reproducible_manifest(
                ROOT, target, profile, "debug", kernel, boot
            )
            self.assertIn("target = x86_64-qemu", manifest)
            self.assertIn("source.revision =", manifest)
            self.assertIn("cargo_lock.sha256 =", manifest)
            self.assertIn("toolchain.rustc =", manifest)
            self.assertIn("kernel.sha256 =", manifest)
            self.assertIn("boot_manager.sha256 =", manifest)


if __name__ == "__main__":
    unittest.main()
