"""The icon set the shell and apps draw from.

The runtime chooses icons from the registry, so the static check is that every
icon named in the catalogue ships, that the installers copy the asset
directories, and that the chroot fallback lists names that exist.
"""

import os
import re
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHELL_ICONS = os.path.join(ROOT, "assets", "icons", "shell")
APPS_ICONS = os.path.join(ROOT, "assets", "icons", "apps")


class IconAssetsTest(unittest.TestCase):

    def test_installers_fill_icons_from_assets(self):
        for path in (
            os.path.join(ROOT, "base", "setup.sh"),
            os.path.join(ROOT, "Makefile"),
            os.path.join(ROOT, "vm", "finn-chroot.sh"),
        ):
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
            self.assertIn("assets/icons", text, path)
        with open(os.path.join(ROOT, "base", "setup.sh"), "r", encoding="utf-8") as handle:
            setup = handle.read()
        self.assertNotIn("src/compositor/icons", setup)
        with open(os.path.join(ROOT, "vm", "finn-chroot.sh"), "r", encoding="utf-8") as handle:
            chroot = handle.read()
        self.assertNotIn("src/compositor/icons", chroot)

    def test_the_catalogue_icons_exist(self):
        with open(os.path.join(ROOT, "assets", "apps.registry"), "r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                fields = [field.strip() for field in line.split("|")]
                icon = fields[2]
                shipped = os.path.join(APPS_ICONS, icon)
                self.assertTrue(os.path.isfile(shipped), shipped)

    def test_chroot_fallback_icons_name_files_that_exist(self):
        with open(os.path.join(ROOT, "vm", "finn-chroot.sh"), "r", encoding="utf-8") as handle:
            script = handle.read()
        blocks = re.findall(
            r"for icon in (.*?); do\s*\n\s*"
            r"get \"[^\"]*/\$icon\.svg\" \"assets/icons/(shell|apps)/\$icon\.svg\"",
            script,
            re.S,
        )
        self.assertEqual({directory for _, directory in blocks}, {"shell", "apps"})
        for names, directory in blocks:
            for name in names.replace("\\", " ").split():
                path = os.path.join(ROOT, "assets", "icons", directory, name + ".svg")
                self.assertTrue(os.path.isfile(path), path)


if __name__ == "__main__":
    unittest.main()
