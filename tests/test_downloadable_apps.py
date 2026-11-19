"""Real, downloadable macOS / OpenHarmony package checks.

The host suite never needs a network or a VM. These checks are therefore off
unless the caller opts in:

    FINN_TEST_PACKAGE_DIR=/path/to/downloads python3 -m unittest \
        tests.test_downloadable_apps -v
    FINN_TEST_NETWORK=1 python3 -m unittest tests.test_downloadable_apps -v

The first form uses packages the caller has already downloaded (nothing is
fetched). The second downloads the small public sample packages listed below
and stores them in a temporary directory. Neither form commits a binary.

The ZIP-header check needs only a C++20 compiler and runs by default: it is the
shared reader behind .zip macOS app archives and OpenHarmony .hap packages.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile

try:
    from support import REPO
except ImportError:  # invoked as `python3 -m unittest tests.test_downloadable_apps`
    from tests.support import REPO

PACKAGE_DIR = os.environ.get("FINN_TEST_PACKAGE_DIR", "")
NETWORK = os.environ.get("FINN_TEST_NETWORK", "") == "1"

# Small public packages, all well under 50 MB, no account or license required.
SAMPLES = {
    "Itsycal-0.29.0.zip": "https://itsycal.s3.amazonaws.com/Itsycal-0.29.0.zip",
    "Hidden-Bar.zip": (
        "https://github.com/consolelabs/hidden/releases/download/"
        "v1.13.2/Hidden-Bar-v1.13.2-macos.zip"
    ),
    "Nightfall.dmg": (
        "https://github.com/r-thomson/Nightfall/releases/download/"
        "v3.1.0/Nightfall.dmg"
    ),
    "AquaSKK-4.7.3.pkg": (
        "https://github.com/codefirst/aquaskk/releases/download/"
        "4.7.3/AquaSKK-4.7.3.pkg"
    ),
    "Calc_Demo.hap": (
        "https://media.githubusercontent.com/media/openharmony/"
        "applications_hap/master/Calc_Demo.hap"
    ),
    "Clock_Demo.hap": (
        "https://media.githubusercontent.com/media/openharmony/"
        "applications_hap/master/Clock_Demo.hap"
    ),
}


HARNESS = r"""
#include "compat/zip_entries.hpp"
#include <iostream>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<std::string> names;
        const bool read = finn::compat::read_zip_entry_names(argv[i], &names);
        const bool app = finn::compat::zip_contains_macos_app(argv[i]);
        std::cout << argv[i] << " read=" << (read ? 1 : 0)
                  << " app=" << (app ? 1 : 0)
                  << " names=" << names.size() << "\n";
    }
    return 0;
}
"""


class ZipEntriesHeaderTest(unittest.TestCase):
    """The shared ZIP reader must not label an arbitrary ZIP as a macOS app."""

    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("c++") or shutil.which("g++")
        if not cls.compiler:
            raise unittest.SkipTest("no C++ compiler for the ZIP header check")

    def test_zip_app_detection(self):
        with tempfile.TemporaryDirectory() as tmp:
            app_zip = os.path.join(tmp, "Sample.zip")
            plain_zip = os.path.join(tmp, "Plain.zip")
            broken_zip = os.path.join(tmp, "Broken.zip")
            with zipfile.ZipFile(app_zip, "w") as z:
                z.writestr("Sample.app/Contents/Info.plist", "<plist/>")
                z.writestr("Sample.app/Contents/MacOS/Sample", "binary")
            with zipfile.ZipFile(plain_zip, "w") as z:
                z.writestr("notes.txt", "not an app")
            with open(broken_zip, "wb") as f:
                f.write(b"PK\x03\x04" + b"\x00" * 40)

            source = os.path.join(tmp, "harness.cpp")
            binary = os.path.join(tmp, "harness")
            with open(source, "w", encoding="utf-8") as f:
                f.write(HARNESS)
            build = subprocess.run(
                [self.compiler, "-std=c++20", "-I" + os.path.join(REPO, "src"),
                 "-o", binary, source],
                capture_output=True, text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)

            result = subprocess.run(
                [binary, app_zip, plain_zip, broken_zip],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = result.stdout.strip().splitlines()
            self.assertEqual(len(lines), 3)
            self.assertIn("read=1 app=1 names=2", lines[0])
            self.assertIn("read=1 app=0 names=1", lines[1])
            self.assertIn("read=0 app=0 names=0", lines[2])


@unittest.skipUnless(PACKAGE_DIR or NETWORK,
                     "set FINN_TEST_PACKAGE_DIR or FINN_TEST_NETWORK=1")
class DownloadableAppsIntegrationTest(unittest.TestCase):
    """Run the real installer path on real packages; never invent a result."""

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="finn-downloadable-")
        cls.packages = {}
        if PACKAGE_DIR:
            for name in sorted(os.listdir(PACKAGE_DIR)):
                if name.lower().endswith((".zip", ".dmg", ".pkg", ".hap", ".hsp")):
                    cls.packages[name] = os.path.join(PACKAGE_DIR, name)
        if NETWORK:
            import urllib.request
            for name, url in SAMPLES.items():
                dest = os.path.join(cls.work, name)
                with urllib.request.urlopen(url, timeout=60) as response:
                    data = response.read(51 * 1024 * 1024)
                if len(data) > 50 * 1024 * 1024:
                    raise AssertionError("sample too large: " + name)
                with open(dest, "wb") as f:
                    f.write(data)
                cls.packages.setdefault(name, dest)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def _run(self, binary, *args):
        env = dict(os.environ)
        env["HOME"] = self.work
        return subprocess.run([binary, *args], env=env,
                              capture_output=True, text=True, timeout=180)

    def test_install_real_packages(self):
        self.assertTrue(self.packages, "no packages found or downloaded")
        for name, path in sorted(self.packages.items()):
            with self.subTest(package=name):
                lower = name.lower()
                if lower.endswith((".hap", ".hsp")):
                    binary = shutil.which("finn-ohos")
                    name_of_binary = "finn-ohos"
                else:
                    binary = shutil.which("finn-darwin")
                    name_of_binary = "finn-darwin"
                if not binary:
                    self.skipTest(name_of_binary + " is not installed")
                pre = self._run(binary, "diagnose", path)
                result = self._run(binary, "install", path)
                self.assertEqual(result.returncode, 0,
                                 "diagnose rc=%d: %s\n%s\ninstall rc=%d: %s\n%s" % (
                                     pre.returncode, pre.stdout, pre.stderr,
                                     result.returncode, result.stdout, result.stderr))
                self.assertIn("Ready:", result.stdout, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
