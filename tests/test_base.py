"""Base-image compatibility packaging stays wired to the package manager."""

import os
import re
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class BaseImageTest(unittest.TestCase):

    def test_x86_64_release_manifest_includes_flatpak(self):
        manifest = os.path.join(ROOT, "base", "packages.x86_64")
        with open(manifest, "r", encoding="utf-8") as handle:
            packages = {
                line.strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        self.assertIn("flatpak", packages)

    def test_iso_bootstrap_packages_stay_out_of_installed_system_manifests(self):
        for architecture in ("x86_64", "aarch64"):
            manifest = os.path.join(ROOT, "base", "packages." + architecture)
            with open(manifest, "r", encoding="utf-8") as handle:
                packages = {
                    line.strip()
                    for line in handle
                    if line.strip() and not line.lstrip().startswith("#")
                }
            with self.subTest(architecture=architecture):
                self.assertNotIn("archiso", packages)
                self.assertNotIn("mkinitcpio-archiso", packages)
                self.assertNotIn("syslinux", packages)

    def test_live_image_declares_finn_group_before_tmpfiles(self):
        sysusers = os.path.join(ROOT, "base", "airootfs", "usr", "lib", "sysusers.d", "finn.conf")
        tmpfiles = os.path.join(ROOT, "base", "airootfs", "usr", "lib", "tmpfiles.d", "finn.conf")
        compositor = os.path.join(ROOT, "base", "airootfs", "etc", "systemd", "system", "finn-comp.service")
        with open(sysusers, "r", encoding="utf-8") as handle:
            self.assertRegex(handle.read(), r"(?m)^g\s+finn\s+-\s*(?:#.*)?$")
        with open(tmpfiles, "r", encoding="utf-8") as handle:
            self.assertIn("root finn", handle.read())
        with open(compositor, "r", encoding="utf-8") as handle:
            self.assertRegex(handle.read(), r"(?m)^Group=finn$")

    def test_x86_64_manifest_carries_both_cpu_microcode_vendors(self):
        manifest = os.path.join(ROOT, "base", "packages.x86_64")
        with open(manifest, "r", encoding="utf-8") as handle:
            packages = {
                line.strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        self.assertTrue({"intel-ucode", "amd-ucode"}.issubset(packages))

        fallback = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(fallback, "r", encoding="utf-8") as handle:
            self.assertIn("intel-ucode amd-ucode", handle.read())

    def test_x86_64_release_manifest_includes_gui_runtime_and_wine(self):
        manifest = os.path.join(ROOT, "base", "packages.x86_64")
        with open(manifest, "r", encoding="utf-8") as handle:
            packages = {
                line.strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        self.assertTrue({"flatpak", "wine", "wayland", "wayland-protocols",
                         "libxkbcommon"}.issubset(packages))

    def test_arm_manifest_has_flatpak_and_wayland_runtime_but_no_unavailable_wine(self):
        manifest = os.path.join(ROOT, "base", "packages.aarch64")
        with open(manifest, "r", encoding="utf-8") as handle:
            packages = {
                line.strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        self.assertTrue({"flatpak", "wayland", "wayland-protocols", "libxkbcommon"}.issubset(packages))
        self.assertNotIn("wine", packages)

    def test_networkmanager_has_a_wifi_supplicant_backend_on_both_arches(self):
        for architecture in ("x86_64", "aarch64"):
            manifest = os.path.join(ROOT, "base", "packages." + architecture)
            with open(manifest, "r", encoding="utf-8") as handle:
                packages = {
                    line.strip()
                    for line in handle
                    if line.strip() and not line.lstrip().startswith("#")
                }
            with self.subTest(architecture=architecture):
                self.assertTrue({"networkmanager", "wpa_supplicant"}.issubset(packages))

        fallback = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(fallback, "r", encoding="utf-8") as handle:
            self.assertIn("networkmanager wpa_supplicant", handle.read())

    def test_disk_installer_enables_only_networkmanager(self):
        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        with open(chroot, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertIn("systemctl enable NetworkManager", script)
        self.assertNotIn("systemd-networkd", script)

    def test_multilib_is_applied_only_to_x86_64(self):
        setup = os.path.join(ROOT, "base", "setup.sh")
        mkfinniso = os.path.join(ROOT, "base", "mkfinniso")
        snippet = os.path.join(ROOT, "base", "pacman.multilib.conf")
        with open(setup, "r", encoding="utf-8") as handle:
            setup_text = handle.read()
        with open(mkfinniso, "r", encoding="utf-8") as handle:
            mkfinniso_text = handle.read()
        with open(snippet, "r", encoding="utf-8") as handle:
            snippet_text = handle.read()
        self.assertIn('[ "$ARCH" = "x86_64" ]', setup_text)
        self.assertIn("pacman.multilib.conf", setup_text)
        self.assertIn('[ "$ARCH" = \'x86_64\' ]', mkfinniso_text)
        self.assertIn("pacman.multilib.conf", mkfinniso_text)
        self.assertIn("[multilib]", snippet_text)

        installer = os.path.join(ROOT, "vm", "finn-install.sh")
        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        with open(installer, "r", encoding="utf-8") as handle:
            installer_text = handle.read()
        with open(chroot, "r", encoding="utf-8") as handle:
            chroot_text = handle.read()
        self.assertIn("grep -Fqx '[multilib]' /etc/pacman.conf", installer_text)
        self.assertIn('cat "$SRC/base/pacman.multilib.conf" >> /etc/pacman.conf', installer_text)
        self.assertIn('get "$MULTILIB_CONF" base/pacman.multilib.conf', chroot_text)

    def test_setup_bootstraps_flathub_through_finnpkg_when_installed(self):
        setup = os.path.join(ROOT, "base", "setup.sh")
        with open(setup, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertIn("if [ -x /usr/bin/flatpak ]; then", script)
        self.assertIn("/usr/local/bin/finnpkg flatpak init", script)
        self.assertIn("WARN: Flathub could not be configured", script)

    def test_disk_installer_bootstraps_flathub_in_the_installed_system(self):
        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        with open(chroot, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertIn("if [ -x /usr/bin/flatpak ]; then", script)
        self.assertIn("/usr/local/bin/finnpkg flatpak init", script)
        self.assertIn("WARN: Flathub could not be configured", script)

    def test_every_writer_of_the_installed_list_names_catalogue_apps(self):
        """The shell draws a tile as installed only when the package
        manager's list names an id the catalogue knows. Three install paths
        write that list, and a path that writes a binary name or a missing
        app is a list the shell cannot match to anything."""
        registry = os.path.join(ROOT, "assets", "apps.registry")
        with open(registry, "r", encoding="utf-8") as handle:
            catalogue = {
                line.split("|", 1)[0].strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        expected = {"settings", "files", "store"}
        self.assertTrue(expected.issubset(catalogue))

        writers = (
            os.path.join(ROOT, "base", "setup.sh"),
            os.path.join(ROOT, "vm", "finn-chroot.sh"),
            os.path.join(ROOT, "base", "pkgbuilds", "finn-filesystem", "PKGBUILD"),
        )
        for path in writers:
            with self.subTest(path=path):
                with open(path, "r", encoding="utf-8") as handle:
                    script = handle.read()
                match = re.search(
                    r"printf '([^']*)' > [^\n]*/var/lib/finnpkg/installed",
                    script,
                )
                self.assertIsNotNone(match, path)
                installed = set(match.group(1).split("\\n"))
                installed.discard("")
                self.assertEqual(installed, expected, path)
                self.assertTrue(installed.issubset(catalogue), path)

    def test_installer_does_not_need_dev_fd(self):
        """The live root may not carry /dev/fd, and a process substitution
        that cannot open one is a loop that silently does nothing."""
        installer = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(installer, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertNotIn("< <(", script)
        self.assertNotIn("> >(", script)

    def test_live_medium_populates_the_keyring_before_the_installer(self):
        """The live root is read-only and has no keyring of its own. The
        installer's first step is a signed pacman sync, so the medium has to
        put a writable keyring in place and populate it before the tty1
        autologin can start finn-install."""
        units = os.path.join(ROOT, "base", "airootfs", "etc", "systemd", "system")
        with open(os.path.join(units, "etc-pacman.d-gnupg.mount"), "r",
                  encoding="utf-8") as handle:
            mount = handle.read()
        self.assertIn("Where=/etc/pacman.d/gnupg", mount)
        self.assertIn("Type=tmpfs", mount)

        with open(os.path.join(units, "pacman-init.service"), "r",
                  encoding="utf-8") as handle:
            init = handle.read()
        self.assertIn("Requires=etc-pacman.d-gnupg.mount", init)
        self.assertIn("Before=getty@tty1.service", init)
        self.assertIn("ExecStart=/usr/bin/pacman-key --init", init)
        self.assertIn("ExecStart=/usr/bin/pacman-key --populate", init)

        link = os.path.join(
            units, "multi-user.target.wants", "pacman-init.service")
        self.assertTrue(os.path.islink(link), link)

        installer = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(installer, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertNotIn("SigLevel = Never", script)
        self.assertIn("pacman-key --init", script)
        self.assertIn("pacman-key --populate", script)

    def test_installer_asks_findmnt_for_one_target_at_a_time(self):
        """findmnt accepts one target. A second is a usage error that
        returns 1 without a message, and set -e stops the install there."""
        installer = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(installer, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertNotIn(
            "findmnt -o TARGET,SOURCE,FSTYPE /mnt /mnt/boot", script
        )
        self.assertIn("findmnt -o TARGET,SOURCE,FSTYPE /mnt\n", script)
        self.assertIn("findmnt -o TARGET,SOURCE,FSTYPE /mnt/boot", script)

    def test_the_catalogue_has_no_first_party_app_without_a_view(self):
        """A native entry is an app the shell draws. An entry whose view and
        launcher do not exist can only be shown as unavailable, so it does
        not belong in the catalogue; Flatpak and winget entries are remote
        and may have no command of their own."""
        registry = os.path.join(ROOT, "assets", "apps.registry")
        native = set()
        with open(registry, "r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                fields = [field.strip() for field in line.split("|")]
                self.assertGreaterEqual(len(fields), 5, line)
                app_id, _, icon, command = fields[:4]
                self.assertTrue(
                    os.path.isfile(os.path.join(ROOT, "assets", "icons", "apps", icon)),
                    line,
                )
                source = fields[5] if len(fields) > 5 else ""
                if source:
                    continue
                self.assertTrue(command, line)
                native.add(app_id)
        self.assertEqual(native, {"settings", "files", "store"})


if __name__ == "__main__":
    unittest.main()
