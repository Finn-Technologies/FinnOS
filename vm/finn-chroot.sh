#!/bin/bash
# FinnOS chroot config — runs inside /mnt via arch-chroot.
#
# Configures the installed system cleanly offline:
#   - Preserves user accounts, passwords, hostname, timezone, and OOBE completion
#     when FINN_REINSTALL=1.
#   - Skips rebuilding FinnOS from source when pre-built binaries already exist
#     in the offline OS image (rebuilds offline from /opt/finnos otherwise).
#   - Installs UEFI (finn-boot/systemd-boot when ESP is mounted at /boot on GPT,
#     or GRUB UEFI when ESP is mounted at /boot/efi for 100 MiB Windows ESPs /
#     MBR UEFI) and/or Legacy BIOS GRUB (i386-pc on MBR or GPT+BIOS-Boot).
#   - Enables os-prober AND writes deterministic /etc/grub.d/40_custom dual-boot
#     entries for detected Windows, macOS, and Linux installations so dual-boot
#     with ANY OS works reliably out of the box.
set -euo pipefail
HOST="${FINN_HOST_URL:-}"
SRC=/root/finnos-src
[ -d "$SRC" ] || SRC=/opt/finnos
OFFLINE="${FINN_OFFLINE:-0}"
REINSTALL="${FINN_REINSTALL:-0}"
REPAIR_ONLY="${FINN_REPAIR_ONLY:-0}"

post() {
    [ "$OFFLINE" = 0 ] && [ -n "$HOST" ] || return 0
    curl -s -X POST --data-binary @/tmp/finn-chroot.log "$HOST/log" -o /dev/null 2>/dev/null || true
}

STEP="start"
PW=""
CREDS_SHOWN=0

credentials() {
    [ -n "$PW" ] || return 0
    [ "$CREDS_SHOWN" = 0 ] || return 0
    CREDS_SHOWN=1
    if [ -n "${FINN_PASSWORD:-}" ]; then
        echo "  operator-supplied credentials are omitted from this log."
        return 0
    fi
    cat <<EOF

  Credentials, shown once:
    user  finn   password  $PW
    root  locked (use sudo as finn)
  Change finn's password with: passwd
  sshd is installed and not enabled.
EOF
}

failed() {
    local rc=$?
    [ "$rc" -eq 0 ] && return 0
    echo "FINN_CHROOT_FAILED: \"$STEP\" exited $rc" >&2
    credentials >&2
}
trap failed EXIT

step() {
    STEP="$1"
    printf '\n=== %s ===\n' "$1"
    post
}

# get <destination> <path-in-the-tree>
get() {
    if [ -f "$SRC/$2" ]; then
        install -D -m 644 "$SRC/$2" "$1"
    elif [ -f "/opt/finnos/$2" ]; then
        install -D -m 644 "/opt/finnos/$2" "$1"
    elif [ "$OFFLINE" = 0 ] && [ -n "$HOST" ]; then
        curl -fsSLo "$1" "$HOST/$2"
    else
        echo "error: missing local payload file $2 (offline mode)" >&2
        exit 1
    fi
}

: "${ROOTUUID:?the installer must pass ROOTUUID}"
: "${FINN_DISK:?the installer must pass FINN_DISK}"
echo "ROOTUUID=$ROOTUUID"
echo "FINN_DISK=$FINN_DISK"

