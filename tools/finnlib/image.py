"""Boot image staging and platform-specific image creation."""
from __future__ import annotations

import shutil
import subprocess
import struct
import sys
import tempfile
import uuid
import zlib
from pathlib import Path
from typing import Optional

from .toolchain import find_tool

def stage_esp(
    root: Path,
    boot_manager: Path,
    kernel: Path,
    boot_filename: str = "BOOTX64.EFI",
    kernel_filename: str = "KERNEL.ELF",
) -> Path:
    esp = root / "esp"
    if esp.exists():
        shutil.rmtree(esp)
    boot_path = esp / "EFI" / "BOOT"
    kernel_path = esp / "EFI" / "FINNOS"
    boot_path.mkdir(parents=True, exist_ok=True)
    kernel_path.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(boot_manager, boot_path / boot_filename)
    shutil.copyfile(kernel, kernel_path / kernel_filename)
    return esp

def make_data_image(output: Path, size_mb: int = 64) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.stat().st_size == 0:
        with output.open("wb") as stream:
            stream.seek(size_mb * 1024 * 1024 - 1)
            stream.write(b"\x00")
    return output

def wrap_in_gpt(
    esp_raw: Path,
    output: Path,
    root_size_mb: int = 64,
    root_image: Optional[Path] = None,
) -> Path:
    esp_bytes = esp_raw.read_bytes()
    esp_sectors = (len(esp_bytes) + 511) // 512
    if len(esp_bytes) % 512 != 0:
        esp_bytes += b"\x00" * (esp_sectors * 512 - len(esp_bytes))

    align_sectors = 2048
    esp_start_lba = align_sectors
    esp_end_lba = esp_start_lba + esp_sectors - 1

    if root_image and root_image.is_file():
        root_bytes = root_image.read_bytes()
        root_sectors = max((len(root_bytes) + 511) // 512, root_size_mb * 1024 * 1024 // 512)
        if len(root_bytes) < root_sectors * 512:
            root_bytes += b"\x00" * (root_sectors * 512 - len(root_bytes))
    else:
        root_sectors = root_size_mb * 1024 * 1024 // 512
        root_bytes = b"\x00" * (root_sectors * 512)

    root_start_lba = esp_end_lba + 1
    root_end_lba = root_start_lba + root_sectors - 1

    total_sectors = root_end_lba + 1 + 32 + 1

    mbr = bytearray(512)
    mbr[446:446 + 16] = struct.pack(
        "<B3sB3sII",
        0x00,
        b"\x00\x02\x00",
        0xEE,
        b"\xFF\xFF\xFF",
        1,
        min(0xFFFFFFFF, total_sectors - 1),
    )
    mbr[510:512] = b"\x55\xAA"

    entries = bytearray(128 * 128)

    def make_entry(type_guid: uuid.UUID, part_guid: uuid.UUID, start_lba: int, end_lba: int, name: str) -> bytearray:
        entry = bytearray(128)
        entry[0:16] = type_guid.bytes_le
        entry[16:32] = part_guid.bytes_le
        entry[32:40] = struct.pack("<Q", start_lba)
        entry[40:48] = struct.pack("<Q", end_lba)
        entry[48:56] = struct.pack("<Q", 0)
        name_encoded = name.encode("utf-16le")[:72]
        entry[56:56 + len(name_encoded)] = name_encoded
        return entry

    esp_type_guid = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
    root_type_guid = uuid.UUID("46494e4e-4f53-524f-4f54-000000000001")
    disk_guid = uuid.uuid5(uuid.NAMESPACE_DNS, "finnos.disk.guid")
    esp_part_guid = uuid.uuid5(uuid.NAMESPACE_DNS, "finnos.part.esp")
    root_part_guid = uuid.uuid5(uuid.NAMESPACE_DNS, "finnos.part.root")

    entries[0:128] = make_entry(esp_type_guid, esp_part_guid, esp_start_lba, esp_end_lba, "EFI System Partition")
    entries[128:256] = make_entry(root_type_guid, root_part_guid, root_start_lba, root_end_lba, "FinnOS Root")

    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF

    def make_header(my_lba: int, alt_lba: int, entry_lba: int) -> bytes:
        hdr = bytearray(92)
        hdr[0:8] = b"EFI PART"
        hdr[8:12] = struct.pack("<I", 0x00010000)
        hdr[12:16] = struct.pack("<I", 92)
        hdr[16:20] = struct.pack("<I", 0)
        hdr[20:24] = struct.pack("<I", 0)
        hdr[24:32] = struct.pack("<Q", my_lba)
        hdr[32:40] = struct.pack("<Q", alt_lba)
        hdr[40:48] = struct.pack("<Q", align_sectors)
        hdr[48:56] = struct.pack("<Q", root_end_lba)
        hdr[56:72] = disk_guid.bytes_le
        hdr[72:80] = struct.pack("<Q", entry_lba)
        hdr[80:84] = struct.pack("<I", 128)
        hdr[84:88] = struct.pack("<I", 128)
        hdr[88:92] = struct.pack("<I", entries_crc)
        crc = zlib.crc32(hdr) & 0xFFFFFFFF
        hdr[16:20] = struct.pack("<I", crc)
        return bytes(hdr.ljust(512, b"\x00"))

    pri_hdr = make_header(1, total_sectors - 1, 2)
    bak_hdr = make_header(total_sectors - 1, 1, total_sectors - 33)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(mbr)
        stream.write(pri_hdr)
        stream.write(entries)
        pad_size = (esp_start_lba - 34) * 512
        if pad_size > 0:
            stream.write(b"\x00" * pad_size)
        stream.write(esp_bytes)
        stream.write(root_bytes)
        stream.write(entries)
        stream.write(bak_hdr)
    return output

def make_image(
    esp: Path,
    output: Path,
    root_size_mb: int = 64,
    root_image: Optional[Path] = None,
) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="finnos-esp-raw-") as temporary:
        esp_raw = Path(temporary) / "esp_raw.img"
        if sys.platform == "darwin":
            _make_image_darwin(esp, esp_raw)
        else:
            _make_image_linux(esp, esp_raw)
        return wrap_in_gpt(esp_raw, output, root_size_mb=root_size_mb, root_image=root_image)

