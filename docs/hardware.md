# Hardware

FinnOS is an Arch Linux system with a different shell. Hardware support is
therefore Arch's: the kernel, the firmware and the userspace drivers come from
the distribution's repositories, not from a FinnOS fork. This page says what
that means for the machine on the desk, and what is still a limit rather than a
promise.

## The reference machine

The x86_64 release target is a Dell OptiPlex 3020:

- Intel Core i3-4130 (Haswell, 2 cores / 4 threads, HD Graphics 4400)
- 4 GB of RAM or more; 8 GB is comfortable with Flatpak and Wine
- SATA SSD or HDD
- 1080p panel over DisplayPort, HDMI or VGA
- Realtek Gigabit Ethernet; Wi-Fi only with an add-in card

It is a general x86_64 profile, not a model-specific image. The same ISO boots
on other UEFI or BIOS machines that Arch supports. The machine-specific page is
[dell-optiplex.md](dell-optiplex.md).

The aarch64 image is a **development target**: it exists so the compositor and
the control plane can be built and run on ARM, not as a promise of a supported
ARM desktop.

## What the image installs

All of this is in `base/packages.x86_64`; the ARM list is
`base/packages.aarch64`. A package list is the honest statement of support, so
it is worth reading directly.

| Area | Packages | Why |
|---|---|---|
| kernel and firmware | `linux`, `linux-firmware`, `intel-ucode`, `amd-ucode` | the kernel, firmware blobs and microcode for both CPU vendors |
| graphics | `mesa`, `libglvnd`, `vulkan-intel` | GBM/EGL for the compositor, Vulkan for 64-bit Wine, Flatpak and games |
| 32-bit graphics | `lib32-mesa`, `lib32-vulkan-icd-loader`, `lib32-vulkan-intel` | many Windows installers still need 32-bit GL/Vulkan |
| display and input | `libdrm`, `libinput`, `seatd`, `wayland`, `wayland-protocols`, `libxkbcommon` | what the compositor and its Wayland host build against |
| text and images | `freetype2`, `fontconfig`, `libwebp`, `alsa-lib`, `alsa-ucm-conf` | Fixel Display, the wallpaper, the mixer |
| audio | `pipewire`, `pipewire-pulse`, `wireplumber` | the session sound server for apps; the shell's own mixer reads ALSA |
| network | `networkmanager`, `wpa_supplicant`, `openssh`, `curl` | wired, wireless, an optional remote login, and the OTA/package fetches |
| Bluetooth | `bluez`, `bluez-utils` | adapters, when the machine has one |
| Windows | `wine`, `winetricks`, `wine-mono`, `wine-gecko` | the x86_64 compatibility stack |
| Linux apps | `flatpak` | the Flatpak path, and the Store's Flatpak catalogue |
| installer and boot | `arch-install-scripts`, `dosfstools`, `e2fsprogs`, `parted`, `gptfdisk`, `grub`, `efibootmgr` | partitioning, filesystems, pacstrap and the bootloader |

The installer enables `[multilib]` before installing Wine. Without that
repository the 32-bit libraries Wine needs are not available, and the install
fails rather than producing a half-working Wine.

The ARM list drops the x86-only parts: no `intel-ucode`/`amd-ucode`, no 32-bit
GL/Vulkan stack and no Wine, because Arch Linux ARM does not package them. It
does carry Flatpak, PipeWire, BlueZ and the same NetworkManager stack.

## Graphics

The compositor opens `/dev/dri/card*` directly and uses GBM/EGL/GLES2 when the
driver provides it, falling back to a software framebuffer when it does not. On
the OptiPlex the `i915` driver and Mesa provide the accelerated path; the
software path is what a machine without a usable GPU gets, and both paths draw
the same surfaces.

The display is a single CRTC and a single connector. There is no multi-monitor
support and no per-output scaling; the compositor picks the first connected
connector with a mode and drives it. `screen.info` reports the density it is
actually using.

Wine uses WineD3D (OpenGL) by default. That is the safer choice on HD Graphics
4400: it works with the Mesa driver the image ships and does not require a DXVK
install. A user who wants DXVK can install it separately; the image does not
force it.

## Audio

Two things are true at once:

