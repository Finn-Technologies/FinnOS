import unittest
from unittest.mock import patch

from tools.finnlib.toolchain import find_command, find_tool

class ToolchainTests(unittest.TestCase):
    @patch("shutil.which", return_value="/bin/tool")
    def test_command_present(self, _which): self.assertEqual(find_command("cargo"), "/bin/tool")

    @patch("shutil.which", return_value=None)
    def test_command_absent(self, _which): self.assertIsNone(find_command("cargo"))

    def test_qemu_override(self):
        with self.subTest("override"):
            self.assertIsNone(find_tool("qemu-img", {"FINNOS_QEMU_IMG": "/missing"}))

if __name__ == "__main__": unittest.main()
