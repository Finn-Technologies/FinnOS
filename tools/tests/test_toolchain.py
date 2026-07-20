import unittest
from unittest.mock import patch

from tools.finnlib.toolchain import find_aavmf, find_command, find_firmware, find_tool

class ToolchainTests(unittest.TestCase):
    @patch("shutil.which", return_value="/bin/tool")
    def test_command_present(self, _which): self.assertEqual(find_command("cargo"), "/bin/tool")

    @patch("shutil.which", return_value=None)
    def test_command_absent(self, _which): self.assertIsNone(find_command("cargo"))

    def test_qemu_override(self):
        with self.subTest("override"):
            self.assertIsNone(find_tool("qemu-img", {"FINNOS_QEMU_IMG": "/missing"}))

    def test_aarch64_qemu_override(self):
        self.assertIsNone(
            find_tool("qemu-system-aarch64", {"FINNOS_QEMU_AARCH64": "/missing"})
        )

    def test_aavmf_override_and_architecture_selection(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as directory:
            firmware = Path(directory) / "AAVMF_CODE.fd"
            firmware.touch()
            environment = {"FINNOS_AAVMF_CODE": str(firmware)}
            self.assertEqual(find_aavmf(environment, ()), firmware)
            self.assertEqual(find_firmware("arm64", environment), firmware)
            self.assertIsNone(find_firmware("unsupported", environment))

if __name__ == "__main__": unittest.main()