def _make_image_linux(esp: Path, output: Path) -> Path:
    mkfs = shutil.which("mkfs.vfat")
    mcopy = shutil.which("mcopy")
    mmd = shutil.which("mmd")
    if not mkfs or not mcopy or not mmd:
        raise RuntimeError("real FAT image creation requires mkfs.vfat, mcopy, and mmd (dosfstools, mtools)")
    with tempfile.TemporaryDirectory(prefix="finnos-image-") as temporary:
        image = Path(temporary) / "esp.img"
        # Create a 64 MB FAT32 image.
        subprocess.run(["dd", "if=/dev/zero", f"of={image}", "bs=1M", "count=64"], check=True, capture_output=True)
        subprocess.run([mkfs, "-F", "32", str(image)], check=True, capture_output=True)
        # Copy the ESP tree into the image using mtools. Create directories first,
        # then copy files.
        directories: set[Path] = set()
        for src in esp.rglob("*"):
            if src.is_file():
                directory = src.relative_to(esp).parent
                while directory != Path("."):
                    directories.add(directory)
                    directory = directory.parent
        for directory in sorted(directories, key=lambda p: p.parts):
            path = directory.as_posix()
            subprocess.run([mmd, "-i", str(image), f"::{path}"], check=True, capture_output=True)
        for src in esp.rglob("*"):
            if src.is_file():
                rel = src.relative_to(esp).as_posix()
                subprocess.run([mcopy, "-i", str(image), str(src), f"::{rel}"], check=True, capture_output=True)
        shutil.copyfile(image, output)
    return output

def _make_image_darwin(esp: Path, output: Path) -> Path:
    hdiutil = shutil.which("hdiutil")
    qemu_img = find_tool("qemu-img")
    if not hdiutil or not qemu_img:
        raise RuntimeError("real FAT image creation requires hdiutil and qemu-img")
    with tempfile.TemporaryDirectory(prefix="finnos-image-") as temporary:
        dmg = Path(temporary) / "esp.dmg"
        mount = Path(temporary) / "mount"
        source = Path(temporary) / "source"
        mount.mkdir()
        source.mkdir()
        subprocess.run([hdiutil, "create", "-format", "UDRW", "-size", "64m", "-fs", "MS-DOS FAT32", "-layout", "NONE", "-srcfolder", str(source), str(dmg)], check=True, capture_output=True)
        attached = False
        try:
            subprocess.run([hdiutil, "attach", "-nobrowse", "-mountpoint", str(mount), str(dmg)], check=True, capture_output=True)
            attached = True
            shutil.copytree(esp, mount, dirs_exist_ok=True)
            subprocess.run([hdiutil, "detach", str(mount)], check=True, capture_output=True)
            attached = False
            # hdiutil's UDRW output is a raw FAT volume despite its .dmg suffix.
            subprocess.run([qemu_img, "convert", "-f", "raw", "-O", "raw", str(dmg), str(output)], check=True)
        finally:
            if attached: subprocess.run([hdiutil, "detach", str(mount)], check=False, capture_output=True)
    return output
