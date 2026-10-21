#!/bin/bash
# Build the bootable FinnOS install ISO.
#
#   ARCH_ISO=/path/to/archlinux-x86_64.iso vm/build-iso.sh [out.iso]
#
# The Arch Linux ISO is the base: it carries the kernel, the initramfs and
# the live environment the installer runs in. This script puts the FinnOS
# tree into that live root at /opt/finnos, then writes the disc back with
# the same boot images and the same label, so the result boots under UEFI
# (which is what VMware uses) and under BIOS.
#
# The base image is not stored in the tree: download it from
# https://archlinux.org/download/ and pass it in ARCH_ISO, or put it at
# .cache/archlinux-x86_64.iso.
#
# This is the remaster path, and it is the one that runs where there is no
# Arch machine to build on — a GitHub runner, for instance. It produces
# the Arch live environment with FinnOS inside it. base/mkfinniso is the
# other path: it builds a FinnOS medium from base/ alone, with mkarchiso,
# on an Arch machine. See base/README.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/finnos.iso}"
BASE="${ARCH_ISO:-$ROOT/.cache/archlinux-x86_64.iso}"

die() { echo "error: $*" >&2; exit 1; }

for tool in xorriso unsquashfs mksquashfs sha512sum tar; do
  command -v "$tool" >/dev/null 2>&1 || die "need $tool (xorriso, squashfs-tools)"
done

[ -f "$BASE" ] || die "base image not found: $BASE
  download https://archlinux.org/download/ and set ARCH_ISO"
[ "$(cd "$(dirname "$BASE")" && pwd)/$(basename "$BASE")" != \
  "$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")" ] || \
  die "the output would overwrite the base image"

WORK="$(mktemp -d)"
trap 'chmod -R u+rwX "$WORK" 2>/dev/null || true; rm -rf "$WORK" 2>/dev/null || true' EXIT

# The live root has file capabilities on ping and newuidmap. Only root may
# write those xattrs; without them the tools in the live environment simply
# have no capabilities, which is acceptable but worth saying out loud.
XATTR=(-no-xattrs)
if [ "$(id -u)" = 0 ]; then
  XATTR=()
else
  echo "==> not root: file capabilities are dropped from the live root"
fi

echo "==> staging the payload"
mkdir -p "$WORK/payload"
tar -C "$ROOT" -cf - \
    --exclude='.git' \
    --exclude='.cache' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.iso' \
    --exclude='src/compositor/finn-comp' \
    --exclude='src/control/finnd' \
    --exclude='src/control/finnctl' \
    --exclude='src/tools/vgtest' \
    --exclude='src/tools/dockprev' \
    . | tar -C "$WORK/payload" -xf -

echo "==> finding the live root on $BASE"
SFS=""
for p in /arch/x86_64/airootfs.sfs /arch/boot/x86_64/airootfs.sfs /arch/airootfs.sfs; do
  if xorriso -osirrox on -indev "$BASE" -extract "$p" "$WORK/airootfs.sfs" 2>/dev/null; then
    SFS="$p"
    break
  fi
done
[ -n "$SFS" ] || die "no airootfs.sfs on the base image; is it an Arch Linux ISO?"

echo "==> unpacking $SFS"
unsquashfs -q "${XATTR[@]}" -d "$WORK/root" "$WORK/airootfs.sfs" >/dev/null

echo "==> adding FinnOS to the live root"
mkdir -p "$WORK/root/opt/finnos"
cp -a "$WORK/payload/." "$WORK/root/opt/finnos/"

# A checkout on a machine that does not keep the executable bit (Windows)
# leaves the scripts flat, and the installer then cannot be run at all. A
# file that starts with a shebang is meant to be run, so it is marked as
# one here rather than left to the mode the checkout happened to produce.
while IFS= read -r -d '' f; do
  if [ "$(head -c 2 "$f" 2>/dev/null)" = "#!" ]; then
    chmod 755 "$f"
  fi
done < <(find "$WORK/root/opt/finnos" -type f -print0)
ls -l "$WORK/root/opt/finnos/vm/finn-install.sh" \
      "$WORK/root/opt/finnos/vm/finn-chroot.sh" 2>/dev/null | sed 's/^/    /' || true

# What the live root says and does is base/airootfs's: the installer
# wrapper, the branding, the units and the login are the files the native
# build puts on the disc, so the remastered medium and the one mkfinniso
# builds behave the same way by construction rather than by being written
# twice. The two units are installed and not enabled: the live root has no
# compositor on it, and the installer builds one on the machine it
# installs.
AIROOTFS="$ROOT/base/airootfs"
install -Dm755 "$AIROOTFS/usr/local/bin/finn-install" \
    "$WORK/root/usr/local/bin/finn-install"
