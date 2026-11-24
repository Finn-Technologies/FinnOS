"""The runtime package is the thing the OTA frontend installs.

These checks are static on purpose: they run on the host, without makepkg,
without Arch, and without a network. They pin the parts of the release that
must agree with the control plane: the version, the asset name, and the files
the package has to carry for a running system to be replaceable.
"""

import hashlib
import os
import re
import stat
import subprocess
import tempfile
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
            "usr/lib/systemd/system/finn-session.service",
            "usr/lib/systemd/system/finn-shell.service",
            "usr/lib/systemd/system/finn-wallpaper.service",
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
        self.assertIn("aarch64 development VM", body)
        self.assertIn("Not verified", body)


class FinnRepoToolingTest(unittest.TestCase):
    """Verify FinnOS package repo layout, key custody guards, and mirrorlists."""

    def read(self, *parts):
        with open(os.path.join(ROOT, *parts), "r", encoding="utf-8") as handle:
            return handle.read()

    def test_pacman_conf_puts_signed_finnos_repo_before_upstream(self):
        conf = self.read("base", "pacman.conf")
        finnos_m = re.search(r"(?m)^\[finnos\]\s*$", conf)
        core_m = re.search(r"(?m)^\[core\]\s*$", conf)
        extra_m = re.search(r"(?m)^\[extra\]\s*$", conf)
        self.assertIsNotNone(finnos_m)
        self.assertIsNotNone(core_m)
        self.assertIsNotNone(extra_m)
        self.assertLess(finnos_m.start(), core_m.start())
        self.assertLess(finnos_m.start(), extra_m.start())

        section = conf[finnos_m.start():core_m.start()]
        self.assertIn("SigLevel = Required", section)
        self.assertIn("Server = https://repo.finnos.org/$repo/os/$arch", section)

    def test_repo_directory_and_mirrorlist_template_exist(self):
        self.assertTrue(os.path.isfile(os.path.join(ROOT, "repo", "README.md")))
        mirrorlist = self.read("repo", "finnos-mirrorlist")
        self.assertIn("https://repo.finnos.org/$repo/os/$arch", mirrorlist)
        self.assertNotIn("geo.mirror.pkgbuild.com", mirrorlist)

        for script in ("gen-signing-key.sh", "publish.sh"):
            path = os.path.join(ROOT, "repo", script)
            self.assertTrue(os.path.isfile(path), script)
            self.assertTrue(os.stat(path).st_mode & stat.S_IXUSR, script)
            syntax = subprocess.run(
                ["bash", "-n", path],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(syntax.returncode, 0, syntax.stderr)

    def test_gen_signing_key_refuses_in_repo_directory_and_allows_outside_dry_run(self):
        script = os.path.join(ROOT, "repo", "gen-signing-key.sh")
        in_repo = subprocess.run(
            [script, "--key-dir", os.path.join(ROOT, "repo", "private"), "--dry-run"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertNotEqual(in_repo.returncode, 0)
        self.assertIn("inside the git repository", in_repo.stderr)

        with tempfile.TemporaryDirectory() as outside:
            ok = subprocess.run(
                [script, "--key-dir", outside, "--dry-run"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(ok.returncode, 0, ok.stderr)
            self.assertIn("verified outside repo", ok.stdout)

    def test_publish_script_dry_run_and_validation(self):
        script = os.path.join(ROOT, "repo", "publish.sh")
        with tempfile.TemporaryDirectory() as tmp:
            pkg = os.path.join(tmp, "finn-filesystem-0.1.0-1-x86_64.pkg.tar.zst")
            with open(pkg, "wb") as handle:
                handle.write(b"stub-package")
            repo_dir = os.path.join(tmp, "repo")
            res = subprocess.run(
                [
                    script,
                    "--arch",
                    "x86_64",
                    "--gpg-key",
                    "0123456789ABCDEF",
                    "--repo-dir",
                    repo_dir,
                    "--rsync",
                    "deploy@repo.finnos.org:/srv/repo/",
                    "--dry-run",
                    pkg,
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertIn(os.path.join(repo_dir, "finnos", "os", "x86_64"), res.stdout)
            self.assertIn("repo-add --verify --sign --key 0123456789ABCDEF", res.stdout)
            self.assertIn("rsync -avz --delete-after --delay-updates", res.stdout)

            missing_key = subprocess.run(
                [script, "--arch", "x86_64", "--dry-run", pkg],
                env={k: v for k, v in os.environ.items() if k != "FINN_GPG_KEY"},
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(missing_key.returncode, 0)

    def test_finnos_keyring_pkgbuild_and_checksums(self):
        pkgdir = os.path.join(ROOT, "base", "pkgbuilds", "finnos-keyring")
        pkgbuild = self.read("base", "pkgbuilds", "finnos-keyring", "PKGBUILD")
        self.assertIn("pkgname=finnos-keyring", pkgbuild)
        self.assertIn("install=finnos-keyring.install", pkgbuild)
        self.assertIn("etc/pacman.d/finnos-mirrorlist", pkgbuild)
        self.assertIn("if [ ! -s \"$srcdir/finnos.gpg\" ]", pkgbuild)

        for fname in ("finnos.gpg", "finnos-trusted", "finnos-revoked"):
            path = os.path.join(pkgdir, fname)
            self.assertTrue(os.path.isfile(path), fname)
            with open(path, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
            self.assertIn(digest, pkgbuild, fname)

    def test_installer_replaces_hardcoded_geo_mirror_with_finnos_mirrorlist(self):
        installer = self.read("vm", "finn-install.sh")
        self.assertNotIn("geo.mirror.pkgbuild.com", installer)
        self.assertIn("/etc/pacman.d/finnos-mirrorlist", installer)
        self.assertIn("/mnt/etc/pacman.d/finnos-mirrorlist", installer)
        self.assertIn("fallback", installer.lower())

        setup = self.read("base", "setup.sh")
        chroot = self.read("vm", "finn-chroot.sh")
        self.assertIn("/etc/pacman.d/finnos-mirrorlist", setup)
        self.assertIn("/etc/pacman.d/finnos-mirrorlist", chroot)

    def test_docs_repo_exists_and_links_ota(self):
        doc = self.read("docs", "repo.md")
        self.assertIn("(ota.md)", doc)
        self.assertIn("repo/gen-signing-key.sh", doc)
        self.assertIn("repo/publish.sh", doc)
        self.assertIn("finnos-keyring", doc)
        self.assertIn("SigLevel = Required", doc)

    def test_ota_units_and_makefile_install_are_wired(self):
        makefile = self.read("Makefile")
        for needle in (
            "src/ota/finn-update",
            "src/ota/finn_ota/*.py",
            "ota/grub-ab.cfg",
            "finn-update-check.service",
            "finn-update-check.timer",
            "finn-boot-health.service",
        ):
            self.assertIn(needle, makefile)

        for unit in (
            "finn-update-check.service",
            "finn-update-check.timer",
            "finn-boot-health.service",
        ):
            self.assertEqual(
                self.read("base", unit),
                self.read("base", "airootfs", "etc", "systemd", "system", unit),
                unit,
            )

    def test_installer_and_chroot_support_ab_slots_and_security_defaults(self):
        installer_sh = self.read("vm", "finn-install.sh")
        for flag in ("--hostname", "--timezone", "--ab-slots", "FINNOS_A", "FINNOS_B", "FINNOS_DATA"):
            self.assertIn(flag, installer_sh)

        chroot_sh = self.read("vm", "finn-chroot.sh")
        self.assertIn("passwd -l root", chroot_sh)
        self.assertNotIn("root:%s", chroot_sh)
        self.assertIn("FINN_AB_SLOTS", chroot_sh)
        self.assertIn("09_finnos_ab", chroot_sh)
        self.assertIn("finn-update-check.timer", chroot_sh)
        self.assertIn("finn-boot-health.service", chroot_sh)

        setup_sh = self.read("base", "setup.sh")
        self.assertIn("FINN_DEV_VM", setup_sh)
        self.assertIn("%wheel ALL=(ALL:ALL) ALL", setup_sh)
        self.assertIn("finn-update-check.timer", setup_sh)
        self.assertIn("offline/unprovisioned", setup_sh)

    def test_gui_installer_fixes_invocation_and_zoneinfo(self):
        cpp = self.read("apps", "installer", "main.cpp")
        self.assertIn("finn::kFinnInstallBin", cpp)
        self.assertIn("plan_install", cpp)
        self.assertNotIn("btrfs + FAT32 ESP", cpp)
        # Pillar A: user/hostname/timezone configuration belongs strictly to OOBE, never USB Recovery install.
        self.assertNotIn('"--hostname"', cpp)
        self.assertNotIn('"--timezone"', cpp)
        self.assertIn("readyReadStandardError", cpp)
        self.assertIn("QProcess::NormalExit", cpp)
        self.assertIn("PageInstallProgress", cpp)
        engine = self.read("src", "recovery", "recovery_engine.cpp")
        self.assertIn('"--disk"', engine)
        self.assertIn('"--ab-slots"', engine)
        self.assertIn('"--yes"', engine)
        self.assertIn('"dualboot-resize"', engine)
        self.assertIn('"reinstall"', engine)
        self.assertIn('"repair-boot"', engine)


if __name__ == "__main__":
    unittest.main()
