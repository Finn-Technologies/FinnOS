# Virtualization & Guest Tools

FinnOS supports running as a guest inside **VMware Workstation / Fusion / ESXi**,
**VirtualBox**, and **QEMU / KVM**, and provides both out-of-the-box x86_64 base
packages and an on-demand installer (`finn-guest-tools` and **Settings →
Compatibility → Virtualization & Guest Tools**).

## What ships in the x86_64 base image

`base/packages.x86_64` includes three zero-GTK3, zero-Xorg guest packages:

| Hypervisor | Package | Services / Kernel modules | Bare-metal impact |
|---|---|---|---|
| **VMware** | `open-vm-tools` | `vmtoolsd.service`, `vmware-vmblock-fuse.service`, in-kernel `vmwgfx`, `vmw_vmci`, `vmw_balloon`, `vmhgfs-fuse` | None (`ConditionVirtualization=vmware`) |
| **VirtualBox** | `virtualbox-guest-utils-nox` | `vboxservice.service`, in-kernel `vboxguest`, `vboxsf`, `vboxvideo` (`vboxsf` group) | None (`ConditionVirtualization=oracle`) |
| **QEMU / KVM** | `qemu-guest-agent` | `qemu-guest-agent.service`, in-kernel `virtio_gpu`, `virtio_balloon`, `virtio_console` | None (udev-bound to `org.qemu.guest_agent.0`) |

`virtualbox-guest-utils-nox` is selected deliberately instead of
`virtualbox-guest-utils` because the X11 variant hard-depends on `gtk3`, which
`base/pacman.conf` refuses by design.

## Installing or managing guest tools on an existing machine

### Graphical (Settings)

Open **Settings → Compatibility → Virtualization & Guest Tools**. Settings probes
real DMI, PCI vendor IDs (`0x15ad` VMware, `0x80ee` VirtualBox, `0x1af4` VirtIO),
`/sys/module`, `/proc/mounts`, and `systemctl` state, and provides **Install**,
**Enable**, and **Mount Shared** buttons powered asynchronously through
`pkexec finn-guest-tools`.

### Command line (`finn-guest-tools`)

```sh
finn-guest-tools status                  # read-only report (or --json)
sudo finn-guest-tools install auto       # detect hypervisor and install matching suite
sudo finn-guest-tools install vmware     # install & enable open-vm-tools
sudo finn-guest-tools install virtualbox # install & enable virtualbox-guest-utils-nox
sudo finn-guest-tools install qemu       # install & enable qemu-guest-agent
sudo finn-guest-tools enable auto        # enable & start installed guest services
sudo finn-guest-tools mount-shared       # mount VMware HGFS at /mnt/hgfs
```

### Installing from a Guest Additions / VMware Tools CD-ROM

- **VMware Tools CD (`VMwareTools-*.tar.gz`)**: VMware recommends
  `open-vm-tools` on modern Linux 6.x/7.x kernels because the legacy tarball's
  out-of-tree kernel modules no longer compile against current kernels.
  `finn-guest-tools install vmware` installs `open-vm-tools` and enables
  `vmtoolsd.service` and `vmware-vmblock-fuse.service` using the in-kernel
  `vmwgfx` and `vmw_*` drivers.
- **VirtualBox Guest Additions ISO (`VBoxLinuxAdditions.run`)**:
  `finn-guest-tools install virtualbox` installs `virtualbox-guest-utils-nox`
  (recommended; uses in-kernel `vboxguest`/`vboxsf`/`vboxvideo` without GTK3).
  If you explicitly want to run the mounted CD-ROM script instead, run:
  ```sh
  sudo finn-guest-tools install virtualbox --from-iso --from-cd /run/media/finn/VBox_GAs_*
  ```
  which installs `linux-headers`, `dkms`, `bzip2`, `tar`, `perl`, and
  `base-devel`, runs `VBoxLinuxAdditions.run --nox11`, and enables
  `vboxservice.service`.

## Dynamic display resizing

`finn-comp` tracks each DRM output's preferred mode whenever no explicit
`resolution=` override is pinned in `~/.config/finn/settings` (or
`FINN_OUTPUT_MODE`). When VMware (`vmwgfx`) or VirtualBox (`vmsvga` /
`vboxvideo`) updates the virtual monitor's preferred resolution on host window
resize, `finn-comp` adopts the new mode and relayouts the wallpaper and shell
layers automatically.

## Rendering on virtual GPUs without 3D acceleration

VMware SVGA (`vmwgfx`) can expose a DRM device even when VMware's **Accelerate
3D graphics** option is off. In that state Mesa cannot create an EGL or Vulkan
device, so a GPU-only renderer would make `finn-comp` exit at boot. `finn-comp`
detects this and falls back to wlroots' pixman software renderer, logging
`no GPU renderer available; falling back to software (pixman)`. The desktop,
Settings and guest-tools UI work normally, but rendering is CPU-bound; enable
3D acceleration in the VM where the host supports it for hardware rendering.
This is a renderer fallback, not a claim that the virtual GPU has 3D support.

## Visual verification

An installed x86_64 system was booted in VMware Workstation 26.0.1 with
`open-vm-tools` active. Confirmed from the live framebuffer: the desktop and
dock at the virtual monitor's 1280×800 preferred mode (not a forced 1440×900),
**Settings → Compatibility → Virtualization & Guest Tools** reporting the host
as "VMware, Inc. VMware Virtual Platform" with VMware Tools **Active**
(`vmtoolsd` running, `vmwgfx`/`vmw_vmci`/`vmw_balloon`/`vmw_vsock_vmci_transport`
loaded) and VirtualBox/QEMU suites **Installed** but inactive on a VMware host,
and **Settings → About** reporting "VMware Graphics vmwgfx". Host tools reported
`vmrun checkToolsState` = `running`, `getGuestIPAddress` = `192.168.33.128` and
a graceful `vmrun stop ... soft`.

## Honest limits

- **Host–guest clipboard & drag-and-drop**: `open-vm-tools`'s `vmware-user`
  DnD/CP plugin requires `gtkmm3` + X11, and `virtualbox-guest-utils`'s
  `VBoxClient --clipboard` hard-depends on `gtk3` + X11. Because FinnOS is a
  Wayland-native OS whose `base/pacman.conf` excludes GTK3 and `xorg-server`,
  host–guest clipboard sync and drag-and-drop are **not** provided by those X11
  plugins today.
- **Shared folders**: VMware Shared Folders mount cleanly at `/mnt/hgfs` via
  `vmhgfs-fuse` (`finn-guest-tools mount-shared`); VirtualBox Shared Folders
  automount via `vboxservice.service` and `vboxsf` (user `finn` is added to the
  `vboxsf` group).
- **VMware UEFI**: `vm/create-dev-vm.sh` configures BIOS boot (`firmware = "bios"`)
  because VMware's virtual UEFI firmware does not persist ESP variables across
  cold boots consistently.
