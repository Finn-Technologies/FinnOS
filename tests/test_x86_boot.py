"""ISO target-architecture and QEMU boot checks.

The tests in this file are in two layers:

  * the profile tests, which need nothing but the tree and a fake mkarchiso;
  * the QEMU tests, which are skipped unless FINNOS_ISO names a built
    image. The boot-to-installer test runs on every ISO build; the full
    install test is opt-in through FINNOS_FULL_INSTALL=1 because it
    pacstraps a base system and builds the compositor on the target disk.

The ISO is built natively with mkarchiso from the tree in base/; there is
no downloaded base image and no remaster path to test.
"""

import glob
import http.server
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.environ.get("FINNOS_ISO", "")
QEMU = shutil.which("qemu-system-x86_64")
XORRISO = shutil.which("xorriso")


class X86BootBuildTest(unittest.TestCase):

    def test_native_profiles_follow_requested_arch_and_stage_uefi_entries(self):
        with tempfile.TemporaryDirectory() as temp:
            tools = os.path.join(temp, "tools")
            os.makedirs(tools)
            capture = os.path.join(temp, "profile.txt")

            fake_id = os.path.join(tools, "id")
            with open(fake_id, "w", encoding="utf-8") as handle:
                handle.write(
                    "#!/bin/sh\n"
                    "if [ \"$1\" = \"-u\" ]; then echo 0; exit 0; fi\n"
                    "exec /usr/bin/id \"$@\"\n"
                )
            os.chmod(fake_id, 0o755)

            fake_mkarchiso = os.path.join(tools, "mkarchiso")
            with open(fake_mkarchiso, "w", encoding="utf-8") as handle:
                handle.write(
                    "#!/bin/bash\n"
                    "set -euo pipefail\n"
                    "profile=''\n"
                    "out=''\n"
                    "while [ \"$#\" -gt 0 ]; do\n"
                    "  case \"$1\" in\n"
                    "    -o) out=\"$2\"; shift 2 ;;\n"
                    "    *) profile=\"$1\"; shift ;;\n"
                    "  esac\n"
                    "done\n"
                    "source \"$profile/profiledef.sh\"\n"
                    "{\n"
                    "  printf 'arch=%s\\n' \"$arch\"\n"
                    "  printf 'pacman_conf=%s\\n' \"$pacman_conf\"\n"
                    "  printf 'bootmodes=%s\\n' \"${bootmodes[*]}\"\n"
                    "  for package in archiso mkinitcpio-archiso syslinux; do\n"
                    "    if grep -qx \"$package\" \"$profile/packages.$arch\"; then\n"
                    "      printf '%s=present\\n' \"$package\"\n"
                    "    else\n"
                    "      printf '%s=absent\\n' \"$package\"\n"
                    "    fi\n"
                    "  done\n"
                    "  for path in efiboot/loader/loader.conf \\\n"
                    "      efiboot/loader/entries/01-finnos-install.conf \\\n"
                    "      efiboot/loader/entries/02-finnos-live.conf; do\n"
                    "    if [ -f \"$profile/$path\" ]; then\n"
                    "      printf '%s=present\\n' \"$path\"\n"
                    "    else\n"
                    "      printf '%s=missing\\n' \"$path\"\n"
                    "    fi\n"
                    "  done\n"
                    "  for path in \"$profile\"/efiboot/loader/entries/*.conf; do\n"
                    "    if grep -q \"/finn/boot/$arch/\" \"$path\"; then\n"
                    "      printf '%s=matching-arch\\n' \"$(basename \"$path\")\"\n"
                    "    fi\n"
                    "  done\n"
                    "} > \"$FINN_CAPTURE\"\n"
                    "mkdir -p \"$out\"\n"
                    "printf 'mock ISO\\n' > \"$out/finnos.iso\"\n"
                )
            os.chmod(fake_mkarchiso, 0o755)

            env = os.environ.copy()
            env["PATH"] = tools + os.pathsep + env.get("PATH", "")
            env["FINN_CAPTURE"] = capture

            for target_arch in ("x86_64", "aarch64"):
                with self.subTest(target_arch=target_arch):
                    output = os.path.join(temp, "finnos-{}.iso".format(target_arch))
                    result = subprocess.run(
                        [
                            os.path.join(ROOT, "base", "mkfinniso"),
                            "--mode=native",
                            "--arch={}".format(target_arch),
                            output,
                        ],
                        cwd=ROOT,
                        env=env,
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        check=False,
                    )

                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    with open(capture, "r", encoding="utf-8") as handle:
                        profile = handle.read()
                    self.assertIn("arch={}".format(target_arch), profile)
                    self.assertIn("pacman_conf=pacman.conf", profile)
                    self.assertIn("archiso=present", profile)
                    self.assertIn("mkinitcpio-archiso=present", profile)
                    if target_arch == "x86_64":
                        self.assertIn(
                            "bootmodes=bios.syslinux uefi.systemd-boot", profile
                        )
                        self.assertIn("syslinux=present", profile)
                        self.assertIn(
                            "It boots: BIOS (syslinux) and UEFI (systemd-boot)",
                            result.stdout,
                        )
                    else:
                        self.assertIn("bootmodes=uefi.systemd-boot", profile)
                        self.assertIn("syslinux=absent", profile)
                        self.assertIn("It boots: UEFI (systemd-boot)", result.stdout)
                    self.assertIn("efiboot/loader/loader.conf=present", profile)
                    for entry in (
                        "01-finnos-install.conf",
                        "02-finnos-live.conf",
                    ):
                        self.assertIn(
                            "efiboot/loader/entries/{}=present".format(entry),
                            profile,
                        )
                        self.assertIn("{}=matching-arch".format(entry), profile)
                    self.assertTrue(os.path.isfile(output))


