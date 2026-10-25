"""The runtime package is the thing the OTA frontend installs.

These checks are static on purpose: they run on the host, without makepkg,
without Arch, and without a network. They pin the parts of the release that
must agree with the control plane: the version, the asset name, and the files
the package has to carry for a running system to be replaceable.
"""

import os
import re
import stat
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class RuntimeReleaseTest(unittest.TestCase):

    def read(self, *parts):
        with open(os.path.join(ROOT, *parts), "r", encoding="utf-8") as handle:
            return handle.read()

    def test_version_is_a_pacman_version(self):
        version = self.read("VERSION").strip()
        self.assertRegex(version, r"^[0-9]+(\.[0-9]+)+([._+~-][0-9A-Za-z]+)*$")

    def test_builder_is_executable_and_names_the_release_asset(self):
        path = os.path.join(ROOT, "scripts", "build-runtime-pkg.sh")
        mode = os.stat(path).st_mode
        self.assertTrue(mode & stat.S_IXUSR)
        body = self.read("scripts", "build-runtime-pkg.sh")
        self.assertIn("finnos-runtime-$VERSION-$ARCH.pkg.tar.zst", body)
        self.assertIn("sha256sum", body)
        self.assertIn("makepkg", body)

    def test_package_carries_the_runtime_not_just_the_binaries(self):
        body = self.read("scripts", "build-runtime-pkg.sh")
        for path in (
            "usr/share/finn/version",
            "usr/lib/systemd/system/finn-comp.service",
            "usr/lib/systemd/system/finnd.service",
            "usr/lib/sysusers.d/finn.conf",
            "usr/lib/tmpfiles.d/finn.conf",
        ):
            with self.subTest(path=path):
                self.assertIn(path, body)

    def test_workflow_publishes_the_same_asset_the_frontend_looks_for(self):
        body = self.read(".github", "workflows", "runtime.yml")
        self.assertIn("finnos-runtime-${version}-${arch}.pkg.tar.zst", body)
        self.assertIn("softprops/action-gh-release", body)
        self.assertIn("sha256sum -c", body)

    def test_make_install_writes_the_version_the_frontend_reads(self):
        body = self.read("Makefile")
        self.assertIn("VERSION $(DESTDIR)/usr/share/finn/version", body)

    def test_the_hardware_page_names_the_reference_machine(self):
        body = self.read("docs", "hardware.md")
        self.assertIn("OptiPlex 3020", body)
        self.assertIn("i3-4130", body)
        self.assertIn("does not ship a runtime it cannot support", body)


if __name__ == "__main__":
    unittest.main()
