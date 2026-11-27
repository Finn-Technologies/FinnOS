"""End-to-end hermetic tests for FinnOS offline Recovery Mode installer.

Executes the REAL vm/finn-install.sh across every installation scenario using
an isolated fake-tool + real-filesystem harness (no root, no internet):
  - Clean Erase GPT (UEFI+BIOS hybrid) + FINNOS_RECOVERY offline partition
  - Clean Erase MBR (Legacy BIOS)
  - Clean Erase A/B OTA slots (GPT)
  - Dual-Boot Resize Windows 11 GPT with 100 MiB ESP (/boot/efi) & trailing WinRE
  - Dual-Boot Resize Windows 10 MBR with 3 primary partitions (extended+logical)
  - Dual-Boot Free Space (macOS APFS + unallocated gap + auto-generated ESP)
  - Target Partition on UEFI GPT with NO ESP (auto-splits into 512M ESP + root)
  - Target Partition on Legacy BIOS GPT with NO BIOS Boot (auto-carves ef02)
  - Reinstall Preserve Data (keeps /home, passwd, hostname, oobe_complete)
  - Bootloader Repair (auto-formats broken ESP, rebuilds boot entries)
  - Offline SHA-512 integrity check (valid passes, 1-byte tamper rejected)
  - Zero internet guarantee (mock curl FAILS immediately if ever invoked!)
  - Live USB erase protection & recovery systemd unit wiring.
"""

import hashlib
import os
import stat
import subprocess
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSTALLER = os.path.join(ROOT, "vm", "finn-install.sh")