def read_packages(architecture):
    path = os.path.join(ROOT, "base", "packages." + architecture)
    with open(path, "r", encoding="utf-8") as handle:
        return {
            line.strip()
            for line in handle
            if line.strip() and not line.lstrip().startswith("#")
        }


class PackageListTest(unittest.TestCase):
    """The hardware set is the difference between a machine that draws and
    one that boots to a console. Both lists are checked against what their
    architecture can actually resolve."""

    def test_x86_64_carries_the_hardware_stack(self):
        packages = read_packages("x86_64")
        self.assertTrue({
            "linux-firmware", "intel-ucode", "amd-ucode", "mesa", "libglvnd",
            "vulkan-intel", "lib32-mesa", "lib32-vulkan-icd-loader",
            "lib32-vulkan-intel", "wine", "winetricks", "wine-mono",
            "wine-gecko", "pipewire", "pipewire-pulse", "wireplumber",
            "bluez", "bluez-utils", "jq", "zstd",
        }.issubset(packages))

    def test_aarch64_is_honest_about_what_arm_does_not_have(self):
        packages = read_packages("aarch64")
        self.assertTrue({
            "linux-firmware", "mesa", "libglvnd", "pipewire",
            "pipewire-pulse", "wireplumber", "bluez", "bluez-utils",
            "jq", "zstd",
        }.issubset(packages))
        self.assertFalse({
            "wine", "winetricks", "wine-mono", "wine-gecko", "vulkan-intel",
            "lib32-mesa", "lib32-vulkan-icd-loader", "lib32-vulkan-intel",
        } & packages)

    def test_neither_list_names_a_package_twice(self):
        for architecture in ("x86_64", "aarch64"):
            with self.subTest(architecture=architecture):
                path = os.path.join(ROOT, "base", "packages." + architecture)
                with open(path, "r", encoding="utf-8") as handle:
                    names = [
                        line.strip()
                        for line in handle
                        if line.strip() and not line.lstrip().startswith("#")
                    ]
                self.assertEqual(len(names), len(set(names)), path)

    def test_multilib_is_wired_into_every_pacman_writer(self):
        for path in (
            os.path.join(ROOT, "base", "setup.sh"),
            os.path.join(ROOT, "vm", "finn-install.sh"),
            os.path.join(ROOT, "vm", "finn-chroot.sh"),
        ):
            with self.subTest(path=path):
                with open(path, "r", encoding="utf-8") as handle:
                    self.assertIn("pacman.multilib.conf", handle.read())


