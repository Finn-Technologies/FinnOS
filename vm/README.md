# FinnOS dev VM

A working Arch Linux ARM (aarch64) install that boots FinnOS, running in
Parallels on Apple Silicon. It is the build machine: the compositor needs
the DRM headers, GBM, EGL and FreeType, and this is where the shell is
actually run.

## The VM

| | |
|---|---|
| Name | `FinnOS-dev` |
| Guest | Arch Linux ARM aarch64 (core: `linux-aarch64 7.2.10`) |
| Resources | 4 GB RAM, 2 vCPU, 32 GB disk — the i3-4130 / 4 GB target |
| Disk | `/dev/sda1` 512M EFI (vfat) + `/dev/sda2` 31.5G root (ext4, label `finnos`) |
| Bootloader | GRUB `arm64-efi`, bootloader-id `FinnOS` (removable) |
| Console | Parallels VM window, autologin `finn` on tty1 |
| Network | Parallels Shared, DHCP. Host bridge `10.211.55.2` |
| GPU | Apple M4 through virgl — the accelerated path, GBM + EGL 1.5 |

## Credentials

- `finn` / `finn` (passwordless sudo), `root` / `finn`
- SSH: `ssh finn@10.211.55.6` (port 22, password auth enabled). The Mac's
  key is in `authorized_keys`.
- Root SSH is publickey-only (the Arch default, `prohibit-password`).

## The day-to-day loop

```sh
make deploy                  # build, install, restart
vm/shot.sh /tmp/shot.png     # look at it
```

`vm/deploy.sh` sends the tree over as a tar stream — the VM has no
`rsync` — builds it in `~/finnos`, installs it to `/usr/local/bin` and
restarts `finn-comp` and `finnd`.

To drive the shell from a script:

```sh
ssh finn@10.211.55.6 '
  sudo finnctl pointer.move x=1196 y=1506
  sudo finnctl pointer.click
  sleep 1
  sudo finnctl screen.shell
'
```

## Installing a fresh VM

1. Boot an Arch Linux ARM ISO as `cdrom0`.
2. Exit the boot menu to a `bash #` prompt.
3. Serve the repository from the Mac:

   ```sh
   cd /Users/abhi/Desktop/FinnOS
   tar -czf /tmp/finnos.tar.gz --exclude=.git --exclude='*.o' --exclude='*.a' .
   python3 -m http.server 8765 --directory /tmp --bind 10.211.55.2
   ```

   The installer fetches the scripts and the payload from that server.
4. In the VM:

   ```sh
   curl -sLo /finn-install.sh 10.211.55.2:8765/vm/finn-install.sh
   bash /finn-install.sh
   ```

   `vm/finn-install.sh` partitions, formats, pacstraps and runs
   `vm/finn-chroot.sh`, which configures the machine, installs the fonts
   and the payload, and sets up the bootloader.
5. Build the compositor on the machine and install it:

   ```sh
   cd /opt/finnos && make && sudo make install
   sudo systemctl restart finn-comp finnd
   ```

## What is on the machine

- `/usr/local/bin/finn-comp` — the compositor
- `/usr/local/bin/finnd`, `finnctl` — the control plane and its client
- `/usr/local/bin/finnpkg` — the package manager
- `/usr/share/finn/wallpaper/default.webp` — the wallpaper
- `/usr/share/finn/icons/*.svg` — the icon set
- `/usr/share/finn/apps.registry` — the catalogue of apps
- `/usr/share/fonts/OTF/FixelDisplay-*.otf` — `fc-match "Fixel Display"` resolves
- `/run/finn/` — the IPC namespace: `control.sock`, `comp.sock`, `state/`

## The services

```sh
systemctl status finn-comp finnd
sudo journalctl -u finn-comp -f
sudo tail -f /var/log/finn/control.log
```

The compositor writes to the journal, never to the TTY it is painting on.
Every control-plane call is audited with the peer uid.

## Making one

`vm/create-dev-vm.sh` creates an empty Parallels VM with the right shape
for the target. The ISO is attached by hand.