- The shell's mixer reads **ALSA directly** (`finn_state_set_volume`,
  `sound.get`/`sound.set`). The Control Center slider is the real mixer, and a
  machine with no card gets `ERR hardware unavailable` rather than a slider
  that pretends.
- Applications get **PipeWire**: `pipewire-pulse` is the PulseAudio socket a
  Wine program or a Flatpak app expects, and `wireplumber` is the session
  manager that gives it devices.

The compositor is not a sound server and does not pretend to be one. If
`aplay -l` lists no card, the shell says the machine has no audio.

## Network and Bluetooth

`NetworkManager` owns connections, with `wpa_supplicant` as the Wi-Fi backend.
The Control Center's Wi-Fi tile and the Settings Network page read the adapter
that is actually present; a machine with no wireless adapter says "Not
available" rather than showing a dead toggle.

`bluez` is installed and its service carries its own condition, so it does not
start on a machine with no controller. The Settings Bluetooth page reports the
controller it finds, or its absence.

`sshd` is installed and left disabled. Enabling it is a decision made on the
machine. See [security.md](security.md).

## Storage and boot

The installer writes a GPT layout: a 512 MiB EFI system partition, a 1 MiB BIOS
boot partition, and an ext4 root labelled `finnos`. It works on NVMe, SATA and
USB disks, and the partition names follow the device (`nvme0n1p1`, `sda1`).

UEFI boots GRUB, with systemd-boot as the fallback when GRUB cannot register
its EFI entry. BIOS boots GRUB's boot code from the MBR. The ARM image is UEFI
only, which is what an ARM board has.

## Limits that are real

- **Secure Boot.** The installer writes a normal GRUB or systemd-boot setup; it
  does not sign the kernel or the bootloader with a key the firmware trusts.
  Disable Secure Boot, or enrol your own key, before installing.
- **NVIDIA.** The image does not install the proprietary NVIDIA driver. The
  open `nouveau` path is what Mesa provides; machines that need the proprietary
  driver must install it themselves.
- **One display.** No multi-monitor, no per-output scale, no automatic HiDPI
  selection.
- **Printers, scanners and fingerprint readers.** There is no FinnOS settings
  surface for them yet, and no promise that the Arch packages are configured.
- **Suspend, hibernate and lid handling.** They are the distribution's, and
  FinnOS does not add a session integration: a machine that suspends is a
  machine whose compositor may need to repaint on resume. Check it on your
  hardware and report it; it is not a verified feature.
- **ARM64.** The image is a development target. It has no Wine runtime in the
  official Arch Linux ARM repositories. FinnOS detects Hangover or Box64 if the
  user installs one, and otherwise says Windows applications are unavailable;
  it does not ship a runtime it cannot support.
- **Flatpak sandboxes.** Flatpak GUI apps are hosted by the compositor's
  limited Wayland server. Apps that require portals or Wayland protocols the
  host does not implement can fail to start. The Store says this rather than
  pretending every Flathub app works.
- **Wine and winget GUI behaviour** has not been validated on FinnOS. The
  commands exist; the pixels are unproven.
- **Finn Browser and headless Chromium.** Finn Browser draws its native chrome
  inside `finn-comp` and relies on an external headless Chromium engine
  (`chromium` from the Arch repositories or Flatpak `org.chromium.Chromium`) to
  render web content. If neither engine is installed, the browser window opens
  cleanly and reports that the engine is unavailable.

## Checking a machine

After installing, open a console (`Ctrl+Alt+F2`) and check the pieces the shell
depends on:

```sh
lspci -k | grep -A3 -E 'VGA|3D|Ethernet|Network'
dmesg | grep -E 'i915|firmware|error'
systemctl status finn-comp finnd NetworkManager
wpctl status            # PipeWire sees the sink
aplay -l                # ALSA sees the card
nmcli device status     # wired/wireless state
ls /sys/class/backlight # is there a backlight to control
cat /sys/class/power_supply/*/capacity 2>/dev/null
```

The Settings pages read the same state: System Information for CPU/RAM/disk,
Network for link state, Sound for the ALSA mixer, Power for the battery when one
is present. A desktop with no battery reports mains, not 0%.

If the compositor does not start, `finn-failure.service` says so on the console
and the journal says why. [troubleshooting.md](troubleshooting.md) has the
sequence.
