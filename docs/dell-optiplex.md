# The Dell OptiPlex 3020

The reference x86_64 machine: the one the release is judged on, and the one
whose firmware menu the instructions name. It is a general x86_64 profile, not
a model-specific image; the same disc boots on other UEFI and BIOS machines
Arch supports.

## The machine

| | |
|---|---|
| CPU | Intel Core i3-4130 (Haswell, 2 cores / 4 threads) |
| Graphics | Intel HD Graphics 4400, driven by `i915` and Mesa |
| RAM | 4 GB minimum; 8 GB is comfortable with Flatpak and Wine |
| Disk | SATA SSD or HDD; an NVMe disk is `/dev/nvme0n1`, a SATA disk `/dev/sda` |
| Display | 1080p over DisplayPort, HDMI or VGA |
| Network | Realtek Gigabit Ethernet. Wi-Fi only with an add-in card |
| Firmware | UEFI or BIOS; Secure Boot must be off |

The whole package set is in [hardware.md](hardware.md). This page is the
machine-specific part: what to press, what to check, and what has actually been
seen.

## Firmware

1. Press **F2** at power-on for the firmware settings.
2. **Security → Secure Boot → Disabled.** The installer writes an unsigned
   GRUB or systemd-boot; a machine with Secure Boot on refuses it.
3. Choose UEFI or BIOS. Both boot paths are on the disc; the installer prefers
   GRUB's UEFI loader and falls back to systemd-boot when GRUB cannot register
   its EFI entry.
4. Press **F12** at power-on for the one-time boot menu and choose the USB
   entry.

The disc's menu has **Install FinnOS** and **FinnOS live shell**. The first
hands the console to the installer after ten seconds; Ctrl-C stays in the
shell. The full walkthrough is [install.md](install.md).

## Install

```sh
lsblk -dno NAME,SIZE,TYPE,MODEL
finn-install --disk /dev/nvme0n1 --yes     # or /dev/sda
```

The installer has no default disk, refuses a partition or a mounted disk, and
prints `FINN_INSTALL_COMPLETE` only after checking the disk it wrote. It does
not reboot for you: unmount the medium, remove it, and start the machine.

## What has been verified

| | |
|---|---|
| Boot the install medium | yes — the machine boots the stick; the disc carries both UEFI and BIOS entries |
| Install to the internal disk | yes — the installer completes and the disk boots |
| Reach the desktop | yes — `finn-comp` starts on tty1, the shell draws |
| Accelerated path | not separately recorded; the machine has `i915` + Mesa, so the GBM/EGL path is available. `journalctl -u finn-comp \| grep -i 'gbm\|egl'` says which path came up |
| Rescue console | `Ctrl+Alt+F2` reaches `getty@tty2` |

Shipping a machine and *using* it are different claims. The table records what
the project has looked at; audio, Bluetooth, suspend, Wi-Fi on an add-in card
and long-running stability are the things to check on your own machine and
report, with the output of the commands below.

## Checks after installing

```sh
lspci -k | grep -A3 -E 'VGA|3D|Ethernet|Network'
dmesg | grep -E 'i915|firmware|error'
systemctl status finn-comp finnd NetworkManager
wpctl status            # PipeWire sees the sink
aplay -l                # ALSA sees the card
nmcli device status     # wired/wireless state
```

The Settings pages read the same state: System Information for CPU/RAM/disk,
Network for link state, Sound for the ALSA mixer, Power for the battery when
one is present. A desktop with no battery reports mains, not 0%.

## Working on it over the network

```sh
sudo systemctl enable --now sshd
VM=finn@<optiplex-ip> make deploy
FINN_HOST=finn@<optiplex-ip> vm/shot.sh /tmp/shot.png
```

The dev loop is the same as the VM's: [development.md](development.md). The
machine's build is the real one — the OptiPlex has the DRM headers and the
compiler, which is why an install builds the compositor there rather than
copying a binary.

If the installer used the serial console, the installed GRUB and systemd-boot
entries keep `console=ttyS0` and the serial getty is enabled, so the machine
stays reachable over a serial cable. An install from a screen gets no serial
console.

## Limits on this machine

- **Secure Boot** is off, not signed. Enrol your own key if you need it on.
- **Wi-Fi** needs an add-in card; the on-board Ethernet is the supported network
  path. `wpa_supplicant` is installed as NetworkManager's backend, and the
  Control Center and Settings show the adapter that is actually there.
- **NVIDIA** is not installed. The image ships Mesa; a machine with a
  proprietary-driver requirement must install it itself.
- **Flatpak and Wine** are x86_64 paths and are installed, but their GUI
  behaviour on this hardware has not been fully validated. See
  [architecture.md](architecture.md#what-is-not-here-yet) for the exact status.
- **Printers, scanners and fingerprint readers** have no settings surface and
  no configuration promise.

## If it does not come up

`Ctrl+Alt+F2`, then:

```sh
systemctl status finn-comp finnd
sudo journalctl -u finn-comp -n 100 --no-pager
```

The compositor has a start limit and `finn-failure.service`, so a crash loop
stops and prints "FinnOS: the compositor stopped; press Ctrl+Alt+F2 for a
console" on the screen it was painting. The rest of the recovery paths are in
[troubleshooting.md](troubleshooting.md).
