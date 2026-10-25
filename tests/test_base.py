"""Base-image compatibility packaging stays wired to the package manager."""

import os
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


if __name__ == "__main__":
    unittest.main()
