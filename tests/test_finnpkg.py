"""finnpkg: what the package manager accepts, and what it refuses.

The script runs against a temporary catalogue, install list and apps
directory, so these tests install into a scratch tree and touch nothing on
the host. macOS is a supported test host: it has no `sha256sum`, so the
script picks `shasum -a 256` there.
"""

import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

# The tests share a helper module, and the SDK is not installed when
# they run: both live beside this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402,F401  (puts the SDK on the path)

PKG = os.path.join(support.REPO, "src", "pkg", "finnpkg")

CATALOGUE = """\
# test catalogue
# Format: id | Name | icon | command | note
notes  | Finn Notes | notes.svg | /bin/sh                  | Notes
code   | Finn Code  | code.svg  | /nonexistent/finn-code   | Code
"""


def manifest(app_id="notes"):
    return ('id = "%s"\n'
            'name = "Finn Notes"\n'
            'version = "0.1.0"\n'
            'command = "/usr/bin/finn-notes"\n' % app_id).encode()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class PackageTest(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="finnpkg-")
        self.addCleanup(shutil.rmtree, self.dir, True)

        self.catalogue = os.path.join(self.dir, "apps.registry")
        self.installed = os.path.join(self.dir, "installed")
        self.apps = os.path.join(self.dir, "apps")

        with open(self.catalogue, "w", encoding="utf-8") as handle:
            handle.write(CATALOGUE)
        open(self.installed, "w").close()
        os.makedirs(self.apps)

    # ------------------------------------------------------------ helpers

    def run_pkg(self, *args):
        env = dict(os.environ)
        env.update({
            "FINN_APPS_CATALOGUE": self.catalogue,
            "FINN_APPS_INSTALLED": self.installed,
            "FINN_APPS_DIR": self.apps,
        })
        return subprocess.run(
            [PKG] + list(args),
            cwd=self.dir, env=env, capture_output=True, universal_newlines=True,
        )

    def installed_ids(self):
        with open(self.installed, "r", encoding="utf-8") as handle:
            return [line.strip() for line in handle if line.strip()]

    def installed_path(self, *parts):
        return os.path.join(self.apps, *parts)

    def sums_for(self, files):
        lines = ["%s  %s" % (sha256(data), name) for name, data in sorted(files.items())]
        return "\n".join(lines) + "\n"

    def package(self, filename, payload, sums=True, sums_text=None):
        """Write a .finn tarball, hashed unless a test wants otherwise."""
        path = os.path.join(self.dir, filename)
        entries = dict(payload)
        if sums_text is not None:
            entries["SHA256SUMS"] = sums_text.encode()
        elif sums:
            entries["SHA256SUMS"] = self.sums_for(payload).encode()

        with tarfile.open(path, "w:gz") as tar:
            for name, data in entries.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                tar.addfile(info, io.BytesIO(data))
        return path

    def assert_nothing_installed(self):
        self.assertEqual(self.installed_ids(), [])
        self.assertEqual(os.listdir(self.apps), [])

    # -------------------------------------------------------------- ids

    def test_a_traversal_id_is_refused_by_every_command(self):
        for verb in ("install", "info", "remove"):
            result = self.run_pkg(verb, "../../etc")
            self.assertNotEqual(result.returncode, 0, verb)
            self.assertIn("not an app id", result.stderr, verb)
        self.assert_nothing_installed()

    def test_an_id_that_is_not_a_plain_word_is_refused(self):
        for bad in ("Notes", "not es", "..", "notes/../code", "-notes", "a" * 33):
            result = self.run_pkg("info", bad)
            self.assertNotEqual(result.returncode, 0, bad)
            self.assertIn("not an app id", result.stderr, bad)

    def test_an_empty_id_is_refused(self):
        result = self.run_pkg("install")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("install needs", result.stderr)

    # ---------------------------------------------------------- archives

    def test_a_package_that_climbs_out_is_refused(self):
        path = self.package("evil.finn", {
            "manifest.toml": manifest(),
            "../escaped": b"x",
        })
        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("climbs out", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.dir, "escaped")))
        self.assertFalse(os.path.exists(os.path.join(os.path.dirname(self.dir), "escaped")))
        self.assert_nothing_installed()

    def test_a_package_with_an_absolute_path_is_refused(self):
        path = self.package("absolute.finn", {
            "manifest.toml": manifest(),
            "/tmp/finnpkg-absolute": b"x",
        })
        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("absolute path", result.stderr)
        self.assertFalse(os.path.exists("/tmp/finnpkg-absolute"))
        self.assert_nothing_installed()

    def test_a_package_with_a_symlink_is_refused(self):
        path = os.path.join(self.dir, "link.finn")
        data = manifest()
        with tarfile.open(path, "w:gz") as tar:
            info = tarfile.TarInfo("manifest.toml")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
            link = tarfile.TarInfo("bin/notes")
            link.type = tarfile.SYMTYPE
            link.linkname = "/bin/sh"
            tar.addfile(link)

        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr)
        self.assert_nothing_installed()

    # ----------------------------------------------------------- hashes

    def test_a_package_without_hashes_is_refused(self):
        path = self.package("plain.finn", {
            "manifest.toml": manifest(),
            "bin/notes": b"#!/bin/sh\n",
        }, sums=False)
        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA256SUMS", result.stderr)
        # Honest about what is and is not checked.
        self.assertIn("signatures are not available", result.stderr)
        self.assert_nothing_installed()

    def test_a_package_whose_hashes_do_not_match_is_refused(self):
        payload = {"manifest.toml": manifest(), "bin/notes": b"#!/bin/sh\n"}
        lines = []
        for name in sorted(payload):
            digest = "0" * 64 if name == "bin/notes" else sha256(payload[name])
            lines.append("%s  %s" % (digest, name))
        path = self.package("tampered.finn", payload,
                            sums_text="\n".join(lines) + "\n")

        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("failed its hash check", result.stderr)
        self.assert_nothing_installed()

    def test_a_package_whose_hashes_skip_a_file_is_refused(self):
        payload = {"manifest.toml": manifest(), "bin/notes": b"#!/bin/sh\n"}
        path = self.package("partial.finn", payload,
                            sums_text="%s  manifest.toml\n" % sha256(payload["manifest.toml"]))

        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not cover", result.stderr)
        self.assertIn("bin/notes", result.stderr)
        self.assert_nothing_installed()

    # --------------------------------------------------------- installs

    def test_a_hashed_package_installs_and_is_listed(self):
        payload = {
            "manifest.toml": manifest(),
            "bin/notes": b"#!/bin/sh\necho notes\n",
            "share/notes/readme.txt": b"hello\n",
        }
        path = self.package("notes.finn", payload)

        result = self.run_pkg("install", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("installed notes", result.stdout)

        self.assertTrue(os.path.isfile(self.installed_path("notes", "bin", "notes")))
        self.assertTrue(os.path.isfile(self.installed_path("notes", "SHA256SUMS")))
        self.assertEqual(self.installed_ids(), ["notes"])

        listing = self.run_pkg("list")
        self.assertIn("notes", listing.stdout)
        self.assertIn("installed", listing.stdout)

    def test_a_package_with_dot_slash_names_installs(self):
        # `tar czf thing.finn .` names every member "./x"; the hashes do
        # the same, and both have to resolve to the same relative path.
        payload = {
            "./manifest.toml": manifest(),
            "./bin/notes": b"#!/bin/sh\n",
        }
        path = self.package("dotslash.finn", payload)

        result = self.run_pkg("install", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.isfile(self.installed_path("notes", "bin", "notes")))

    def test_an_app_outside_the_catalogue_is_refused(self):
        path = self.package("rogue.finn", {
            "manifest.toml": manifest("rogue"),
            "bin/rogue": b"#!/bin/sh\n",
        })
        result = self.run_pkg("install", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not in the OS catalogue", result.stderr)
        self.assert_nothing_installed()

    def test_a_catalogue_app_with_a_runnable_command_installs(self):
        result = self.run_pkg("install", "notes")      # command is /bin/sh
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("installed Finn Notes", result.stdout)
        self.assertEqual(self.installed_ids(), ["notes"])

    def test_a_catalogue_app_whose_command_is_missing_cannot_be_installed(self):
        result = self.run_pkg("install", "code")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot be installed", result.stderr)
        self.assert_nothing_installed()

    def test_an_unknown_catalogue_app_is_refused(self):
        result = self.run_pkg("install", "nonesuch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no app called", result.stderr)

    # ----------------------------------------------------------- remove

    def test_remove_refuses_an_unknown_id(self):
        result = self.run_pkg("remove", "nonesuch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not in the OS catalogue", result.stderr)

    def test_remove_refuses_an_app_that_is_not_installed(self):
        result = self.run_pkg("remove", "notes")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is not installed", result.stderr)

    def test_remove_takes_an_installed_app_away(self):
        path = self.package("notes.finn", {
            "manifest.toml": manifest(),
            "bin/notes": b"#!/bin/sh\n",
        })
        self.assertEqual(self.run_pkg("install", path).returncode, 0)
        self.assertTrue(os.path.isdir(self.installed_path("notes")))

        result = self.run_pkg("remove", "notes")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("removed notes", result.stdout)
        self.assertFalse(os.path.exists(self.installed_path("notes")))
        self.assertEqual(self.installed_ids(), [])

    # ----------------------------------------------------------- update

    def test_update_still_says_it_is_not_available(self):
        result = self.run_pkg("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("not available", result.stdout)
        self.assertIn("no AI", result.stdout)


if __name__ == "__main__":
    unittest.main()
