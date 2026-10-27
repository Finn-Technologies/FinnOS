"""Every icon the shell loads at runtime ships in the repo's assets.

The compositor draws tiles and glyphs from /usr/share/finn/icons, and both
the dev-VM deploy (`make install`) and the ISO installer (`base/setup.sh`)
fill that directory from assets/icons. An icon the C code loads but the
repo does not carry is a tile drawn as an empty square, so the load sites
and the asset directories must agree.
"""

import os
import re
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPOSITOR = os.path.join(ROOT, "src", "compositor")
SHELL_ICONS = os.path.join(ROOT, "assets", "icons", "shell")
APPS_ICONS = os.path.join(ROOT, "assets", "icons", "apps")

LOAD_PATTERNS = (
    re.compile(r'"%s/([^"]+\.svg)"'),
    re.compile(r'ICON_DIR\s*"/([^"]+\.svg)"'),
    re.compile(r'load_nav_icon\([^,]+,\s*"([^"]+)"\)'),
)


def loaded_icons():
    names = set()
    for leaf in sorted(os.listdir(COMPOSITOR)):
        if not leaf.endswith((".c", ".h")):
            continue
        with open(os.path.join(COMPOSITOR, leaf), "r", encoding="utf-8") as handle:
            text = handle.read()
        for pattern in LOAD_PATTERNS:
            names.update(pattern.findall(text))
    return names


class ShellIconsTest(unittest.TestCase):

    def test_every_loaded_icon_ships_in_assets(self):
        shipped = set(os.listdir(SHELL_ICONS)) | set(os.listdir(APPS_ICONS))
        missing = sorted(name for name in loaded_icons() if name not in shipped)
        self.assertEqual(missing, [])

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

    def test_chroot_fallback_icons_name_files_that_exist(self):
        """The host-server flow has no tree to glob, so it lists the names.
        A name that moved is an icon the installed shell cannot draw."""
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