install -Dm644 "$AIROOTFS/etc/issue" "$WORK/root/etc/issue"
install -Dm644 "$AIROOTFS/etc/motd" "$WORK/root/etc/motd"
install -Dm644 "$AIROOTFS/etc/os-release" "$WORK/root/etc/os-release"
install -Dm644 "$AIROOTFS/etc/hostname" "$WORK/root/etc/hostname"
install -Dm644 "$AIROOTFS/etc/systemd/system/finn-comp.service" \
    "$WORK/root/etc/systemd/system/finn-comp.service"
install -Dm644 "$AIROOTFS/etc/systemd/system/finnd.service" \
    "$WORK/root/etc/systemd/system/finnd.service"
install -Dm644 "$AIROOTFS/usr/lib/tmpfiles.d/finn.conf" \
    "$WORK/root/usr/lib/tmpfiles.d/finn.conf"
install -Dm644 "$AIROOTFS/etc/systemd/system/getty@tty1.service.d/autologin.conf" \
    "$WORK/root/etc/systemd/system/getty@tty1.service.d/autologin.conf"

# The default boot entry passes finn.install on the kernel command line.
# When it is there, the live shell hands over to the installer once it is
# up, after a moment to back out.
#
# The live environment's root shell is zsh, not bash, so the hook goes into
# the file that shell reads when it logs in; .bash_profile is written as
# well, for a live environment that is started with bash. It is appended
# rather than installed: this live root is Arch's and its own profile may
# have something in it that the live environment needs.
for profile in .zprofile .bash_profile; do
  PROFILE="$WORK/root/root/$profile"
  touch "$PROFILE"
  cat "$AIROOTFS/root/$profile" >> "$PROFILE"
  chmod 644 "$PROFILE"
done

echo "==> adding the FinnOS boot entries"
# BIOS boots through syslinux and UEFI through systemd-boot, and both menus
# name the distribution they came from. It is an Arch live environment
# underneath, but this disc installs FinnOS, so the menus have to say so:
# the shipped fragments are relabelled, and an entry that hands over to
# the installer is made the default. Kernel, initrd and the archiso
# parameters are read out of the shipped entry rather than guessed,
# because they carry the release's kernel path and volume label. The
# layout has moved between releases, so the files are looked for in a few
# known places instead of one.
#
# The top-level syslinux.cfg is only a dispatcher (whichsys.c32 picks pxe,
# sys or iso); the entries people see are in a file it includes. Take the
# whole directory and use the config that actually has a LINUX line.
BIOS_DIR=""
for p in /boot/syslinux /syslinux /isolinux; do
  rm -rf "$WORK/syslinux"
  if xorriso -osirrox on -indev "$BASE" -extract "$p" "$WORK/syslinux" 2>/dev/null; then
    BIOS_DIR="$p"
    break
  fi
done

SYSCFG=""
if [ -n "$BIOS_DIR" ]; then
  echo "    BIOS menu files in $BIOS_DIR:"
  ls -1 "$WORK/syslinux" 2>/dev/null | sed 's/^/      /' || true
  # The stock fragments name the distribution in their titles, labels and
  # help text; all of it is presentation, none of it is a path or a
  # parameter, so one replacement covers it. The background image is the
  # other distribution's mark, so it goes rather than being painted over.
  sed -i 's/Arch Linux/FinnOS/g' "$WORK"/syslinux/*.cfg
  sed -i '/^[[:space:]]*MENU BACKGROUND/d' "$WORK"/syslinux/*.cfg
  # Several configs carry LINUX lines. The PXE and NBD ones boot from a
  # server; the disc's own entry is the one with archisobasedir and no
  # server parameter.
  for f in "$WORK"/syslinux/*.cfg; do
    [ -f "$f" ] || continue
    grep -qE '^[[:space:]]*LINUX[[:space:]]' "$f" || continue
    grep -q 'archisobasedir=arch' "$f" || continue
    if grep -qE 'archiso_(nbd|nfs)_srv|archiso_pxe' "$f"; then continue; fi
    SYSCFG="$f"
    break
  done
fi

LOADER_DIR=""
for p in /loader /boot/loader /EFI/loader; do
  rm -rf "$WORK/loader"
  if xorriso -osirrox on -indev "$BASE" -extract "$p" "$WORK/loader" 2>/dev/null; then
    LOADER_DIR="$p"
    break
  fi
done

MAPS=()
if [ -n "$SYSCFG" ]; then
  echo "    BIOS menu: $SYSCFG"
  sed 's/^/      | /' "$SYSCFG" | head -20 || true
  LINUX_LINE="$(sed -n 's/^[[:space:]]*LINUX[[:space:]]*//p' "$SYSCFG" | head -1)"
  INITRD_LINE="$(sed -n 's/^[[:space:]]*INITRD[[:space:]]*//p' "$SYSCFG" | head -1)"
  APPEND_LINE="$(sed -n 's/^[[:space:]]*APPEND[[:space:]]*//p' "$SYSCFG" | head -1)"
  if [ -n "$LINUX_LINE" ] && [ -n "$APPEND_LINE" ]; then
    # The stock file has a plain live entry and a speech entry. The plain
    # one is what "FinnOS live shell" is, so it goes; the speech one stays,
    # because a screen reader is still a way in.
    STOCK="$WORK/stock.cfg"
    awk '
      /^[[:space:]]*LABEL[[:space:]]/ {
        if (block != "") { if (speech) printf "%s", block }
        block = $0 "\n"; speech = 0; next
      }
      {
        if (block == "") { print; next }
        block = block $0 "\n"
        if ($0 ~ /accessibility=on/) speech = 1
      }
      END { if (block != "" && speech) printf "%s", block }
    ' "$SYSCFG" > "$STOCK"
    sed -i -E 's/^([[:space:]]*MENU LABEL[[:space:]]+)FinnOS install medium \([^)]*\) with \^speech$/\1FinnOS live shell with ^speech/' "$STOCK"
    sed -i 's/Boot the FinnOS install medium/Boot the FinnOS live shell/' "$STOCK"
    {
      echo "DEFAULT finnos"
      echo "PROMPT 0"
      echo "TIMEOUT 100"
      echo "LABEL finnos"
      echo "  MENU LABEL Install FinnOS"
      echo "  LINUX $LINUX_LINE"
      if [ -n "$INITRD_LINE" ]; then echo "  INITRD $INITRD_LINE"; fi
      echo "  APPEND $APPEND_LINE finn.install"
      echo
      echo "LABEL finnos-shell"
      echo "  MENU LABEL FinnOS live shell"
      echo "  LINUX $LINUX_LINE"
      if [ -n "$INITRD_LINE" ]; then echo "  INITRD $INITRD_LINE"; fi
      echo "  APPEND $APPEND_LINE"
      cat "$STOCK"
    } > "$WORK/syslinux.cfg.new"
    mv "$WORK/syslinux.cfg.new" "$SYSCFG"
  else
    echo "warning: could not read the kernel and append lines from $SYSCFG" >&2
  fi
