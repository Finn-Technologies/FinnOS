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

    def test_x86_64_release_manifest_includes_guest_tools_without_gtk3(self):
        manifest = os.path.join(ROOT, "base", "packages.x86_64")
        with open(manifest, "r", encoding="utf-8") as handle:
            packages = {
                line.strip()
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            }
        self.assertTrue(
            {"open-vm-tools", "virtualbox-guest-utils-nox", "qemu-guest-agent"}
            .issubset(packages)
        )
        # virtualbox-guest-utils hard-depends on gtk3, which pacman.conf refuses.
        self.assertNotIn("virtualbox-guest-utils", packages)

        for script_parts in (("base", "setup.sh"), ("vm", "finn-chroot.sh")):
            with open(os.path.join(ROOT, *script_parts), "r", encoding="utf-8") as h:
                text = h.read()
            self.assertIn("vmtoolsd.service", text)
            self.assertIn("vboxservice.service", text)
            self.assertIn("/mnt/hgfs", text)

        helper = os.path.join(ROOT, "tools", "finn-guest-tools")
        self.assertTrue(os.path.isfile(helper))
        self.assertTrue(os.access(helper, os.X_OK))

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

    def test_setup_ships_no_package_manager(self):
        setup = os.path.join(ROOT, "base", "setup.sh")
        with open(setup, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertNotIn("flatpak init", script)
        self.assertIn("loginctl enable-linger finn", script)

    def test_disk_installer_does_not_init_flatpak(self):
        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        with open(chroot, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertNotIn("flatpak init", script)

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
        expected = {
            "settings",
            "files",
            "terminal",
            "notes",
            "store",
            "viewer",
            "monitor",
            "calculator",
            "archive",
            "capture",
            "flourish",
        }
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
                    r"printf '([^']*)' > [^\n]*/var/lib/finn/installed",
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

    def test_installed_branding_is_not_the_live_medium(self):
        """The live root's issue and motd tell the reader to run the
        installer. An installed machine must not say that at its login
        console, so the two paths install different files."""
        def read(*parts):
            with open(os.path.join(ROOT, *parts), "r", encoding="utf-8") as handle:
                return handle.read()

        self.assertIn("live medium", read("base", "airootfs", "etc", "issue"))
        for name in ("issue", "motd"):
            installed = read("base", "installed", "etc", name)
            self.assertIn("FinnOS", installed)
            self.assertNotIn("live medium", installed)
            self.assertNotIn("finn-install", installed)

        setup = read("base", "setup.sh")
        self.assertIn('"$HERE/installed/etc/issue" /etc/issue', setup)
        self.assertIn('"$HERE/installed/etc/motd" /etc/motd', setup)
        chroot = read("vm", "finn-chroot.sh")
        self.assertIn("get /etc/issue base/installed/etc/issue", chroot)
        self.assertIn("get /etc/motd base/installed/etc/motd", chroot)

    def test_a_serial_install_keeps_its_serial_console(self):
        """The live medium can see console=ttyS0; the chroot cannot. An
        install driven over serial must hand that answer to the installed
        machine, or the boot after the install is invisible."""
        installer = os.path.join(ROOT, "vm", "finn-install.sh")
        with open(installer, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertIn('export FINN_SERIAL="$SERIAL"', script)

        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        with open(chroot, "r", encoding="utf-8") as handle:
            script = handle.read()
        self.assertIn('if [ "${FINN_SERIAL:-0}" = 1 ]; then', script)
        self.assertIn("console=ttyS0,115200 console=tty0", script)
        self.assertIn("systemctl enable serial-getty@ttyS0.service", script)
        self.assertIn("loglevel=3$serial_args", script)
        self.assertIn("loglevel=3%s", script)

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
        self.assertEqual(
            native,
            {
                "settings",
                "files",
                "terminal",
                "notes",
                "store",
                "viewer",
                "monitor",
                "calculator",
                "archive",
                "capture",
                "flourish",
            },
        )


class BootChainTest(unittest.TestCase):
    """The boot shows the Finn mark, and nothing names another distribution."""

    def read(self, *parts):
        with open(os.path.join(ROOT, *parts), "r", encoding="utf-8") as handle:
            return handle.read()

    def test_every_boot_menu_is_a_silent_finn_boot(self):
        menus = [
            ("base", "boot", "grub", "grub.cfg"),
            ("base", "boot", "syslinux", "syslinux.cfg"),
            ("base", "boot", "loader", "entries", "01-finnos-install.conf"),
            ("base", "boot", "loader", "entries", "02-finnos-live.conf"),
        ]
        for menu in menus:
            with self.subTest(menu=menu[-1]):
                text = self.read(*menu)
                self.assertIn("quiet", text)
                self.assertIn("splash", text)
                self.assertIn("vt.global_cursor_default=0", text)
                self.assertIn("plymouth.ignore-serial-consoles", text)
                self.assertNotIn("Arch Linux", text)

    def test_bootloader_waits_are_short(self):
        syslinux = self.read("base", "boot", "syslinux", "syslinux.cfg")
        self.assertIn("TIMEOUT 30", syslinux)
        loader = self.read("base", "boot", "loader", "loader.conf")
        self.assertIn("timeout 3", loader)
        grub = self.read("base", "boot", "grub", "grub.cfg")
        self.assertIn("set timeout=3", grub)
        # The install entry used to hold the console for ten seconds before
        # finn-install; that wait was most of the perceived boot time.
        self.assertNotIn("sleep 10", self.read("base", "airootfs", "root", ".bash_profile"))
        self.assertNotIn("sleep 10", self.read("base", "airootfs", "root", ".zprofile"))

    def test_plymouth_theme_is_in_both_initramfs_images(self):
        theme = ("base", "airootfs", "usr", "share", "plymouth", "themes", "finnos")
        for name in ("finnos.plymouth", "finnos.script", "mark.png",
                     "bar-track.png", "bar-fill.png", "bar-cap-l.png",
                     "bar-cap-r.png"):
            self.assertTrue(os.path.isfile(os.path.join(ROOT, *theme, name)), name)
        descriptor = self.read(*theme, "finnos.plymouth")
        self.assertIn("ModuleName=script", descriptor)
        script = self.read(*theme, "finnos.script")
        self.assertIn("Window.SetBackgroundTopColor", script)
        self.assertIn("Image(\"mark.png\")", script)
        # A normal boot is the mark plus a macOS-style bar. The bar moves only
        # on real Plymouth progress reports; the sweep is not a fake percentage.
        self.assertIn("bar-track.png", script)
        self.assertIn("bar-fill.png", script)
        self.assertIn("Plymouth.SetBootProgressFunction", script)
        self.assertIn("global.has_progress == 0", script)

        config = self.read("base", "airootfs", "etc", "plymouth", "plymouthd.conf")
        self.assertIn("Theme=finnos", config)
        self.assertIn("ShowDelay=0", config)

        for which in ("airootfs", "installed"):
            initrd = self.read("base", which, "etc", "mkinitcpio.conf")
            with self.subTest(initrd=which):
                self.assertIn("plymouth", initrd)
                self.assertIn("/etc/plymouth/plymouthd.conf", initrd)
                self.assertIn("/usr/share/plymouth/themes/finnos/mark.png", initrd)

    def test_both_manifests_carry_plymouth(self):
        for architecture in ("x86_64", "aarch64"):
            manifest = self.read("base", "packages." + architecture)
            self.assertIn("\nplymouth\n", manifest)

    def test_installed_bootloader_is_a_silent_finn_boot(self):
        chroot = self.read("vm", "finn-chroot.sh")
        self.assertIn('GRUB_DISTRIBUTOR="FinnOS"', chroot)
        self.assertIn("quiet splash vt.global_cursor_default=0", chroot)
        self.assertIn("base/installed/etc/mkinitcpio.conf", chroot)
        self.assertIn("base/airootfs/usr/share/plymouth/themes/finnos", chroot)
        setup = self.read("base", "setup.sh")
        self.assertIn("$HERE/airootfs/usr/share/plymouth/themes/finnos", setup)
        self.assertIn("$HERE/installed/etc/mkinitcpio.conf", setup)

    def test_lock_screen_has_real_triggers(self):
        paths = self.read("src", "common", "paths.hpp")
        self.assertIn("kLockRequestPath", paths)
        comp = self.read("src", "comp", "server.cpp")
        self.assertIn("XKB_KEY_l", comp)
        self.assertIn("request_lock()", comp)
        self.assertIn("read_auto_lock_minutes", comp)
        shell = self.read("src", "shell", "main.cpp")
        self.assertIn("kLockRequestPath", shell)
        self.assertIn("PrepareForSuspend", shell)
        tmpfiles = self.read("base", "finn-tmpfiles.conf")
        self.assertIn("d /run/finn/state 0775 root finn -", tmpfiles)

    def test_lock_screen_has_its_own_settings_backed_wallpaper(self):
        settings = self.read("apps", "settings", "main.cpp")
        self.assertIn("Lock Screen Wallpaper", settings)
        self.assertIn("setLockWallpaper", settings)
        theme = self.read("src", "ui", "finn", "theme.hpp")
        self.assertIn("setLockWallpaper", theme)
        self.assertIn("lockWallpaperPath", theme)
        data = self.read("src", "system", "sys_files.hpp")
        self.assertIn("lock_wallpaper", data)
        persisted = self.read("src", "system", "sys_files.cpp")
        self.assertIn("lock_wallpaper=", persisted)
        screen = self.read("src", "shell", "ui", "common", "LockScreenOverlay.qml")
        self.assertIn("lockWallpaperUrl", screen)
        wallpaper = self.read("src", "shell", "ui", "common", "WallpaperLayer.qml")
        self.assertIn("Image.PreserveAspectCrop", wallpaper)

    def test_ribbon_animation_is_first_setup_only(self):
        # The ribbon "hi." animation plays in finn-oobe's first step. The
        # shell must not draw it over a completed desktop on a normal boot;
        # that boot is the Plymouth mark and its progress bar.
        shell = self.read("src", "shell", "main.cpp")
        self.assertNotIn("BootSplashWindow", shell)
        self.assertNotIn("startBootAnimation", shell)
        self.assertNotIn("FINN_BOOT_SPLASH", shell)

    def test_no_distribution_name_in_visible_boot_or_app_copy(self):
        surfaces = [
            ("base", "airootfs", "etc", "os-release"),
            ("base", "airootfs", "etc", "plymouth", "plymouthd.conf"),
            ("base", "airootfs", "usr", "share", "plymouth", "themes",
             "finnos", "finnos.plymouth"),
            ("base", "airootfs", "usr", "share", "plymouth", "themes",
             "finnos", "finnos.script"),
            ("base", "installed", "etc", "issue"),
            ("base", "installed", "etc", "motd"),
            ("apps", "settings", "main.cpp"),
            ("apps", "store", "main.cpp"),
        ]
        for surface in surfaces:
            with self.subTest(surface=surface[-1]):
                self.assertNotIn("Arch Linux", self.read(*surface))


if __name__ == "__main__":
    unittest.main()