# ---------------------------------------------------------------------------
# QEMU
# ---------------------------------------------------------------------------

class _CaptureServer(http.server.HTTPServer):
    allow_reuse_address = True

    def __init__(self, address, handler):
        super().__init__(address, handler)
        self.posts = []


class _CaptureHandler(http.server.BaseHTTPRequestHandler):

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace")
        self.server.posts.append(body)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, fmt, *args):
        pass


def start_capture():
    server = _CaptureServer(("0.0.0.0", 8765), _CaptureHandler)
    server.timeout = 0.5
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def stop_capture(server, thread):
    server.shutdown()
    server.server_close()
    thread.join(2)


def wait_for(server, needle, timeout, process=None, stop=()):
    """Wait for a needle in the captured posts.

    A marker in stop means the run has already failed; returning as soon as
    one appears keeps a failed install from waiting out the whole timeout.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        for body in list(server.posts):
            if needle in body:
                return True
            if any(marker in body for marker in stop):
                return False
        if process is not None and process.poll() is not None:
            return False
        time.sleep(1)
    return False


def serial_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def wait_for_installer(server, serial, timeout, process=None):
    """Wait for either proof that finn-install started.

    An install entry that names a disk posts its log, and the first post
    that says the payload was found is the whole chain. The default entry
    names no disk, so the installer asks for one; that question is on the
    serial console, and seeing it is the same proof without requiring a
    post that a no-disk install never makes.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        for body in list(server.posts):
            if "payload found in the live root" in body:
                return True
        if "Disk to install onto" in serial_text(serial):
            return True
        if process is not None and process.poll() is not None:
            return False
        time.sleep(1)
    return False


def ovmf_pair():
    code = os.environ.get("OVMF_CODE")
    variables = os.environ.get("OVMF_VARS")
    if code and variables and os.path.isfile(code) and os.path.isfile(variables):
        return code, variables
    for code, variables in (
        ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
        ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
        ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
        ("/usr/share/edk2/x64/OVMF_CODE.fd", "/usr/share/edk2/x64/OVMF_VARS.fd"),
    ):
        if os.path.isfile(code) and os.path.isfile(variables):
            return code, variables
    return None


def qemu_network():
    # The installer posts to 192.168.44.1:8765, so the host alias is that
    # address rather than slirp's usual 10.0.2.2. The capture server then
    # sees the guest's POSTs on the host loopback.
    return [
        "-netdev", "user,id=n0,net=192.168.44.0/24,host=192.168.44.1",
        "-device", "e1000,netdev=n0",
    ]