else
  echo "warning: no syslinux menu found; the BIOS menu stays Arch's" >&2
fi

# Whatever the fragment split is, the rewritten files are the ones to put
# back; the binaries next to them are left exactly as the base ships them.
if [ -n "$BIOS_DIR" ]; then
  for f in "$WORK"/syslinux/*.cfg; do
    [ -f "$f" ] || continue
    MAPS+=(-map "$f" "$BIOS_DIR/$(basename "$f")")
  done
fi

if [ -n "$LOADER_DIR" ] && [ -d "$WORK/loader/entries" ]; then
  echo "    UEFI entries in $LOADER_DIR/entries:"
  ls -1 "$WORK/loader/entries" | sed 's/^/      /' || true
  sed -i 's/Arch Linux/FinnOS/g' "$WORK"/loader/entries/*.conf
  # The stock entries boot the live environment; only the new one installs.
  sed -i -E 's/^(title[[:space:]]+)FinnOS install medium(.*)$/\1FinnOS live medium\2/' "$WORK"/loader/entries/*.conf
  ENTRY="$(ls "$WORK/loader/entries"/*.conf 2>/dev/null | head -1 || true)"
  if [ -n "$ENTRY" ]; then
    # A numeric prefix sorts it to the top of the menu; the default is
    # named explicitly, but the order is what the eye reads.
    cp "$ENTRY" "$WORK/loader/entries/00-finnos.conf"
    sed -i -E 's/^title.*/title   Install FinnOS/' "$WORK/loader/entries/00-finnos.conf"
    sed -i -E '/^options/ s/$/ finn.install/' "$WORK/loader/entries/00-finnos.conf"
    printf 'default 00-finnos.conf\ntimeout 10\n' > "$WORK/loader/loader.conf"
    MAPS+=(-map "$WORK/loader/loader.conf" "$LOADER_DIR/loader.conf")
    for f in "$WORK/loader/entries"/*.conf; do
      MAPS+=(-map "$f" "$LOADER_DIR/entries/$(basename "$f")")
    done
  fi
else
  echo "warning: no systemd-boot entries found; the UEFI menu stays Arch's" >&2
fi

echo "==> repacking the live root"
mksquashfs "$WORK/root" "$WORK/airootfs.sfs" -noappend -comp zstd -all-root \
    "${XATTR[@]}" -no-progress >/dev/null
(cd "$WORK" && sha512sum airootfs.sfs > airootfs.sha512)

echo "==> writing $OUT"
rm -f "$OUT"
SFS_DIR="${SFS%/*}"
xorriso -indev "$BASE" -outdev "$OUT" \
    -map "$WORK/airootfs.sfs" "$SFS" \
    -map "$WORK/airootfs.sha512" "$SFS_DIR/airootfs.sha512" \
    ${MAPS[@]+"${MAPS[@]}"} \
    -boot_image any replay -commit

echo
echo "==> boot images on the result"
xorriso -indev "$OUT" -report_el_torito plain 2>/dev/null | sed 's/^/    /'
echo
echo "ISO written to $OUT"
echo "Boot it in VMware, or test it here: vm/test-boot.sh $OUT"
