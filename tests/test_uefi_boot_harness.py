"""Real QEMU + EDK2 OVMF graphical UEFI bootloader verification harness.

Builds src/boot/finn-bootx64.efi and src/boot/test-stubx64.efi, mounts a
virtual FAT32 EFI System Partition populated with:
  - /EFI/BOOT/BOOTX64.EFI (finn-bootx64.efi)
  - /loader/loader.conf (timeout 10)
  - /loader/entries/01-finnos.conf (FinnOS -> test-stubx64.efi)
  - /loader/entries/02-finnos-recovery.conf (Recovery)
  - /loader/entries/03-ubuntu.conf (Linux)
  - /EFI/Microsoft/Boot/bootmgfw.efi (Windows auto-detection -> test-stubx64.efi)
  - /System/Library/CoreServices/boot.efi (macOS auto-detection -> test-stubx64.efi)

Boots inside real qemu-system-x86_64 with EDK2 OVMF firmware, captures a real
QMP screendump of the GOP macOS Startup Manager UI, sends QMP keyboard events
(right arrow -> Windows, enter -> boot), and verifies that the chainloaded UEFI
payload receives the real systemd Boot Loader Interface EFI variables
(LoaderInfo, LoaderEntries, LoaderEntrySelected) over serial.
"""

import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = shutil.which("qemu-system-x86_64")


def find_edk2_x64():
    for path in (
        "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",
        "/usr/share/OVMF/OVMF_CODE_4M.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
        "/usr/share/edk2/ovmf/OVMF_CODE.fd",
        "/usr/share/edk2/x64/OVMF_CODE.fd",
    ):
        if os.path.isfile(path):
            return path
    return None


def qmp_cmd(sock, cmd, args=None):
    payload = {"execute": cmd}
    if args is not None:
        payload["arguments"] = args
    sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
    data = b""
    while b"\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", "replace")


@unittest.skipUnless(QEMU and find_edk2_x64(), "requires qemu-system-x86_64 and EDK2 OVMF")
class UefiBootloaderQemuHarnessTest(unittest.TestCase):

    def test_gop_graphical_boot_picker_and_systemd_efi_vars_in_ovmf(self):
        # 1. Build finn-bootx64.efi and test-stubx64.efi from source
        build = subprocess.run(
            ["make", "-C", os.path.join(ROOT, "src", "boot"), "all"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

        boot_efi = os.path.join(ROOT, "src", "boot", "finn-bootx64.efi")
        stub_efi = os.path.join(ROOT, "src", "boot", "test-stubx64.efi")
        self.assertTrue(os.path.isfile(boot_efi))
        self.assertTrue(os.path.isfile(stub_efi))

        with tempfile.TemporaryDirectory(prefix="finn-uefi-ovmf-") as work:
            esp = os.path.join(work, "esp")
            for rel in (
                "EFI/BOOT",
                "EFI/Microsoft/Boot",
                "System/Library/CoreServices",
                "loader/entries",
            ):
                os.makedirs(os.path.join(esp, rel), exist_ok=True)

            shutil.copyfile(boot_efi, os.path.join(esp, "EFI", "BOOT", "BOOTX64.EFI"))
            shutil.copyfile(stub_efi, os.path.join(esp, "EFI", "Microsoft", "Boot", "bootmgfw.efi"))
            shutil.copyfile(stub_efi, os.path.join(esp, "System", "Library", "CoreServices", "boot.efi"))
            shutil.copyfile(stub_efi, os.path.join(esp, "vmlinuz-stub.efi"))

            with open(os.path.join(esp, "loader", "loader.conf"), "w", encoding="utf-8") as f:
                f.write("default 01-finnos.conf\ntimeout 12\neditor yes\nauto-entries yes\n")
            with open(os.path.join(esp, "loader", "entries", "01-finnos.conf"), "w", encoding="utf-8") as f:
                f.write("title FinnOS\nversion 1.0\nsort-key finnos\nefi /vmlinuz-stub.efi\noptions quiet splash\n")
            with open(os.path.join(esp, "loader", "entries", "02-finnos-recovery.conf"), "w", encoding="utf-8") as f:
                f.write("title FinnOS Recovery\nversion 1.0\nefi /vmlinuz-stub.efi\noptions finn.recovery\n")
            with open(os.path.join(esp, "loader", "entries", "03-ubuntu.conf"), "w", encoding="utf-8") as f:
                f.write("title Ubuntu\nversion 24.04\nefi /vmlinuz-stub.efi\noptions quiet\n")

            serial_log = os.path.join(work, "serial.log")
            qmp_sock_path = os.path.join(work, "qmp.sock")
            screendump_ppm = os.environ.get("FINN_BOOT_SCREENDUMP", os.path.join(work, "boot_gop.ppm"))

            cmd = [
                QEMU,
                "-machine", "q35",
                "-m", "512",
                "-drive", f"if=pflash,format=raw,readonly=on,file={find_edk2_x64()}",
                "-drive", f"format=raw,file=fat:rw:{esp}",
                "-vga", "std",
                "-display", "none",
                "-serial", f"file:{serial_log}",
                "-qmp", f"unix:{qmp_sock_path},server,nowait",
                "-no-reboot",
            ]
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                # Wait for QMP socket
                for _ in range(50):
                    if os.path.exists(qmp_sock_path):
                        break
                    time.sleep(0.1)
                self.assertTrue(os.path.exists(qmp_sock_path), "QMP socket did not appear")

                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(5.0)
                sock.connect(qmp_sock_path)
                # Read QMP greeting & enter command mode
                sock.recv(4096)
                qmp_cmd(sock, "qmp_capabilities")

                # Wait for OVMF + finn-bootx64.efi to render the GOP Startup Manager
                time.sleep(4.0)
                qmp_cmd(sock, "screendump", {"filename": screendump_ppm})
                time.sleep(0.4)
                self.assertTrue(os.path.isfile(screendump_ppm), "QMP screendump failed")
                with open(screendump_ppm, "rb") as pf:
                    ppm_data = pf.read()
                self.assertTrue(ppm_data.startswith(b"P6"), "screendump is not a valid P6 PPM")
                # Verify framebuffer contains rendered FinnOS blue accent pixels (#7AB8FF / #007BFF)
                self.assertIn(b"\x7a\xb8\xff", ppm_data, "GOP Startup Manager did not render FinnOS accent mark/cards")

                # Navigate right (FinnOS -> Windows) and press Enter to chainload bootmgfw.efi
                qmp_cmd(sock, "send-key", {"keys": [{"type": "qcode", "data": "right"}]})
                time.sleep(0.3)
                qmp_cmd(sock, "send-key", {"keys": [{"type": "qcode", "data": "ret"}]})
                sock.close()

                # Wait for QEMU to exit after test-stubx64.efi prints EFI vars and shuts down
                proc.wait(timeout=20)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)

            with open(serial_log, "r", encoding="utf-8", errors="replace") as sf:
                serial_out = sf.read()

            self.assertIn("FINN_UEFI_HANDOFF_OK", serial_out, serial_out)
            self.assertIn("LoaderInfo=systemd-boot 257 (finn-boot 1.0)", serial_out, serial_out)
            self.assertIn("LoaderEntrySelected=auto-windows", serial_out, serial_out)
            self.assertIn("01-finnos.conf", serial_out, serial_out)
            self.assertIn("auto-windows", serial_out, serial_out)
            self.assertIn("auto-macos", serial_out, serial_out)
            self.assertIn("03-ubuntu.conf", serial_out, serial_out)
            self.assertIn("02-finnos-recovery.conf", serial_out, serial_out)


if __name__ == "__main__":
    unittest.main()
