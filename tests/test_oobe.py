"""End-to-end tests for first-boot OOBE provisioning helper (tools/finn-oobe-apply)."""

import os
import stat
import subprocess
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HELPER = os.path.join(ROOT, "tools", "finn-oobe-apply")


class OobeApplyHelperTest(unittest.TestCase):

    def test_helper_exists_and_is_executable(self):
        self.assertTrue(os.path.isfile(HELPER))
        self.assertTrue(os.stat(HELPER).st_mode & stat.S_IXUSR)

    def test_applies_settings_writes_marker_and_retires_sudoers_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "etc", "sudoers.d"), exist_ok=True)
            os.makedirs(os.path.join(tmp, "etc", "polkit-1", "rules.d"), exist_ok=True)
            with open(os.path.join(tmp, "etc", "locale.gen"), "w", encoding="utf-8") as f:
                f.write("# en_US.UTF-8 UTF-8\n# de_DE.UTF-8 UTF-8\n")
            sudoers_file = os.path.join(tmp, "etc", "sudoers.d", "finn-oobe")
            with open(sudoers_file, "w", encoding="utf-8") as f:
                f.write("finn ALL=(root) NOPASSWD: /usr/local/libexec/finn-oobe-apply\n")

            env = os.environ.copy()
            env["FINN_OOBE_TEST_ROOT"] = tmp
            payload = (
                "locale=de_DE.UTF-8\n"
                "region=DE\n"
                "timezone=Europe/Berlin\n"
                "kb_layout=de\n"
                "kb_variant=nodeadkeys\n"
                "hostname=berlin-ws\n"
                "username=ada\n"
                "full_name=Ada Lovelace\n"
                "password=s3cr3tPass!\n"
                "dark=1\n"
                "avatar_color=indigo\n"
            )
            res = subprocess.run(
                [HELPER],
                input=payload,
                text=True,
                env=env,
                capture_output=True,
                check=False,
            )
            self.assertEqual(res.returncode, 0, res.stderr)

            with open(os.path.join(tmp, "etc", "locale.conf"), "r", encoding="utf-8") as f:
                self.assertEqual(f.read().strip(), "LANG=de_DE.UTF-8")
            with open(os.path.join(tmp, "etc", "hostname"), "r", encoding="utf-8") as f:
                self.assertEqual(f.read().strip(), "berlin-ws")
            with open(os.path.join(tmp, "etc", "hosts"), "r", encoding="utf-8") as f:
                self.assertIn("berlin-ws.localdomain berlin-ws", f.read())
            with open(os.path.join(tmp, "etc", "vconsole.conf"), "r", encoding="utf-8") as f:
                self.assertEqual(f.read().strip(), "KEYMAP=de")
            self.assertEqual(
                os.readlink(os.path.join(tmp, "etc", "localtime")),
                "/usr/share/zoneinfo/Europe/Berlin",
            )
            with open(os.path.join(tmp, "var", "lib", "finn", "settings"), "r", encoding="utf-8") as f:
                settings_txt = f.read()
                self.assertIn("kb_layout=de\n", settings_txt)
                self.assertIn("kb_variant=nodeadkeys\n", settings_txt)
                self.assertIn("dark=1\n", settings_txt)
                self.assertIn("avatar_color=indigo\n", settings_txt)

            self.assertTrue(os.path.isfile(os.path.join(tmp, "var", "lib", "finn", "oobe_complete")))
            self.assertFalse(os.path.exists(sudoers_file))

            # Second invocation MUST be rejected once oobe_complete exists.
            second = subprocess.run(
                [HELPER],
                input=payload,
                text=True,
                env=env,
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(second.returncode, 0)
            self.assertIn("already complete", second.stderr)

    def test_rejects_invalid_hostname_timezone_or_username(self):
        for bad_line in (
            "hostname=Bad_Host!\n",
            "timezone=../../etc/passwd\n",
            "username=root\n",
            "username=1bad\n",
        ):
            with self.subTest(bad_line=bad_line.strip()):
                with tempfile.TemporaryDirectory() as tmp:
                    env = os.environ.copy()
                    env["FINN_OOBE_TEST_ROOT"] = tmp
                    res = subprocess.run(
                        [HELPER],
                        input=bad_line,
                        text=True,
                        env=env,
                        capture_output=True,
                        check=False,
                    )
                    self.assertNotEqual(res.returncode, 0)
                    self.assertFalse(
                        os.path.exists(os.path.join(tmp, "var", "lib", "finn", "oobe_complete"))
                    )


if __name__ == "__main__":
    unittest.main()
