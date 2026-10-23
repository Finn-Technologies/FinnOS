"""The vector rasteriser: verifies that the SVG engine builds and passes all tests."""

import os
import subprocess
import unittest


class VgTest(unittest.TestCase):

    def test_finnvg_passes_host_test_suite(self):
        tests_c_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "c")
        res = subprocess.run(
            ["make", "test_finnvg"],
            cwd=tests_c_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(res.returncode, 0, f"Compilation failed: {res.stderr}")

        bin_path = os.path.join(tests_c_dir, "test_finnvg")
        run_res = subprocess.run(
            [bin_path],
            cwd=tests_c_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(run_res.returncode, 0, f"test_finnvg failed:\n{run_res.stdout}\n{run_res.stderr}")
        self.assertIn("test_finnvg: ok", run_res.stdout)
