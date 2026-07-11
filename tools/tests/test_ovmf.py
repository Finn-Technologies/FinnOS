import tempfile
import unittest
from pathlib import Path

from tools.finnlib.toolchain import find_ovmf

class OvmfTests(unittest.TestCase):
    def test_override_precedes_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "OVMF.fd"
            path.touch()
            self.assertEqual(find_ovmf({"FINNOS_OVMF_CODE": str(path)}, ()), path)

    def test_missing_override_is_not_silently_replaced(self):
        self.assertIsNone(find_ovmf({"FINNOS_OVMF_CODE": "/missing"}, ()))

    def test_candidate_is_found(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "OVMF.fd"
            path.touch()
            self.assertEqual(find_ovmf({}, (str(path),)), path)

if __name__ == "__main__": unittest.main()
