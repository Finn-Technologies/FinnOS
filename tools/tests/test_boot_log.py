import unittest

from tools.finnlib.qemu import MARKERS, validate_smoke

class BootLogTests(unittest.TestCase):
    def test_markers_in_order(self):
        self.assertEqual(validate_smoke(33, "\n".join(MARKERS)), [])

    def test_missing_and_failure_markers(self):
        errors = validate_smoke(35, MARKERS[0] + "\nFINNOS:KERNEL:PANIC")
        self.assertTrue(any("missing marker" in error for error in errors))
        self.assertIn("kernel panic marker found", errors)

    def test_status_zero_is_not_success(self):
        self.assertTrue(validate_smoke(0, "\n".join(MARKERS)))

if __name__ == "__main__": unittest.main()