class _InstallerHarness:
    """Hermetic fake block-device & tool environment for running vm/finn-install.sh."""

    def __init__(self, tmp_dir, *, pttype="gpt", disk_bytes=64 * 1024**3,
                 live_disk="/dev/sdb", existing_parts=None,
                 parted_print="", parted_free="", esp_size_bytes=512 * 1024**2,
                 create_live_image=True, tamper_sfs=False, use_sfs_only=False):
        tmp_dir = os.path.realpath(tmp_dir)
        self.tmp = tmp_dir
        self.bin_dir = os.path.join(tmp_dir, "bin")
        self.dev_dir = os.path.join(tmp_dir, "dev")
        self.mnt_dir = os.path.join(tmp_dir, "mnt")
        self.airootfs = os.path.join(tmp_dir, "live", "airootfs")
        self.bootmnt = os.path.join(tmp_dir, "live", "bootmnt")
        self.recovery_mnt = os.path.join(tmp_dir, "live", "recovery")
        self.sys_efi = os.path.join(tmp_dir, "sys_efi")
        self.state_dir = os.path.join(tmp_dir, "state")
        self.calls_log = os.path.join(self.state_dir, "calls.log")
        self.sfdisk_log = os.path.join(self.state_dir, "sfdisk.log")
        self.mounts_file = os.path.join(self.state_dir, "mounts.txt")
        self.parts_file = os.path.join(self.state_dir, "parts.txt")
        self.parted_print_file = os.path.join(self.state_dir, "parted_print.txt")
        self.parted_free_file = os.path.join(self.state_dir, "parted_free.txt")
        self.log_file = os.path.join(tmp_dir, "install.log")

        for d in (self.bin_dir, self.dev_dir, self.mnt_dir,
                  self.recovery_mnt, self.sys_efi, self.state_dir):
            os.makedirs(d, exist_ok=True)

        self.disk = os.path.join(self.dev_dir, "nvme0n1")
        with open(self.disk, "wb") as f:
            f.write(b"FAKE_DISK")

        # Create pre-existing partition device nodes.
        self.existing_parts = existing_parts or []
        with open(self.parts_file, "w", encoding="utf-8") as pf:
            for p in self.existing_parts:
                pnode = os.path.join(self.dev_dir, p["name"])
                with open(pnode, "wb") as f:
                    f.write(b"PART")
                pf.write(f"{pnode}|{p['name']}|{p.get('fstype','')}|{p.get('size',0)}|{p.get('partn',1)}\n")

        with open(self.parted_print_file, "w", encoding="utf-8") as f:
            f.write(parted_print)
        with open(self.parted_free_file, "w", encoding="utf-8") as f:
            f.write(parted_free)
        open(self.mounts_file, "w", encoding="utf-8").close()
        open(self.calls_log, "w", encoding="utf-8").close()

        # Create realistic offline live image + SHA-512 checksum.
        arch = os.uname().machine
        if create_live_image:
            sfs_dir = os.path.join(self.bootmnt, "finn", arch)
            boot_dir = os.path.join(self.bootmnt, "finn", "boot", arch)
            os.makedirs(sfs_dir, exist_ok=True)
            os.makedirs(boot_dir, exist_ok=True)
            with open(os.path.join(boot_dir, "vmlinuz-linux"), "wb") as f:
                f.write(b"FAKE_KERNEL")
            with open(os.path.join(boot_dir, "vmlinuz-linux-aarch64"), "wb") as f:
                f.write(b"FAKE_KERNEL")
            sfs_payload = b"hsqs_FAKE_FINNOS_SQUASHFS_IMAGE_CONTENT_1234567890"
            sfs_path = os.path.join(sfs_dir, "airootfs.sfs")
            with open(sfs_path, "wb") as f:
                f.write(sfs_payload)
            digest = hashlib.sha512(sfs_payload).hexdigest()
            with open(os.path.join(sfs_dir, "airootfs.sha512"), "w", encoding="utf-8") as f:
                f.write(f"{digest}  airootfs.sfs\n")
            if tamper_sfs:
                with open(sfs_path, "wb") as f:
                    f.write(b"hsqs_TAMPERED_IMAGE_CONTENT_1234567890")

            if not use_sfs_only:
                self._populate_rootfs(self.airootfs)

        self._install_mock_tools(pttype=pttype, disk_bytes=disk_bytes,
                                 live_disk=live_disk, esp_size_bytes=esp_size_bytes)

    def _populate_rootfs(self, root_dir):
        for rel in (
            "usr/bin",
            "usr/local/bin",
            "var/lib/pacman/local",
            "var/lib/finn",
            "etc/systemd/system/graphical.target.wants",
            "etc/systemd/system/getty@tty1.service.d",
            "root",
        ):
            os.makedirs(os.path.join(root_dir, rel), exist_ok=True)
        for b in ("finn-comp", "finn-session", "finn-shell", "finn-wallpaper", "recovery-mode"):
            bp = os.path.join(root_dir, "usr/local/bin", b)
            with open(bp, "w", encoding="utf-8") as f:
                f.write("#!/bin/sh\nexit 0\n")
            os.chmod(bp, 0o755)
        # Live-ISO markers that installer MUST strip on installed root.
        for frel in ("root/.zprofile", "root/.bash_profile", "etc/machine-id",
                     "var/lib/finn/oobe_complete"):
            with open(os.path.join(root_dir, frel), "w", encoding="utf-8") as f:
                f.write("live\n")

    def _write_tool(self, name, body):
        path = os.path.join(self.bin_dir, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write("#!/bin/bash\nset -euo pipefail\n" + body + "\n")
        os.chmod(path, 0o755)

    def _install_mock_tools(self, *, pttype, disk_bytes, live_disk, esp_size_bytes):
        st = self.state_dir
        dev = self.dev_dir
        disk = self.disk

        # STRICT ZERO-INTERNET GUARD: if curl is EVER called, fail the test immediately!
        self._write_tool("curl", f"""
echo "NETWORK_CALL_FORBIDDEN: curl $*" >> "{st}/calls.log"
echo "ERROR: curl invoked during offline installation!" >&2
exit 99
""")

        self._write_tool("lsblk", f"""
echo "lsblk $*" >> "{st}/calls.log"
args="$*"
if [[ "$args" == *"-dno TYPE"* ]]; then
    echo "disk"
    exit 0
fi
if [[ "$args" == *"-bno SIZE -d"* ]]; then
    echo "{disk_bytes}"
    exit 0
fi
if [[ "$args" == *"-bno SIZE"* ]]; then
    target="${{@: -1}}"
    sz=$(awk -F'|' -v p="$target" '$1==p{{print $4; exit}}' "{st}/parts.txt" 2>/dev/null || true)
    echo "${{sz:-{esp_size_bytes}}}"
    exit 0
fi
if [[ "$args" == *"-no PARTN"* ]]; then
    target="${{@: -1}}"
    n=$(awk -F'|' -v p="$target" '$1==p{{print $5; exit}}' "{st}/parts.txt" 2>/dev/null || true)
    if [ -z "$n" ]; then
        n=$(printf '%s' "$target" | sed -E 's/.*[^0-9]([0-9]+)$/\\1/')
    fi
    echo "$n"
    exit 0
fi
if [[ "$args" == *"-no PKNAME"* ]]; then
    target="${{@: -1}}"
    if [[ "$target" == "{live_disk}"* ]]; then
        basename "{live_disk}"
    else
        basename "{disk}"
    fi
    exit 0
fi
if [[ "$args" == *"-lno PATH,TYPE"* ]]; then
    echo "{disk} disk"
    awk -F'|' '{{print $1 " part"}}' "{st}/parts.txt"
    exit 0
fi
if [[ "$args" == *"-lno NAME,TYPE"* ]]; then
    echo "$(basename "{disk}") disk"
    awk -F'|' '{{print $2 " part"}}' "{st}/parts.txt"
    exit 0
fi
if [[ "$args" == *"-lno NAME"* ]]; then
    echo "$(basename "{disk}")"
    awk -F'|' '{{print $2}}' "{st}/parts.txt"
    exit 0
fi
echo "NAME SIZE TYPE"
""")

        self._write_tool("blkid", f"""
echo "blkid $*" >> "{st}/calls.log"
args="$*"
if [[ "$args" == *"-s PTTYPE"* ]]; then
    echo "{pttype}"
    exit 0
fi
if [[ "$args" == *"-s UUID"* ]]; then
    echo "11111111-2222-3333-4444-555555555555"
    exit 0
fi
if [[ "$args" == *"-s TYPE"* ]]; then
    target="${{@: -1}}"
    t=$(awk -F'|' -v p="$target" '$1==p{{print $3; exit}}' "{st}/parts.txt" 2>/dev/null || true)
    echo "${{t:-ext4}}"
    exit 0
fi
exit 0
""")

        self._write_tool("sfdisk", f"""
echo "sfdisk $*" >> "{st}/calls.log"
if [ "$1" = "-d" ]; then
    echo "label: {pttype}"
    exit 0
fi
body="$(cat)"
printf '%s\\n' "$body" > "{st}/sfdisk.log"
# Create partition nodes p1..p6 corresponding to lines in sfdisk spec.
: > "{st}/parts.txt"
idx=0
while IFS= read -r line; do
    [ -n "$line" ] || continue
    case "$line" in label:*) continue ;; esac
    idx=$((idx + 1))
    pnode="{disk}p${{idx}}"
    : > "$pnode"
    echo "$pnode|$(basename "$pnode")|raw|1000000000|$idx" >> "{st}/parts.txt"
done <<< "$body"
""")

        self._write_tool("parted", f"""
echo "parted $*" >> "{st}/calls.log"
args="$*"
if [[ "$args" == *"print free"* ]]; then
    cat "{st}/parted_free.txt"
    exit 0
fi
if [[ "$args" == *"print"* ]]; then
    cat "{st}/parted_print.txt"
    exit 0
fi
if [[ "$args" == *"mkpart"* ]]; then
    # Allocate next partition number and create fake device node
    max_n=$(awk -F'|' 'BEGIN{{m=0}} {{if($5+0>m) m=$5+0}} END{{print m}}' "{st}/parts.txt")
    next_n=$((max_n + 1))
    if [[ "$args" == *"logical"* ]] && [ "$next_n" -lt 5 ]; then
        next_n=5
    fi
    pnode="{disk}p${{next_n}}"
    : > "$pnode"
    echo "$pnode|$(basename "$pnode")|raw|20000000000|$next_n" >> "{st}/parts.txt"
    exit 0
fi
exit 0
""")

        self._write_tool("mkfs.vfat", f"""
echo "mkfs.vfat $*" >> "{st}/calls.log"
target="${{@: -1}}"
awk -F'|' -v p="$target" 'BEGIN{{OFS="|"}} $1==p{{$3="vfat"}} {{print}}' "{st}/parts.txt" > "{st}/parts.tmp" && mv "{st}/parts.tmp" "{st}/parts.txt"
""")

        self._write_tool("mkfs.ext4", f"""
echo "mkfs.ext4 $*" >> "{st}/calls.log"
target="${{@: -1}}"
awk -F'|' -v p="$target" 'BEGIN{{OFS="|"}} $1==p{{$3="ext4"}} {{print}}' "{st}/parts.txt" > "{st}/parts.tmp" && mv "{st}/parts.tmp" "{st}/parts.txt"
""")

        self._write_tool("mount", f"""
echo "mount $*" >> "{st}/calls.log"
src=""
dst=""
for a in "$@"; do
    case "$a" in -*) continue ;; esac
    if [ -z "$src" ]; then src="$a"; elif [ -z "$dst" ]; then dst="$a"; fi
done
# Handle -o ro / -o ro,loop where second arg is option value
if [ "$1" = "-o" ]; then src="$3"; dst="$4"; fi
mkdir -p "$dst"
printf '%s|%s\\n' "$dst" "$src" >> "{st}/mounts.txt"
""")

        self._write_tool("umount", f"""
echo "umount $*" >> "{st}/calls.log"
exit 0
""")

        self._write_tool("findmnt", f"""
echo "findmnt $*" >> "{st}/calls.log"
args="$*"
target="${{@: -1}}"
if [[ "$args" == *"-no SOURCE -T"* ]]; then
    if [ "$target" = "{self.bootmnt}" ] || [ "$target" = "{self.airootfs}" ]; then
        echo "{live_disk}1"
        exit 0
    fi
    src=$(awk -F'|' -v d="$target" '$1==d{{s=$2}} END{{print s}}' "{st}/mounts.txt" 2>/dev/null || true)
    echo "$src"
    exit 0
fi
if [[ "$args" == *"-no UUID"* ]]; then
    echo "11111111-2222-3333-4444-555555555555"
    exit 0
fi
if [[ "$args" == *"-rno TARGET -S"* ]]; then
    exit 0
fi
echo "TARGET SOURCE FSTYPE"
""")

        self._write_tool("genfstab", f"""
echo "genfstab $*" >> "{st}/calls.log"
echo "UUID=11111111-2222-3333-4444-555555555555 / ext4 rw,relatime 0 1"
""")

        self._write_tool("arch-chroot", f"""
echo "arch-chroot $*" >> "{st}/calls.log"
root="$1"
# Simulate successful chroot configuration, enabled units, and bootloader entries!
mkdir -p "$root/etc/systemd/system/graphical.target.wants" "$root/boot/grub" "$root/boot/loader/entries"
for u in finn-comp.service finn-session.service finn-shell.service finn-wallpaper.service; do
    touch "$root/etc/systemd/system/graphical.target.wants/$u"
done
echo "linux /vmlinuz-linux root=UUID=${{ROOTUUID}} rw quiet splash" > "$root/boot/grub/grub.cfg"
if [ "${{FINN_ESP_MOUNT:-}}" = "/boot" ]; then
    mkdir -p "$root/boot/EFI/BOOT"
    touch "$root/boot/EFI/BOOT/BOOTX64.EFI"
    printf 'title FinnOS\\noptions root=UUID=%s rw\\n' "${{ROOTUUID}}" > "$root/boot/loader/entries/finnos.conf"
elif [ "${{FINN_ESP_MOUNT:-}}" = "/boot/efi" ]; then
    mkdir -p "$root/boot/efi/EFI/FinnOS"
    touch "$root/boot/efi/EFI/FinnOS/grubx64.efi"
fi
echo "FINN_CHROOT_COMPLETE"
""")

        for simple in ("wipefs", "sgdisk", "partprobe", "udevadm", "swapon",
                       "swapoff", "ntfsfix", "ntfsresize", "e2fsck", "resize2fs",
                       "btrfs", "mountpoint", "unsquashfs"):
            if simple == "unsquashfs":
                self._write_tool(simple, f"""
echo "unsquashfs $*" >> "{st}/calls.log"
dst=""
prev=""
for a in "$@"; do
    if [ "$prev" = "-d" ]; then dst="$a"; fi
    prev="$a"
done
mkdir -p "$dst/usr/bin" "$dst/usr/local/bin" "$dst/var/lib/pacman/local" "$dst/boot"
for b in finn-comp finn-session finn-shell finn-wallpaper; do
    printf '#!/bin/sh\\nexit 0\\n' > "$dst/usr/local/bin/$b"
    chmod 755 "$dst/usr/local/bin/$b"
done
printf 'FAKE_KERNEL' > "$dst/boot/vmlinuz-linux"
""")
            else:
                self._write_tool(simple, f'echo "{simple} $*" >> "{st}/calls.log"\nexit 0')

    def run(self, extra_args, *, uefi=True):
        env = os.environ.copy()
        env["PATH"] = self.bin_dir + os.pathsep + env.get("PATH", "")
        env["FINN_TARGET_MNT"] = self.mnt_dir
        env["FINN_LIVE_AIROOTFS"] = self.airootfs
        env["FINN_LIVE_BOOTMNT"] = self.bootmnt
        env["FINN_RECOVERY_MNT"] = self.recovery_mnt
        env["FINN_SYS_EFI"] = self.sys_efi if uefi else os.path.join(self.tmp, "no_efi")
        env["FINN_SRC_DIR"] = ROOT
        env["FINN_INSTALL_LOG"] = self.log_file
        env["FINN_ALLOW_FILE_DISK"] = "1"
        env["FINN_SKIP_PKG_VERIFY"] = "1"
        return subprocess.run(
            [INSTALLER] + extra_args,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )

    def calls(self):
        with open(self.calls_log, "r", encoding="utf-8") as f:
            return f.read()

    def sfdisk_spec(self):
        if not os.path.isfile(self.sfdisk_log):
            return ""
        with open(self.sfdisk_log, "r", encoding="utf-8") as f:
            return f.read()


class RecoveryInstallerEndToEndTest(unittest.TestCase):

    def test_clean_erase_gpt_uefi_offline_with_recovery_partition(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt", disk_bytes=64 * 1024**3)
            res = h.run([
                "--disk", h.disk, "--mode", "clean", "--table", "gpt",
                "--create-recovery-part", "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)
            self.assertIn("image integrity verified (SHA-512)", res.stdout)
            self.assertNotIn("NETWORK_CALL_FORBIDDEN", h.calls())
            spec = h.sfdisk_spec()
            self.assertIn("label: gpt", spec)
            self.assertIn("FINNOS_EFI", spec)
            self.assertIn("BIOS_BOOT", spec)
            self.assertIn("FINNOS_RECOVERY", spec)
            # First-boot OOBE marker MUST be absent on fresh install.
            self.assertFalse(os.path.exists(os.path.join(h.mnt_dir, "var/lib/finn/oobe_complete")))
            # Live hooks MUST be stripped.
            self.assertFalse(os.path.exists(os.path.join(h.mnt_dir, "root/.zprofile")))

    def test_clean_erase_mbr_bios_offline(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="dos", disk_bytes=64 * 1024**3)
            res = h.run([
                "--disk", h.disk, "--mode", "clean", "--table", "mbr",
                "--offline-only", "--firmware", "bios", "--yes",
            ], uefi=False)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)
            self.assertIn("label: dos", h.sfdisk_spec())
            self.assertNotIn("NETWORK_CALL_FORBIDDEN", h.calls())

    def test_clean_erase_ab_slots_gpt(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt", disk_bytes=64 * 1024**3)
            res = h.run([
                "--disk", h.disk, "--mode", "clean", "--ab-slots",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)
            spec = h.sfdisk_spec()
            self.assertIn("FINNOS_A", spec)
            self.assertIn("FINNOS_B", spec)
            self.assertIn("FINNOS_DATA", spec)

    def test_dualboot_windows11_gpt_100m_esp_and_trailing_winre(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "vfat", "size": 100 * 1024**2, "partn": 1},
                {"name": "nvme0n1p2", "fstype": "ntfs", "size": 220 * 1024**3, "partn": 2},
                {"name": "nvme0n1p3", "fstype": "ntfs", "size": 650 * 1024**2, "partn": 3},
            ]
            parted_print = (
                "BYT;\n"
                "/dev/nvme0n1:240000MiB:nvme:512:512:gpt:Win11:;\n"
                "1:1MiB:101MiB:100MiB:fat32:EFI:boot, esp;\n"
                "2:101MiB:225381MiB:225280MiB:ntfs:Windows:;\n"
                "3:225381MiB:226031MiB:650MiB:ntfs:WinRE:hidden, diag;\n"
            )
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts,
                                  parted_print=parted_print, esp_size_bytes=100 * 1024**2)
            res = h.run([
                "--disk", h.disk, "--mode", "dualboot-resize",
                "--resize-part", os.path.join(h.dev_dir, "nvme0n1p2"),
                "--resize-mb", "150000",
                "--esp-part", os.path.join(h.dev_dir, "nvme0n1p1"),
                "--esp-action", "share-boot-efi",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            calls = h.calls()
            self.assertIn("ntfsresize --info", calls)
            self.assertIn("resizepart 2 150105MiB", calls)
            # CRITICAL: New FinnOS partition MUST end at 225381MiB (NOT 100%), preserving WinRE!
            self.assertIn("mkpart FinnOS ext4 150105MiB 225381MiB", calls)
            self.assertNotIn("100%", calls)
            # Small 100 MiB Windows ESP MUST be mounted at /boot/efi, NEVER /boot!
            self.assertIn(f"mount {os.path.join(h.dev_dir, 'nvme0n1p1')} {h.mnt_dir}/boot/efi", calls)
            self.assertNotIn("NETWORK_CALL_FORBIDDEN", calls)

    def test_dualboot_windows10_mbr_3_primaries_bios(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "ntfs", "size": 100 * 1024**2, "partn": 1},
                {"name": "nvme0n1p2", "fstype": "ntfs", "size": 220 * 1024**3, "partn": 2},
                {"name": "nvme0n1p3", "fstype": "ntfs", "size": 500 * 1024**2, "partn": 3},
            ]
            parted_print = (
                "BYT;\n"
                "/dev/nvme0n1:240000MiB:sata:512:512:msdos:Win10:;\n"
                "1:1MiB:101MiB:100MiB:ntfs::boot;\n"
                "2:101MiB:225381MiB:225280MiB:ntfs::;\n"
                "3:225381MiB:225881MiB:500MiB:ntfs::diag;\n"
            )
            h = _InstallerHarness(tmp, pttype="dos", existing_parts=parts, parted_print=parted_print)
            res = h.run([
                "--disk", h.disk, "--mode", "dualboot-resize",
                "--resize-part", os.path.join(h.dev_dir, "nvme0n1p2"),
                "--resize-mb", "150000",
                "--esp-action", "none",
                "--offline-only", "--firmware", "bios", "--yes",
            ], uefi=False)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            calls = h.calls()
            # With 3 primary partitions on MBR, installer creates extended + logical cleanly!
            self.assertIn("mkpart extended 150105MiB 225381MiB", calls)
            self.assertIn("mkpart logical ext4 150106MiB 225381MiB", calls)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)

    def test_dualboot_free_space_macos_apfs_auto_creates_esp(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "apfs", "size": 200 * 1024**3, "partn": 1},
            ]
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts)
            res = h.run([
                "--disk", h.disk, "--mode", "dualboot-free",
                "--free-start-mb", "205000", "--free-end-mb", "256000",
                "--esp-action", "create-esp",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            calls = h.calls()
            self.assertIn("mkpart FINNOS_EFI fat32 205000MiB 205512MiB", calls)
            self.assertIn("mkfs.vfat -F32 -n FINNOS_EFI", calls)
            self.assertIn("mkpart FinnOS ext4 205512MiB 256000MiB", calls)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)

    def test_target_partition_uefi_no_esp_auto_splits_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "ext4", "size": 60 * 1024**3, "partn": 1},
            ]
            parted_print = (
                "BYT;\n"
                "/dev/nvme0n1:61440MiB:nvme:512:512:gpt:Disk:;\n"
                "1:1MiB:61439MiB:61438MiB:ext4:Data:;\n"
            )
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts, parted_print=parted_print)
            res = h.run([
                "--disk", h.disk, "--mode", "partition",
                "--root-part", os.path.join(h.dev_dir, "nvme0n1p1"),
                "--esp-action", "split-esp",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            calls = h.calls()
            self.assertIn("parted -s", calls)
            self.assertIn("mkpart FINNOS_EFI fat32 1MiB 513MiB", calls)
            self.assertIn("mkpart FinnOS ext4 513MiB 61439MiB", calls)
            self.assertIn("mkfs.vfat -F32 -n FINNOS_EFI", calls)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)

    def test_target_partition_bios_gpt_no_bios_boot_auto_carves(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "ext4", "size": 60 * 1024**3, "partn": 1},
            ]
            parted_print = (
                "BYT;\n"
                "/dev/nvme0n1:61440MiB:nvme:512:512:gpt:Disk:;\n"
                "1:1MiB:61439MiB:61438MiB:ext4:Data:;\n"
            )
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts, parted_print=parted_print)
            res = h.run([
                "--disk", h.disk, "--mode", "partition",
                "--root-part", os.path.join(h.dev_dir, "nvme0n1p1"),
                "--esp-action", "none", "--need-bios-boot",
                "--offline-only", "--firmware", "bios", "--yes",
            ], uefi=False)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            calls = h.calls()
            self.assertIn("mkpart BIOS_BOOT ext2 1MiB 3MiB", calls)
            self.assertIn("bios_grub on", calls)
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)

    def test_reinstall_preserves_home_accounts_hostname_and_oobe(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "vfat", "size": 512 * 1024**2, "partn": 1},
                {"name": "nvme0n1p2", "fstype": "ext4", "size": 60 * 1024**3, "partn": 2},
            ]
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts)
            # Pre-populate existing user data on target /mnt.
            os.makedirs(os.path.join(h.mnt_dir, "home", "ada"), exist_ok=True)
            os.makedirs(os.path.join(h.mnt_dir, "etc"), exist_ok=True)
            os.makedirs(os.path.join(h.mnt_dir, "var", "lib", "finn"), exist_ok=True)
            with open(os.path.join(h.mnt_dir, "home", "ada", "notes.txt"), "w", encoding="utf-8") as f:
                f.write("keep-my-data\n")
            with open(os.path.join(h.mnt_dir, "etc", "hostname"), "w", encoding="utf-8") as f:
                f.write("ada-workstation\n")
            with open(os.path.join(h.mnt_dir, "var", "lib", "finn", "oobe_complete"), "w", encoding="utf-8") as f:
                f.write("done\n")

            res = h.run([
                "--disk", h.disk, "--mode", "reinstall",
                "--root-part", os.path.join(h.dev_dir, "nvme0n1p2"),
                "--esp-part", os.path.join(h.dev_dir, "nvme0n1p1"),
                "--esp-action", "share-boot",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertNotIn("mkfs.ext4", h.calls())
            with open(os.path.join(h.mnt_dir, "home", "ada", "notes.txt"), "r", encoding="utf-8") as f:
                self.assertEqual(f.read().strip(), "keep-my-data")
            with open(os.path.join(h.mnt_dir, "etc", "hostname"), "r", encoding="utf-8") as f:
                self.assertEqual(f.read().strip(), "ada-workstation")
            self.assertTrue(os.path.isfile(os.path.join(h.mnt_dir, "var", "lib", "finn", "oobe_complete")))

    def test_bootloader_repair_auto_formats_broken_esp(self):
        with tempfile.TemporaryDirectory() as tmp:
            parts = [
                {"name": "nvme0n1p1", "fstype": "", "size": 512 * 1024**2, "partn": 1},
                {"name": "nvme0n1p2", "fstype": "ext4", "size": 60 * 1024**3, "partn": 2},
            ]
            h = _InstallerHarness(tmp, pttype="gpt", existing_parts=parts)
            os.makedirs(os.path.join(h.mnt_dir, "root"), exist_ok=True)
            res = h.run([
                "--disk", h.disk, "--mode", "repair-boot",
                "--root-part", os.path.join(h.dev_dir, "nvme0n1p2"),
                "--esp-part", os.path.join(h.dev_dir, "nvme0n1p1"),
                "--esp-action", "format-esp",
                "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("mkfs.vfat -F32 -n FINNOS_EFI", h.calls())
            self.assertIn("FINN_BOOT_REPAIR_COMPLETE", res.stdout)

    def test_tampered_offline_image_sha512_rejected_before_touching_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt", tamper_sfs=True)
            res = h.run([
                "--disk", h.disk, "--mode", "clean",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("SHA-512 integrity check FAILED", res.stdout + res.stderr)
            self.assertNotIn("sfdisk", h.calls())
            self.assertNotIn("wipefs", h.calls())

    def test_offline_sfs_direct_extraction_works_without_unpacked_airootfs(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt", use_sfs_only=True)
            res = h.run([
                "--disk", h.disk, "--mode", "clean",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("unsquashfs", h.calls())
            self.assertIn("FINN_INSTALL_COMPLETE", res.stdout)

    def test_offline_only_refuses_when_no_local_image_without_touching_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt", create_live_image=False)
            res = h.run([
                "--disk", h.disk, "--mode", "clean",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("refusing internet download", res.stdout + res.stderr)
            self.assertNotIn("NETWORK_CALL_FORBIDDEN", h.calls())

    def test_live_usb_backing_disk_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            h = _InstallerHarness(tmp, pttype="gpt")
            # Override live_disk to match h.disk by pointing /run/archiso/bootmnt at h.disk!
            h2 = _InstallerHarness(tmp, pttype="gpt", live_disk=h.disk)
            res = h2.run([
                "--disk", h2.disk, "--mode", "clean",
                "--offline-only", "--firmware", "uefi", "--yes",
            ])
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("refusing to modify the active live/recovery medium", res.stdout + res.stderr)
            self.assertNotIn("sfdisk", h2.calls())

    def test_recovery_service_and_compositor_cmdline_guard(self):
        with open(os.path.join(ROOT, "base", "finn-comp.service"), encoding="utf-8") as f:
            comp_a = f.read()
        with open(os.path.join(ROOT, "base", "airootfs", "etc", "systemd", "system", "finn-comp.service"), encoding="utf-8") as f:
            comp_b = f.read()
        with open(os.path.join(ROOT, "base", "finn-recovery.service"), encoding="utf-8") as f:
            rec_a = f.read()
        with open(os.path.join(ROOT, "base", "airootfs", "etc", "systemd", "system", "finn-recovery.service"), encoding="utf-8") as f:
            rec_b = f.read()
        self.assertEqual(comp_a, comp_b)
        self.assertEqual(rec_a, rec_b)
        self.assertIn("ConditionKernelCommandLine=!finn.recovery", comp_a)
        self.assertIn("ConditionKernelCommandLine=finn.recovery", rec_a)
        self.assertIn("/usr/local/bin/recovery-mode", rec_a)

    def test_chroot_offline_dualboot_and_reinstall_contracts(self):
        chroot = os.path.join(ROOT, "vm", "finn-chroot.sh")
        chk = subprocess.run(["bash", "-n", chroot], capture_output=True, text=True, check=False)
        self.assertEqual(chk.returncode, 0, chk.stderr)
        with open(chroot, encoding="utf-8") as f:
            text = f.read()
        self.assertIn("FINN_OFFLINE", text)
        self.assertIn("FINN_REINSTALL", text)
        self.assertIn("FINN_REPAIR_ONLY", text)
        self.assertIn("FINN_ESP_MOUNT", text)
        self.assertIn("GRUB_DISABLE_OS_PROBER=false", text)
        self.assertIn("40_finnos_dualboot", text)
        self.assertIn("Windows Boot Manager", text)
        self.assertIn("FinnOS Recovery", text)
        self.assertIn("finn.recovery", text)
        self.assertIn("finn-recovery.service", text)
        self.assertIn("reinstall: preserving existing user accounts", text)
        self.assertIn("SHELL=prebuilt-installed", text)


if __name__ == "__main__":
    unittest.main()
