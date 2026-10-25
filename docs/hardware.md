# Hardware

FinnOS is an Arch Linux system with a different shell. The hardware support is
the hardware support of the official Arch repositories: the kernel, the
firmware and the userspace drivers are the distribution's, not a FinnOS fork.
This page says what that means for the machine on the desk, and what is still
a limit rather than a promise.

## The reference machine

The release target is a Dell OptiPlex 3020:

- Intel Core i3-4130 (Haswell, 2 cores / 4 threads, HD Graphics 4400)
- 4 GB or more of RAM (8 GB is comfortable with Flatpak and Wine)
- SATA SSD or HDD
- 1080p panel over DisplayPort, HDMI or VGA
- Realtek Gigabit Ethernet; Wi-Fi only with an add-in card

It is a general x86_64 profile, not a model-specific image. The same ISO boots
on other UEFI or BIOS machines that Arch supports.

## What is installed for hardware

- `linux` and `linux-firmware` — the kernel and the firmware blobs.
- `intel-ucode` and `amd-ucode` — CPU microcode for both vendors.
- `mesa`, `libglvnd`, `vulkan-intel` — the Intel graphics stack, including
  the Vulkan driver used by 64-bit Wine and Flatpak apps.
- `lib32-mesa`, `lib32-vulkan-icd-loader`, `lib32-vulkan-intel` — the 32-bit
  half, which many Windows installers still need.
- `networkmanager` and `wpa_supplicant` — wired and wireless networking.
- `bluez` and `bluez-utils` — Bluetooth adapters.
- `pipewire`, `pipewire-pulse` and `wireplumber` — audio for Flatpak and Wine;
  the shell's own mixer still reads ALSA directly.
- `wine`, `winetricks`, `wine-mono` and `wine-gecko` — the x86_64 Windows
  compatibility stack.
- `flatpak` — Linux application packages.

The installer enables `[multilib]` before installing `wine`. Without that
repository the 32-bit libraries Wine needs are not available, and the install
would fail rather than produce a half-working Wine.

## Graphics

The compositor opens `/dev/dri/card*` directly and uses GBM/EGL/GLES2 when the
driver provides it, falling back to a software framebuffer when it does not.
On the OptiPlex the i915 driver and Mesa provide the accelerated path.

Wine uses WineD3D (OpenGL) by default. That is the safer choice on HD Graphics
4400: it works with the Mesa driver the image ships, and it does not require a
DXVK install. A user who wants DXVK can install it separately; the image does
not force it.

## What to check on the OptiPlex

After installing, open a console and check the pieces the shell depends on:

```sh
lspci -k | grep -A3 -E 'VGA|3D|Ethernet|Network'
dmesg | grep -E 'i915|firmware|error'
systemctl status finn-comp finnd NetworkManager
wpctl status            # PipeWire sees the sink
aplay -l                # ALSA sees the card
nmcli device status     # wired/wireless state
```

The Settings pages read the same state: System Information for CPU/RAM/disk,
Network for link state, Sound for the ALSA mixer, and Power for the battery
when one is present. A desktop with no battery reports mains, not 0%.

## Limits that are real

- **Secure Boot.** The installer writes a normal GRUB setup; it does not sign
  the kernel or the bootloader with a key the firmware trusts. Disable Secure
  Boot, or enrol your own key, before installing.
- **NVIDIA.** The image does not install the proprietary NVIDIA driver. The
  open `nouveau` path is what Mesa provides; machines that need the proprietary
  driver must install it themselves.
- **Printers, scanners and fingerprint readers.** There is no FinnOS settings
  surface for them yet, and no promise that the Arch packages are configured.
- **ARM64.** The ARM image is a development target. It has no Wine runtime in
  the official Arch Linux ARM repositories. FinnOS detects Hangover or Box64
  if the user installs one, and otherwise says Windows applications are
  unavailable; it does not ship a runtime it cannot support.
- **Flatpak sandboxes.** Flatpak GUI apps are hosted by the compositor's
  Wayland server. Apps that require portals or Wayland protocols the host does
  not implement can fail to start. The Store says this rather than pretending
  every Flathub app works.

## The installer

The ISO is built natively from the tree in `base/` with mkarchiso: the
profile, the package list, the live root and the three boot menus, with
the FinnOS sources at `/opt/finnos` and the installer on the live shell.
No base image is downloaded. It boots under UEFI and BIOS. The installer
partitions the disk, enables `[multilib]`, installs the package list,
copies the tree, builds the compositor on the machine, and installs GRUB.

The CI boot test runs the ISO in QEMU. A physical OptiPlex is the final check:
if the machine boots the live shell, the installer completes, and the installed
system reaches the desktop, the hardware path is working. Anything less is
reported as untested, not as supported.
