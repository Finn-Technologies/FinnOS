# Install & Base Image

How FinnOS builds its live ISO, installs offline onto a target disk, configures a base system, and manages its systemd units.

## Supported architectures

FinnOS supports **`x86_64`** and **`aarch64`** only (`base/packages.x86` and `base/packages.arm` are compatibility symlinks; `base/mkfinniso`, `base/profiledef.sh`, and `base/setup.sh` reject 32-bit architectures).

## Live ISO & Offline Recovery Mode

The live ISO boots directly into graphical **FinnOS Recovery Mode** (`finn-recovery.service` on `tty1`, `apps/recovery-mode/`), backed by the pure C++20 `recovery_engine` (`src/recovery/`) and `vm/finn-install.sh` + `vm/finn-chroot.sh`.

- **100% offline installation**: Installs from local unpacked live `airootfs`, `airootfs.sfs` (`unsquashfs`), an on-disk `FINNOS_RECOVERY` partition, or a `.tar.zst` rootfs with FIPS 180-4 SHA-512 sidecar verification and zero network calls (`--offline-only`).
- **Partition tables & firmware**: Supports **UEFI** (`finn-boot` / `systemd-boot` or GRUB UEFI) and **Legacy BIOS** (GRUB BIOS, auto-carving `bios_grub` on GPT), on both **GPT** and **MBR** disks (automatically creating extended + logical partitions when 3 MBR primaries exist).
- **Install modes**:
  - Clean Erase (single root) or Clean Erase with **A/B OTA slots** (`--ab-slots`: `FINNOS_A`, `FINNOS_B`, `FINNOS_DATA`)
  - Dual-Boot in free space or non-destructive **Dual-Boot Resize** (NTFS/ext4/btrfs, protecting trailing Windows Recovery partitions and handling 100 MiB Windows ESPs by switching to GRUB UEFI at `/boot/efi`)
  - Target Partition install & data-preserving Reinstall
  - Optional on-disk `FINNOS_RECOVERY` partition creation
  - Bootloader Repair, Visual Disk Utility, System Logs, World Keyboards, Wi-Fi, and an embedded PTY terminal
- **First-boot OOBE separation**: Recovery Mode collects zero personal user/hostname/timezone details — those are handled once on the first normal boot by `finn-oobe` (`tools/finn-oobe-apply`).

## Building the live ISO (`base/mkfinniso`)

```sh
base/mkfinniso --check                 # validate profile, boot menus, branding, and unit parity
sudo base/mkfinniso --arch x86_64      # native mkarchiso build (requires pacman-based Linux + root)
```

`mkfinniso` also supports `--mode assemble` (`xorriso` + `mksquashfs` from caller-supplied kernel/initramfs). On `x86_64`, the ISO includes both `bios.syslinux` and `uefi.systemd-boot`; on `aarch64`, `uefi.systemd-boot` only.

## Converting an existing minimal base (`base/setup.sh`)

```sh
sudo base/setup.sh
```

`base/setup.sh`:
1. Removes unwanted legacy toolkits (`xorg-server`, `gtk3`, `gtk4`, `qt5-base`) if present.
2. Configures `/etc/pacman.conf` with `[finnos]` (falling back cleanly to offline/standard repos if `repo.finnos.org` is unreachable).
3. Installs base packages from `base/packages.<arch>` (including `qt6-*`, `mesa`, `pipewire`, `networkmanager`, `bluez`, `flatpak`, `fuse3`, `plymouth`, `xdg-utils`, plus `wine` and guest tools `open-vm-tools` / `virtualbox-guest-utils-nox` / `qemu-guest-agent` on `x86_64`).
4. Installs branding, Plymouth `finnos` theme, `/etc/sudoers.d/finn-oobe`, and systemd units — enabling the graphical session only when the required FinnOS binaries exist.
5. Leaves `NOPASSWD` sudo disabled unless `FINN_DEV_VM=1` is explicitly set.

## Systemd units (`base/` and `base/airootfs/etc/systemd/system/`)

Both copies are compared by `base/mkfinniso --check` and must never drift:

| Unit | Role |
|---|---|
| `finn-comp.service` | Starts `finn-comp` (Qt 6 Wayland compositor) as root (`ConditionKernelCommandLine=!finn.recovery`) |
| `finn-recovery.service` | Starts `recovery-mode` directly on `tty1` when `finn.recovery` is passed on the kernel cmdline |
| `finn-session.service` | Starts `finn-session` user broker as `finn` |
| `finn-shell.service` | Starts `finn-shell` desktop System UI as `finn` |
| `finn-wallpaper.service` | Starts `finn-wallpaper` (`background` layer) as `finn` |
| `finn-failure.service` | Rescue fallback if the compositor fails |
| `finn-boot-health.service` | Verifies healthy boot (`finn-comp`, `finn-session`, `/run/finn/wayland-0`) and commits OTA A/B slot |
| `finn-update-check.service` / `.timer` | Periodic OTA update check via `finn-update` |
| `finn-tmpfiles.conf` | Creates `/run/finn` (`0775 root:finn`) and `/run/finn/state` (`0775 root:finn`) |

## A/B OTA updates & first-party repository

- **A/B OTA updates** are implemented in `src/ota/finn-update` (`docs/ota.md`), supporting `systemd-boot` and `GRUB` slot switching with Ed25519 manifest verification and `zstd` deltas. Production OTA signing keys are not yet provisioned in-tree.
- **First-party `[finnos]` repo tooling** lives in `repo/` and `base/pkgbuilds/` (`docs/repo.md`). `https://repo.finnos.org` and `base/pkgbuilds/finnos-keyring/finnos.gpg` are placeholders until hosted infrastructure and production GPG keys are provisioned.
- **Verified boot**: UEFI Secure Boot, `dm-verity`, and TPM2 sealing are not yet implemented.
