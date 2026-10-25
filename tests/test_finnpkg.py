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
        self.fake_bin = os.path.join(self.dir, "bin")
        self.tool_log = os.path.join(self.dir, "tool.log")
        self.wine_root = os.path.join(self.dir, "wine-prefixes")
        os.makedirs(self.fake_bin)

        self.flatpak = self.write_executable("flatpak", """#!/bin/sh
printf 'flatpak' >> "$FINN_FAKE_TOOL_LOG"
for arg in "$@"; do printf '\\t%s' "$arg" >> "$FINN_FAKE_TOOL_LOG"; done
printf '\\n' >> "$FINN_FAKE_TOOL_LOG"
if [ "${1:-}" = remotes ]; then
    if [ -n "${FINN_FAKE_FLATPAK_REMOTE:-}" ]; then
        printf 'flathub\\t%s\\n' "$FINN_FAKE_FLATPAK_REMOTE"
    fi
    if [ -n "${FINN_FAKE_OTHER_REMOTE:-}" ]; then
        printf '%s\\n' "$FINN_FAKE_OTHER_REMOTE"
    fi
fi
""")
        self.wine = self.write_executable("wine", """#!/bin/sh
printf 'wine' >> "$FINN_FAKE_TOOL_LOG"
for arg in "$@"; do printf '\\t%s' "$arg" >> "$FINN_FAKE_TOOL_LOG"; done
printf '\\n' >> "$FINN_FAKE_TOOL_LOG"
""")
        self.wineboot = self.write_executable("wineboot", """#!/bin/sh
printf 'wineboot\\t%s\\t%s\\n' "$WINEPREFIX" "$*" >> "$FINN_FAKE_TOOL_LOG"
""")
        self.finnctl = self.write_executable("finnctl", """#!/bin/sh
printf 'finnctl' >> "$FINN_FAKE_TOOL_LOG"
for arg in "$@"; do printf '\\t%s' "$arg" >> "$FINN_FAKE_TOOL_LOG"; done
printf '\\n' >> "$FINN_FAKE_TOOL_LOG"
""")
        self.write_executable("uname", """#!/bin/sh
if [ -n "${FINN_FAKE_ARCH:-}" ]; then
    printf '%s\\n' "$FINN_FAKE_ARCH"
else
    /usr/bin/uname "$@"
fi
""")

        with open(self.catalogue, "w", encoding="utf-8") as handle:
            handle.write(CATALOGUE)
        open(self.installed, "w").close()
        os.makedirs(self.apps)

    # ------------------------------------------------------------ helpers

    def write_executable(self, name, content):
        path = os.path.join(self.fake_bin, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
        os.chmod(path, 0o755)
        return path

    def run_pkg(self, *args, env_overrides=None):
        env = dict(os.environ)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        env.update({
            "FINN_APPS_CATALOGUE": self.catalogue,
            "FINN_APPS_INSTALLED": self.installed,
            "FINN_APPS_DIR": self.apps,
            "FINNPKG_FLATPAK_BIN": self.flatpak,
            "FINNPKG_WINE_BIN": self.wine,
            "FINNPKG_WINEBOOT_BIN": self.wineboot,
            "FINNPKG_FINNCTL_BIN": self.finnctl,
            "FINN_WINE_ROOT": self.wine_root,
            "FINN_FAKE_TOOL_LOG": self.tool_log,
            "FINN_FAKE_ARCH": "x86_64",
            "PATH": self.fake_bin + os.pathsep + os.environ.get("PATH", ""),
        })
        if env_overrides:
            env.update(env_overrides)
        return subprocess.run(
            [PKG] + list(args),
            cwd=self.dir, env=env, capture_output=True, universal_newlines=True,
        )

    def tool_log_text(self):
        if not os.path.isfile(self.tool_log):
            return ""
        with open(self.tool_log, "r", encoding="utf-8") as handle:
            return handle.read()

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

    # ----------------------------------------------------- compatibility apps

    def test_flatpak_init_registers_the_canonical_system_flathub_remote(self):
        result = self.run_pkg("flatpak", "init")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("configured system Flathub remote", result.stdout)
        self.assertIn(
            "flatpak\tremote-add\t--system\t--if-not-exists\t--from\tflathub"
            "\thttps://dl.flathub.org/repo/flathub.flatpakrepo\n",
            self.tool_log_text(),
        )

    def test_flatpak_init_accepts_only_the_canonical_existing_flathub(self):
        result = self.run_pkg(
            "flatpak", "init",
            env_overrides={"FINN_FAKE_FLATPAK_REMOTE": "https://example.invalid/repo/"},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("points somewhere unexpected", result.stderr)
        self.assertNotIn("flatpak\tremote-add\t", self.tool_log_text())

    def test_flatpak_search_uses_the_system_installation(self):
        result = self.run_pkg("flatpak", "search", "calculator")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "flatpak\tsearch\t--system\t--columns=application,name,description,remotes"
            "\tcalculator\n",
            self.tool_log_text(),
        )

    def test_flatpak_install_adds_only_the_official_remote_and_uses_an_app_id(self):
        result = self.run_pkg("flatpak", "install", "org.gnome.Calculator")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("installed Flatpak org.gnome.Calculator", result.stdout)
        log = self.tool_log_text()
        self.assertIn(
            "flatpak\tremote-add\t--system\t--if-not-exists\t--from\tflathub"
            "\thttps://dl.flathub.org/repo/flathub.flatpakrepo\n",
            log,
        )
        self.assertIn(
            "flatpak\tinstall\t--system\t--assumeyes\tflathub"
            "\torg.gnome.Calculator\n",
            log,
        )

    def test_flatpak_install_uses_the_search_result_remote_when_configured(self):
        result = self.run_pkg(
            "flatpak", "install", "org.gnome.Calculator", "gnome-nightly",
            env_overrides={"FINN_FAKE_OTHER_REMOTE": "gnome-nightly"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "flatpak\tinstall\t--system\t--assumeyes\tgnome-nightly"
            "\torg.gnome.Calculator\n",
            self.tool_log_text(),
        )
        self.assertNotIn("flatpak\tremote-add\t", self.tool_log_text())

    def test_flatpak_install_refuses_an_unconfigured_remote(self):
        result = self.run_pkg("flatpak", "install", "org.gnome.Calculator", "missing-remote")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not configured in the system installation", result.stderr)
        self.assertNotIn("flatpak\tinstall\t", self.tool_log_text())

    def test_flatpak_install_refuses_an_invalid_remote_name(self):
        result = self.run_pkg("flatpak", "install", "org.gnome.Calculator", "flathub;reboot")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a valid Flatpak remote name", result.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def test_flatpak_install_refuses_a_noncanonical_remote(self):
        result = self.run_pkg(
            "flatpak", "install", "org.gnome.Calculator",
            env_overrides={"FINN_FAKE_FLATPAK_REMOTE": "https://example.invalid/repo/"},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("points somewhere unexpected", result.stderr)
        self.assertNotIn("flatpak\tinstall\t", self.tool_log_text())

    def test_flatpak_app_ids_are_validated_before_backend_calls(self):
        result = self.run_pkg("flatpak", "install", "org.gnome.Calculator;touch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a Flatpak application id", result.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def test_flatpak_app_id_components_do_not_start_with_digits(self):
        result = self.run_pkg("flatpak", "install", "org.2Calculator")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a Flatpak application id", result.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def test_flatpak_run_routes_to_finnd_even_when_a_host_display_exists(self):
        result = self.run_pkg(
            "flatpak", "run", "org.gnome.Calculator",
            env_overrides={"WAYLAND_DISPLAY": "host-display"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.tool_log_text(),
                         "finnctl\tflatpak.run\torg.gnome.Calculator\n")

    def test_flatpak_run_refuses_invalid_ids_before_finnd(self):
        result = self.run_pkg("flatpak", "run", "org.gnome.Calculator;touch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a Flatpak application id", result.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def register_wine_app(self, app_id="portable", filename="Portable App.exe", env_overrides=None):
        executable = os.path.join(self.dir, filename)
        with open(executable, "wb") as handle:
            handle.write(b"existing Windows application")
        result = self.run_pkg("wine", "register", app_id, executable,
                              env_overrides=env_overrides)
        return result, executable

    def test_wine_registers_an_existing_exe_and_creates_a_private_prefix(self):
        result, executable = self.register_wine_app()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("registered portable", result.stdout)
        app_dir = os.path.join(self.wine_root, "portable")
        self.assertTrue(os.path.isdir(os.path.join(app_dir, "prefix")))
        with open(os.path.join(app_dir, "executable"), "r", encoding="utf-8") as handle:
            self.assertEqual(handle.read(), executable + "\n")
        self.assertEqual(self.tool_log_text(), "")

    def test_wine_list_reports_only_registered_apps(self):
        self.register_wine_app("portable")
        os.makedirs(os.path.join(self.wine_root, "not-registered"))
        result = self.run_pkg("wine", "list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["portable"])

    def test_wine_run_routes_registered_app_through_finnd_only(self):
        result, _ = self.register_wine_app("paint", "Portable; echo nope.exe")
        self.assertEqual(result.returncode, 0, result.stderr)
        launched = self.run_pkg("wine", "run", "paint")
        self.assertEqual(launched.returncode, 0, launched.stderr)
        self.assertEqual(self.tool_log_text(), "finnctl\twine.run\tpaint\n")

    def test_wine_invalid_ids_paths_and_unregistered_runs_are_refused(self):
        invalid_id = self.run_pkg("wine", "register", "../paint", "/tmp/paint.exe")
        self.assertNotEqual(invalid_id.returncode, 0)
        self.assertIn("Wine app id", invalid_id.stderr)
        self.assertFalse(os.path.exists(self.wine_root))

        relative = self.run_pkg("wine", "register", "paint", "paint.exe")
        self.assertNotEqual(relative.returncode, 0)
        self.assertIn("absolute .exe", relative.stderr)
        wrong_extension = self.run_pkg("wine", "register", "paint", "/tmp/paint.zip")
        self.assertNotEqual(wrong_extension.returncode, 0)
        missing = self.run_pkg("wine", "register", "paint", "/tmp/no-such-app.exe")
        self.assertNotEqual(missing.returncode, 0)
        unregistered = self.run_pkg("wine", "run", "paint")
        self.assertNotEqual(unregistered.returncode, 0)
        self.assertIn("not registered", unregistered.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def test_wine_rejects_arm_and_does_not_create_prefixes_or_call_wine(self):
        result, executable = self.register_wine_app(
            "portable", env_overrides={"FINN_FAKE_ARCH": "aarch64"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("only on x86_64", result.stderr)
        self.assertFalse(os.path.exists(self.wine_root))
        self.assertEqual(self.tool_log_text(), "")

        install = self.run_pkg(
            "wine", "install", "portable", executable,
            env_overrides={"FINN_FAKE_ARCH": "aarch64"},
        )
        self.assertNotEqual(install.returncode, 0)
        self.assertIn("only on x86_64", install.stderr)
        self.assertFalse(os.path.exists(self.wine_root))
        self.assertEqual(self.tool_log_text(), "")

    def test_wine_install_launches_installer_in_a_managed_prefix(self):
        installer = os.path.join(self.dir, "Setup With Space.exe")
        with open(installer, "wb") as handle:
            handle.write(b"Windows installer")

        result = self.run_pkg("wine", "install", "paint", installer)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Started Windows installer 'paint'", result.stdout)
        app_dir = os.path.join(self.wine_root, "paint")
        self.assertTrue(os.path.isdir(os.path.join(app_dir, "prefix")))
        with open(os.path.join(app_dir, "executable"), "r", encoding="utf-8") as handle:
            self.assertEqual(handle.read(), installer + "\n")
        self.assertEqual(self.tool_log_text(), "finnctl\twine.run\tpaint\n")

    def test_wine_set_selects_the_installed_exe_in_the_existing_prefix(self):
        registered, _ = self.register_wine_app("paint", "Setup.exe")
        self.assertEqual(registered.returncode, 0, registered.stderr)
        installed = os.path.join(self.dir, "Installed Paint.exe")
        with open(installed, "wb") as handle:
            handle.write(b"installed Windows application")

        selected = self.run_pkg("wine", "set", "paint", installed)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        self.assertIn("updated paint", selected.stdout)
        with open(os.path.join(self.wine_root, "paint", "executable"),
                  "r", encoding="utf-8") as handle:
            self.assertEqual(handle.read(), installed + "\n")
        self.assertTrue(os.path.isdir(os.path.join(self.wine_root, "paint", "prefix")))
        self.assertEqual(self.tool_log_text(), "")

        launched = self.run_pkg("wine", "run", "paint")
        self.assertEqual(launched.returncode, 0, launched.stderr)
        self.assertEqual(self.tool_log_text(), "finnctl\twine.run\tpaint\n")

    def test_wine_install_and_set_reject_invalid_or_unregistered_requests(self):
        invalid = self.run_pkg("wine", "install", "../paint", "/tmp/setup.exe")
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("Wine app id", invalid.stderr)
        self.assertFalse(os.path.exists(self.wine_root))

        unregistered = self.run_pkg("wine", "set", "paint", "/tmp/app.exe")
        self.assertNotEqual(unregistered.returncode, 0)
        self.assertIn("not registered", unregistered.stderr)
        self.assertEqual(self.tool_log_text(), "")

    def test_wine_register_refuses_to_replace_an_existing_registration(self):
        first, _ = self.register_wine_app("paint", "First.exe")
        self.assertEqual(first.returncode, 0, first.stderr)
        second, _ = self.register_wine_app("paint", "Second.exe")
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("already registered", second.stderr)


if __name__ == "__main__":
    unittest.main()