def launch(iso, work, extra=(), uefi=False):
    serial = os.path.join(work, "serial.log")
    command = [
        QEMU,
        "-machine", "q35",
        "-m", "2048",
        "-smp", "2",
        "-cdrom", iso,
        "-boot", "order=d",
        *qemu_network(),
        "-display", "none",
        "-serial", "file:" + serial,
        "-monitor", "none",
        "-no-reboot",
        *extra,
    ]
    if uefi:
        pair = ovmf_pair()
        if pair is None:
            return None, None, None
        code, variables = pair
        variables = os.path.join(work, "OVMF_VARS.fd")
        shutil.copyfile(pair[1], variables)
        command += [
            "-drive", "if=pflash,format=raw,readonly=on,file=" + code,
            "-drive", "if=pflash,format=raw,file=" + variables,
        ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return process, serial, command


def stop(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=15)


def tail(path, lines=40):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return "".join(handle.readlines()[-lines:])
    except OSError:
        return ""


@unittest.skipUnless(
    ISO and os.path.isfile(ISO) and QEMU,
    "set FINNOS_ISO to a built ISO and install qemu-system-x86_64",
)
class QemuBootTest(unittest.TestCase):
    """Boot the real ISO through firmware and wait for the installer.

    The default install entry names no disk, so finn-install asks for one.
    That question reaching the serial console proves the whole chain ran:
    firmware, boot loader, kernel, initramfs, live root, autologin and
    finn-install. An entry that does name a disk is also accepted when its
    log says the payload was found in the live root. Under UEFI the run
    also proves the menu inside the appended FAT partition is FinnOS's.
    """

    def boot(self, uefi):
        if uefi and ovmf_pair() is None:
            self.skipTest("no OVMF firmware; install ovmf for the UEFI check")
        timeout = int(os.environ.get("FINNOS_QEMU_TIMEOUT", "900"))
        server, thread = start_capture()
        work = tempfile.mkdtemp(prefix="finnos-qemu-")
        process = None
        try:
            process, serial, command = launch(ISO, work, uefi=uefi)
            self.assertIsNotNone(process, " ".join(command or []))
            found = wait_for_installer(server, serial, timeout, process)
            if not found:
                self.fail(
                    "the guest did not reach the installer within {}s\n"
                    "command: {}\n"
                    "posts:\n{}\n"
                    "serial:\n{}".format(
                        timeout,
                        " ".join(command),
                        "\n---\n".join(server.posts[-5:]) or "(none)",
                        tail(serial, 120),
                    )
                )
        finally:
            stop(process)
            stop_capture(server, thread)
            shutil.rmtree(work, ignore_errors=True)

    def test_bios_boots_to_the_installer(self):
        self.boot(uefi=False)

    def test_uefi_boots_to_the_installer(self):
        self.boot(uefi=True)


@unittest.skipUnless(
    ISO and os.path.isfile(ISO) and QEMU and XORRISO and
    os.environ.get("FINNOS_FULL_INSTALL") == "1",
    "set FINNOS_ISO and FINNOS_FULL_INSTALL=1, with qemu and xorriso",
)
class QemuFullInstallTest(unittest.TestCase):
    """An unattended install onto a real disk image.

    This is opt-in because it downloads a base system and builds the
    compositor. It verifies the installer's own final checks (the binaries,
    the enabled units, the boot entry and the hardware packages on the
    disk). It does not reboot the installed disk: that is the manual check
    in docs/architecture.md, and a QEMU GPU is not the OptiPlex's GPU.
    """

    def test_installer_completes_on_a_disk(self):
        timeout = int(os.environ.get("FINNOS_QEMU_TIMEOUT", "3600"))
        work = tempfile.mkdtemp(prefix="finnos-install-")
        server, thread = start_capture()
        process = None
        try:
            disk = os.path.join(work, "disk.img")
            with open(disk, "wb") as handle:
                handle.truncate(24 * 1024 ** 3)
            kernel = os.path.join(work, "vmlinuz-linux")
            initramfs = os.path.join(work, "initramfs-linux.img")
            for source, destination in (
                ("/finn/boot/x86_64/vmlinuz-linux", kernel),
                ("/finn/boot/x86_64/initramfs-linux.img", initramfs),
            ):
                subprocess.run(
                    [XORRISO, "-osirrox", "on", "-indev", ISO,
                     "-extract", source, destination],
                    check=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            extra = [
                "-drive", "file=" + disk + ",if=virtio,format=raw",
                "-kernel", kernel,
                "-initrd", initramfs,
                "-append", (
                    "archisobasedir=finn archisodevice=/dev/sr0 "
                    "console=ttyS0 systemd.firstboot=off "
                    "finn.install finn.disk=/dev/vda finn.yes"
                ),
            ]
            process, serial, command = launch(ISO, work, extra=extra)
            self.assertIsNotNone(process, " ".join(command or []))
            complete = wait_for(
                server, "FINN_INSTALL_COMPLETE", timeout, process,
                stop=("FINN_INSTALL_FAILED",),
            )
            posts = "\n---\n".join(server.posts[-10:])
            self.assertTrue(
                complete,
                "the unattended install did not complete within {}s\n"
                "posts:\n{}\nserial:\n{}".format(
                    timeout, posts or "(none)", tail(serial, 60)
                ),
            )
            self.assertIn("FINN_CHROOT_COMPLETE", posts)
            self.assertIn("HARDWARE=", posts)
            self.assertNotIn("FINN_INSTALL_FAILED", posts)
        finally:
            stop(process)
            stop_capture(server, thread)
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