if [ "$REPAIR_ONLY" != 1 ]; then
    step "base config"
    if [ "$REINSTALL" != 1 ] || [ ! -e /etc/localtime ]; then
        TZ_NAME="${FINN_TIMEZONE:-UTC}"
        if [ ! -f "/usr/share/zoneinfo/$TZ_NAME" ]; then
            echo "WARN: timezone '$TZ_NAME' not found under /usr/share/zoneinfo; falling back to UTC"
            TZ_NAME="UTC"
        fi
        ln -sf "/usr/share/zoneinfo/$TZ_NAME" /etc/localtime
        hwclock --systohc 2>/dev/null || true
    fi
    if [ "$REINSTALL" != 1 ] || [ ! -s /etc/locale.conf ]; then
        echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
        locale-gen 2>/dev/null || true
        echo "LANG=en_US.UTF-8" > /etc/locale.conf
    fi
    if [ "$REINSTALL" != 1 ] || [ ! -s /etc/hostname ]; then
        get /etc/hostname base/airootfs/etc/hostname
        HOST_NAME="${FINN_HOSTNAME:-$(cat /etc/hostname 2>/dev/null || echo finnos)}"
        printf '%s\n' "$HOST_NAME" > /etc/hostname
    else
        HOST_NAME="$(cat /etc/hostname 2>/dev/null || echo finnos)"
    fi
    get /etc/os-release base/airootfs/etc/os-release
    get /etc/issue base/installed/etc/issue
    get /etc/motd base/installed/etc/motd
    get /etc/pacman.conf base/pacman.conf
    get /etc/pacman.d/finnos-mirrorlist repo/finnos-mirrorlist
    CHROOT_ARCH="$(uname -m)"
    if [ -n "${FINN_REPO_URL:-}" ]; then
        awk -v url="$FINN_REPO_URL" '
          /^\[finnos\]/ { in_finnos=1 }
          in_finnos && /^\[/ && !/^\[finnos\]/ { in_finnos=0 }
          in_finnos && /^Server[[:space:]]*=/ { print "Server = " url; next }
          { print }
        ' /etc/pacman.conf > /etc/pacman.conf.new && mv /etc/pacman.conf.new /etc/pacman.conf
    elif [ -f "/var/lib/finn/repo/finnos/os/$CHROOT_ARCH/finnos.db" ]; then
        awk '
          /^\[finnos\]/ { in_finnos=1 }
          in_finnos && /^\[/ && !/^\[finnos\]/ { in_finnos=0 }
          in_finnos && /^Server[[:space:]]*=/ { print "Server = file:///var/lib/finn/repo/$repo/os/$arch"; next }
          { print }
        ' /etc/pacman.conf > /etc/pacman.conf.new && mv /etc/pacman.conf.new /etc/pacman.conf
    elif [ "$OFFLINE" = 1 ] || ! command -v curl >/dev/null 2>&1 ||
         ! curl -fsI --connect-timeout 2 --max-time 4 "https://repo.finnos.org/finnos/os/$CHROOT_ARCH/finnos.db" >/dev/null 2>&1; then
        echo "note: disabling [finnos] placeholder in /etc/pacman.conf (offline install)"
        awk '
          /^\[finnos\]/ { in_finnos=1; print "# [finnos] (offline fallback)"; next }
          in_finnos && /^\[/ { in_finnos=0 }
          in_finnos && /^(SigLevel|Server)/ { print "# " $0; next }
          { print }
        ' /etc/pacman.conf > /etc/pacman.conf.new && mv /etc/pacman.conf.new /etc/pacman.conf
    fi
    if [ "$(uname -m)" = 'x86_64' ]; then
        MULTILIB_CONF="$(mktemp)"
        get "$MULTILIB_CONF" base/pacman.multilib.conf
        cat "$MULTILIB_CONF" >> /etc/pacman.conf
        rm -f "$MULTILIB_CONF"
        grep -Fqx '[multilib]' /etc/pacman.conf ||
            { echo "error: [multilib] was not applied to /etc/pacman.conf" >&2; exit 1; }
    fi
    printf '127.0.0.1 localhost\n::1 localhost\n127.0.1.1 %s.localdomain %s\n' "$HOST_NAME" "$HOST_NAME" > /etc/hosts

    step "accounts"
    mkdir -p /etc/sudoers.d
    if [ "$REINSTALL" = 1 ] && id finn >/dev/null 2>&1; then
        echo "reinstall: preserving existing user accounts and passwords"
    else
        id finn >/dev/null 2>&1 || useradd -m -G wheel,input,video,render,seat -s /bin/bash finn
        PW="${FINN_PASSWORD:-}"
        if [ -z "$PW" ]; then
            PW="$(head -c 12 /dev/urandom | base64 | tr -d '\n')"
        fi
        case "$PW" in
            *:*|*$'\n'*) echo "error: the password may not contain ':' or a newline" >&2; exit 1 ;;
        esac
        printf 'finn:%s\n' "$PW" | chpasswd
        passwd -l root
    fi
    printf 'finn ALL=(ALL:ALL) ALL\n' > /etc/sudoers.d/finn
    chmod 0440 /etc/sudoers.d/finn
    if [ ! -f /var/lib/finn/oobe_complete ]; then
        printf 'finn ALL=(root) NOPASSWD: /usr/local/libexec/finn-oobe-apply\n' > /etc/sudoers.d/finn-oobe
        chmod 0440 /etc/sudoers.d/finn-oobe
    fi

    step "services"
    systemctl enable NetworkManager 2>/dev/null || true
    systemctl enable seatd 2>/dev/null || true
    systemctl enable systemd-timesyncd 2>/dev/null || true
    systemctl enable bluetooth 2>/dev/null ||
        echo "WARN: bluetooth.service is not installed"
    systemctl enable vmtoolsd.service vmware-vmblock-fuse.service 2>/dev/null || true
    systemctl enable vboxservice.service 2>/dev/null || true
    if getent group vboxsf >/dev/null 2>&1; then
        usermod -aG vboxsf finn 2>/dev/null || true
    fi
    mkdir -p /mnt/hgfs
    rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf
    rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true
    systemctl enable getty@tty2.service 2>/dev/null ||
        echo "WARN: getty@tty2 could not be enabled; there is no rescue console"

    step "Fixel Display fonts"
    mkdir -p /usr/share/fonts/OTF
    for f in FixelDisplay-Regular FixelDisplay-Medium FixelDisplay-SemiBold FixelDisplay-Bold; do
        get "/usr/share/fonts/OTF/$f.otf" "assets/fonts/FixelDisplay/$f.otf"
    done
    fc-cache -f >/dev/null 2>&1 || true
    post

    step "the payload: wallpaper, icons, registry"
    mkdir -p /usr/share/finn/wallpaper /usr/share/finn/icons /var/lib/finn /usr/local/bin /etc/ld.so.conf.d
    get /usr/share/finn/wallpaper/default.webp assets/wallpaper/default.webp
    get /usr/share/finn/wallpaper/setup.webp assets/wallpaper/setup.webp
    get /usr/share/finn/apps.registry assets/apps.registry
    printf 'settings\nfiles\nterminal\nnotes\nstore\nviewer\nmonitor\ncalculator\narchive\ncapture\nflourish\n' > /var/lib/finn/installed
    printf '/usr/local/lib\n' > /etc/ld.so.conf.d/finn.conf
    ldconfig 2>/dev/null || true
    if compgen -G "$SRC/assets/icons/shell/*.svg" >/dev/null 2>&1; then
        for f in "$SRC"/assets/icons/shell/*.svg "$SRC"/assets/icons/apps/*.svg; do
            install -D -m644 "$f" "/usr/share/finn/icons/$(basename "$f")"
        done
    else
        for icon in arc avatar battery bluetooth boot-mark chevron-right cursor \
                    display edit-01 expand folder home-09 info internet minus-sign \
                    moon oobe-hi paint-brush power refresh search-01 settings-02 \
                    sidebar-toggle sun-01 sun-03 view-open-windows volume-02 \
                    wifi-02 x; do
            get "/usr/share/finn/icons/$icon.svg" "assets/icons/shell/$icon.svg"
        done
        for icon in app-grid browser chevron-left files library monitor \
                    native-apps notes settings-app store terminal viewer \
                    windows; do
            get "/usr/share/finn/icons/$icon.svg" "assets/icons/apps/$icon.svg"
        done
    fi
    chown -R root:finn /usr/share/finn /var/lib/finn 2>/dev/null || true

    step "the boot splash"
    get /etc/plymouth/plymouthd.conf base/airootfs/etc/plymouth/plymouthd.conf
    get /etc/mkinitcpio.conf base/installed/etc/mkinitcpio.conf
    mkdir -p /usr/share/plymouth/themes/finnos
    for f in finnos.plymouth finnos.script mark.png bar-track.png bar-fill.png bar-cap-l.png bar-cap-r.png; do
        get "/usr/share/plymouth/themes/finnos/$f" \
            "base/airootfs/usr/share/plymouth/themes/finnos/$f"
    done
    systemctl enable plymouth-start.service 2>/dev/null ||
        echo "WARN: plymouth-start.service is not available"

    step "the system units"
    get /etc/systemd/system/finn-comp.service base/finn-comp.service
    get /etc/systemd/system/finn-session.service base/finn-session.service
    get /etc/systemd/system/finn-shell.service base/finn-shell.service
    get /etc/systemd/system/finn-wallpaper.service base/finn-wallpaper.service
    get /etc/systemd/system/finn-failure.service base/finn-failure.service
    get /etc/systemd/system/finn-recovery.service base/finn-recovery.service
    get /etc/systemd/system/finn-update-check.service base/finn-update-check.service
    get /etc/systemd/system/finn-update-check.timer base/finn-update-check.timer
    get /etc/systemd/system/finn-boot-health.service base/finn-boot-health.service
    get /usr/share/finn/ota/grub-ab.cfg ota/grub-ab.cfg
    get /usr/lib/tmpfiles.d/finn.conf base/finn-tmpfiles.conf
    systemctl daemon-reload 2>/dev/null || true

    step "the sources & session binaries"
    mkdir -p /opt/finnos
    if [ -d "$SRC" ] && [ "$SRC" != "/opt/finnos" ]; then
        tar -C "$SRC" -cf - . | tar -C /opt/finnos -xf -
        chown -R finn:finn /opt/finnos 2>/dev/null || true
        echo "SOURCES=installed"
    elif [ -d /opt/finnos/src ]; then
        echo "SOURCES=present"
    elif [ "$OFFLINE" = 0 ] && [ -n "$HOST" ] && curl -sfLo /tmp/finnos.tar.gz "$HOST/finnos.tar.gz"; then
        tar -C /opt/finnos -xzf /tmp/finnos.tar.gz
        chown -R finn:finn /opt/finnos 2>/dev/null || true
        echo "SOURCES=installed"
    else
        echo "WARN: sources not copied; using pre-installed binaries if present"
    fi

    # Fast offline path: if FinnOS session binaries are already pre-built in the
    # offline image, skip the multi-minute chroot compilation!
    SHELL_STATE=missing
    if [ -x /usr/local/bin/finn-comp ] && [ -x /usr/local/bin/finn-session ] &&
       [ -x /usr/local/bin/finn-shell ] && [ -x /usr/local/bin/finn-wallpaper ]; then
        SHELL_STATE=installed
        echo "SHELL=prebuilt-installed"
    elif [ -d /opt/finnos ]; then
        cd /opt/finnos
        rc=0
        make -j"$(nproc 2>/dev/null || echo 2)" >/tmp/finn-build.log 2>&1 || rc=$?
        if [ "$rc" -eq 0 ]; then
            make install >>/tmp/finn-build.log 2>&1 || rc=$?
        fi
        if [ "$rc" -eq 0 ] && [ -x /usr/local/bin/finn-comp ] &&
           [ -x /usr/local/bin/finn-session ] && [ -x /usr/local/bin/finn-shell ] &&
           [ -x /usr/local/bin/finn-wallpaper ]; then
            SHELL_STATE=installed
            echo "SHELL=installed"
        else
            echo "WARN: the build or install failed; see /tmp/finn-build.log"
            tail -30 /tmp/finn-build.log 2>/dev/null || true
        fi
    fi
    chown -R finn:finn /opt/finnos 2>/dev/null || true

    if [ "$SHELL_STATE" = installed ]; then
        mkdir -p /var/lib/systemd/linger
        touch /var/lib/systemd/linger/finn
        systemctl enable finn-comp finn-session finn-shell finn-wallpaper
        if [ -x /usr/local/bin/recovery-mode ]; then
            systemctl enable finn-recovery.service 2>/dev/null || true
        fi
        if [ -x /usr/local/bin/finn-update ]; then
            systemctl enable finn-update-check.timer finn-boot-health.service 2>/dev/null || true
        fi
        systemctl set-default graphical.target
        for unit in finn-comp finn-session finn-shell finn-wallpaper; do
            [ -e "/etc/systemd/system/graphical.target.wants/$unit.service" ] ||
                { echo "error: $unit was not enabled for graphical.target" >&2; exit 1; }
        done
        echo "SESSION=enabled"
    else
        echo "SESSION=not-enabled: the session binaries are missing"
    fi
    post
else
    SHELL_STATE=installed
fi

# ── Initramfs + Universal Dual-Boot Bootloader Setup ─────────────────────────
step "initramfs + bootloader"
mkinitcpio -P 2>&1 | tail -5
[ -f /boot/vmlinuz-linux ] || { echo "error: no kernel in /boot" >&2; exit 1; }
[ -f /boot/initramfs-linux.img ] || { echo "error: no initramfs in /boot" >&2; exit 1; }
ls -la /boot/

serial_args=""
if [ "${FINN_SERIAL:-0}" = 1 ]; then
    serial_args=" console=ttyS0,115200 console=tty0"
    systemctl enable serial-getty@ttyS0.service 2>/dev/null ||
        echo "WARN: serial-getty@ttyS0 could not be enabled"
    echo "SERIAL=console"
else
    echo "SERIAL=none"
fi

mkdir -p /etc/default /etc/grub.d
# Enable os-prober so dual-booting with Windows, macOS, or any Linux distro
# automatically registers their entries in GRUB as well!
cat > /etc/default/grub <<EOF
GRUB_DEFAULT=0
GRUB_TIMEOUT=3
GRUB_DISTRIBUTOR="FinnOS"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3$serial_args"
GRUB_DISABLE_OS_PROBER=false
EOF

if [ "${FINN_AB_SLOTS:-0}" = 1 ]; then
    install -d /etc/grub.d /boot/grub /boot/finnos/a
    cp /boot/vmlinuz-linux /boot/finnos/a/vmlinuz
    cp /boot/initramfs-linux.img /boot/finnos/a/initramfs.img
    if [ -f /usr/share/finn/ota/grub-ab.cfg ]; then
        cat > /etc/grub.d/09_finnos_ab <<'ABEOF'
#!/bin/sh
cat /usr/share/finn/ota/grub-ab.cfg
ABEOF
        chmod 755 /etc/grub.d/09_finnos_ab
    fi
    if command -v grub-editenv >/dev/null 2>&1; then
        [ -f /boot/grub/grubenv ] || grub-editenv /boot/grub/grubenv create
        SLOT_VER="$(cat /usr/share/finn/version 2>/dev/null || echo 0.2.0)"
        grub-editenv /boot/grub/grubenv set finn_active_slot=a finn_tries_left=0 "finn_version_a=$SLOT_VER"
    fi
fi

# Detect where the ESP is mounted (/boot vs /boot/efi) and firmware mode.
ESP_MOUNT="${FINN_ESP_MOUNT:-}"
if [ -z "$ESP_MOUNT" ]; then
    if [ "$(findmnt -no FSTYPE /boot/efi 2>/dev/null || true)" = "vfat" ]; then
        ESP_MOUNT="/boot/efi"
    elif [ "$(findmnt -no FSTYPE /boot 2>/dev/null || true)" = "vfat" ]; then
        ESP_MOUNT="/boot"
    fi
fi

# Generate deterministic dual-boot custom GRUB entries (Windows Boot Manager,
# macOS, and FinnOS Recovery) so dual-boot entries ALWAYS appear even when
# os-prober cannot run inside a chroot.
cat > /etc/grub.d/40_finnos_dualboot <<'EOF'
#!/bin/sh
exec tail -n +3 $0
EOF
chmod 755 /etc/grub.d/40_finnos_dualboot
cat >> /etc/grub.d/40_finnos_dualboot <<EOF
menuentry "FinnOS Recovery" {
    search --no-floppy --fs-uuid --set=root $ROOTUUID
    linux /boot/vmlinuz-linux root=UUID=$ROOTUUID rw finn.recovery quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3$serial_args
    initrd /boot/initramfs-linux.img
}
EOF
if [ -n "$ESP_MOUNT" ] && [ -f "${ESP_MOUNT}/EFI/Microsoft/Boot/bootmgfw.efi" ]; then
    ESP_UUID="$(findmnt -no UUID "$ESP_MOUNT" 2>/dev/null || true)"
    cat >> /etc/grub.d/40_finnos_dualboot <<EOF
menuentry "Windows Boot Manager" {
    insmod part_gpt
    insmod fat
    search --no-floppy --fs-uuid --set=root ${ESP_UUID}
    chainloader /EFI/Microsoft/Boot/bootmgfw.efi
}
EOF
fi
if [ -n "$ESP_MOUNT" ] && { [ -f "${ESP_MOUNT}/System/Library/CoreServices/boot.efi" ] || [ -f "${ESP_MOUNT}/EFI/Apple/Boot/boot.efi" ]; }; then
    ESP_UUID="$(findmnt -no UUID "$ESP_MOUNT" 2>/dev/null || true)"
    APPLE_EFI="/EFI/Apple/Boot/boot.efi"
    [ -f "${ESP_MOUNT}/System/Library/CoreServices/boot.efi" ] && APPLE_EFI="/System/Library/CoreServices/boot.efi"
    cat >> /etc/grub.d/40_finnos_dualboot <<EOF
menuentry "macOS" {
    insmod part_gpt
    insmod fat
    search --no-floppy --fs-uuid --set=root ${ESP_UUID}
    chainloader ${APPLE_EFI}
}
EOF
fi

efi=0
PT_TYPE="$(blkid -s PTTYPE -o value "$FINN_DISK" 2>/dev/null || true)"
EFI_TARGET=x86_64-efi
[ "$(uname -m)" = 'aarch64' ] && EFI_TARGET=arm64-efi

if [ "$ESP_MOUNT" = "/boot" ] && [ "$PT_TYPE" != "dos" ] &&
   command -v bootctl >/dev/null 2>&1 && bootctl --esp-path=/boot install 2>&1 | tail -5; then
    if [ -f /usr/share/finn/boot/finn-bootx64.efi ]; then
        install -Dm644 /usr/share/finn/boot/finn-bootx64.efi /boot/EFI/BOOT/BOOTX64.EFI
        install -Dm644 /usr/share/finn/boot/finn-bootx64.efi /boot/EFI/systemd/systemd-bootx64.efi
    elif [ -f /usr/share/finn/boot/finn-bootaa64.efi ]; then
        install -Dm644 /usr/share/finn/boot/finn-bootaa64.efi /boot/EFI/BOOT/BOOTAA64.EFI
        install -Dm644 /usr/share/finn/boot/finn-bootaa64.efi /boot/EFI/systemd/systemd-bootaa64.efi
    fi
    mkdir -p /boot/loader/entries
    if [ "${FINN_AB_SLOTS:-0}" = 1 ]; then
        printf 'default finnos-a.conf\ntimeout 3\nconsole-mode max\neditor no\nauto-entries yes\nauto-firmware yes\n' > /boot/loader/loader.conf
        printf 'title FinnOS (A)\nlinux /finnos/a/vmlinuz\ninitrd /finnos/a/initramfs.img\noptions root=PARTLABEL=FINNOS_A rw finn.slot=a quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3%s\n' \
            "$serial_args" > /boot/loader/entries/finnos-a.conf
    else
        printf 'default finnos.conf\ntimeout 3\nconsole-mode max\neditor no\nauto-entries yes\nauto-firmware yes\n' > /boot/loader/loader.conf
        printf 'title FinnOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3%s\n' \
            "$ROOTUUID" "$serial_args" > /boot/loader/entries/finnos.conf
    fi
    printf 'title FinnOS Recovery\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\noptions root=UUID=%s rw finn.recovery quiet splash vt.global_cursor_default=0 plymouth.ignore-serial-consoles loglevel=3%s\n' \
        "$ROOTUUID" "$serial_args" > /boot/loader/entries/finnos-recovery.conf
    efi=1
    echo "BOOTLOADER=systemd-boot"
elif [ -n "$ESP_MOUNT" ] && command -v grub-install >/dev/null 2>&1; then
    # Works cleanly for BOTH 100 MiB Windows ESPs (/boot/efi) and MBR UEFI disks!
    # Try registering NVRAM entry first; fall back to --removable if firmware NVRAM is read-only.
    if grub-install --target="$EFI_TARGET" --efi-directory="$ESP_MOUNT" \
            --bootloader-id=FinnOS 2>&1 | tail -5 ||
       grub-install --target="$EFI_TARGET" --efi-directory="$ESP_MOUNT" \
            --bootloader-id=FinnOS --removable 2>&1 | tail -5; then
        mkdir -p /boot/grub
        grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 ||
            { echo "error: grub-mkconfig failed" >&2; exit 1; }
        efi=1
        echo "BOOTLOADER=grub-efi"
    fi
fi

# Legacy BIOS bootloader (MBR gap on dos disks, or 1 MiB BIOS Boot ef02 on GPT disks).
bios=0
if [ "$(uname -m)" = 'x86_64' ] && command -v grub-install >/dev/null 2>&1 &&
   grub-install --target=i386-pc --recheck "$FINN_DISK" 2>&1 | tail -5; then
    mkdir -p /boot/grub
    grub-mkconfig -o /boot/grub/grub.cfg 2>&1 | tail -5 || true
    bios=1
    echo "BOOTLOADER=grub-bios"
else
    echo "WARN: no BIOS boot code on $FINN_DISK; this machine boots via UEFI"
fi

[ "$efi" = 1 ] || [ "$bios" = 1 ] ||
    { echo "error: no bootloader was installed" >&2; exit 1; }

if [ -f /boot/grub/grub.cfg ] &&
   { grep -q "root=UUID=$ROOTUUID" /boot/grub/grub.cfg || grep -q "root=PARTLABEL=FINNOS_A" /boot/grub/grub.cfg; }; then
    :
elif compgen -G '/boot/loader/entries/*.conf' >/dev/null &&
     { grep -q "root=UUID=$ROOTUUID" /boot/loader/entries/*.conf || grep -q "root=PARTLABEL=FINNOS_A" /boot/loader/entries/*.conf; }; then
    :
else
    echo "error: no boot menu names root=UUID=$ROOTUUID or root=PARTLABEL=FINNOS_A" >&2
    exit 1
fi

post
echo
if [ "$SHELL_STATE" != installed ]; then
    echo "FINN_CHROOT_FAILED: the compositor is not installed; the machine boots to a login console"
    exit 1
fi
credentials
echo "FINN_CHROOT_COMPLETE"
