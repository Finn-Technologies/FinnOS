import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def credentials_function():
    source = (ROOT / "vm/finn-chroot.sh").read_text()
    start = source.index("credentials() {")
    end = source.index("\n}\n\nfailed()", start) + 2
    return source[start:end]


class ChrootPasswordTest(unittest.TestCase):
    def run_credentials(self, supplied):
        marker = "TEST-ONLY-PASSWORD-MARKER"
        env = os.environ.copy()
        env.update(PW=marker, CREDS_SHOWN="0", FINN_PASSWORD=marker if supplied else "")
        script = credentials_function() + "\ncredentials\ncredentials\n"
        result = subprocess.run(
            ["bash", "-c", script],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return marker, result.stdout

    def test_operator_supplied_password_is_not_echoed(self):
        marker, output = self.run_credentials(supplied=True)
        self.assertNotIn(marker, output)
        self.assertEqual(output.count("operator-supplied credentials are omitted"), 1)

    def test_generated_password_is_reported_once(self):
        marker, output = self.run_credentials(supplied=False)
        self.assertEqual(output.count(marker), 2)


if __name__ == "__main__":
    unittest.main()
