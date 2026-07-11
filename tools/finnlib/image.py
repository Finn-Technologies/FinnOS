"""Boot image staging and platform-specific image creation."""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from .toolchain import find_tool

def stage_esp(root: Path, boot_manager: Path, kernel: Path) -> Path:
    esp = root / "esp"
    boot_path = esp / "EFI" / "BOOT"
    kernel_path = esp / "EFI" / "FINNOS"
    boot_path.mkdir(parents=True, exist_ok=True)
    kernel_path.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(boot_manager, boot_path / "BOOTX64.EFI")
    shutil.copyfile(kernel, kernel_path / "KERNEL.ELF")
    return esp

def make_image(esp: Path, output: Path) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == "darwin":
        return _make_image_darwin(esp, output)
    return _make_image_linux(esp, output)

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
