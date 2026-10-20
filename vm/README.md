# FinnOS Dev VM

Working Arch Linux ARM (aarch64) install that boots FinnOS, running in Parallels on Apple Silicon.

## VM
| | |
|---|---|
| Name | `FinnOS-dev` |
| Guest | Arch Linux ARM aarch64 (core: `linux-aarch64 7.2.10`) |
| Resources | 4 GB RAM, 2 vCPU, 32 GB disk (matches the i3-4130 / 4 GB target) |
| Disk | `/dev/sda1` 512M EFI (vfat) + `/dev/sda2` 31.5G root (ext4, label `finnos`) |
| Bootloader | GRUB `arm64-efi`, bootloader-id `FinnOS` (removable) |
| Console | Parallels VM window, autologin `finn` on tty1 |
| Network | Parallels Shared, DHCP. Host bridge `10.211.55.2` |

## Credentials
- `finn` / `finn` (passwordless sudo), `root` / `finn`
- SSH: `ssh finn@<vm-ip>` (port 22, password auth enabled). Mac key is in `authorized_keys`.
- Root SSH is publickey-only (Arch default `prohibit-password`).

## Install flow (from archboot live ISO)
1. Boot `archboot-2026.10.16-...-aarch64-ARCH-latest-aarch64.iso` as `cdrom0`.
2. Exit the Archboot menu to a `bash #` prompt.
3. Serve the repo from the Mac: `python3 /tmp/finnserve.py` (port 8765, GET repo + POST `/log`).
4. In the VM: `curl -sLo /finn-install.sh 10.211.55.2:8765/vm/finn-install.sh && bash /finn-install.sh`
5. Scripts: `finn-install.sh` (partition/format/mount/pacstrap) → `finn-chroot.sh` (config, Fixel, apps, grub).

## Dev workflow
```bash
# host: serve repo + collect VM logs
python3 /tmp/finnserve.py            # 10.211.55.2:8765 -> /Users/abhi/Desktop/FinnOS
tail -f /tmp/finn-vm.log             # script output POSTed by the VM

# push a change and inspect
ssh finn@10.211.55.6 'sudo cp /opt/finn-apps/Desktop.fui /tmp/ && echo ok'
```

## Installed FinnOS payload
- `/usr/local/bin/finn-comp` — compositor (currently a boot placeholder)
- `/usr/local/bin/finnpkg` — `.finn` package manager stub
- `/opt/finn-apps/**` — `Desktop.fui`, `launcher/`, `settings/`, `files/`
- `/usr/share/finn/wallpaper/default.webp` — de-badged fluted gradient
- `/usr/share/fonts/OTF/FixelDisplay-{Regular,Medium,SemiBold,Bold}.otf` — `fc-match "Fixel Display"` resolves
- Autologin on tty1 → `exec /usr/local/bin/finn-comp`

## Next
1. Replace `finn-comp` placeholder with the real DRM/KMS compositor (`compositor/desktop.rules`).
2. Render `ui/Desktop.fui`: cached wallpaper texture + bottom-center dock pill.
3. Implement `finnpkg` `.finn` format + OTA A/B slots.
