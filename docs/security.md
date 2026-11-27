# Security

Privilege boundaries, sockets, update trust, and honest gaps in FinnOS.

## Authority model

- **`finn-comp` runs as root** for DRM/KMS and input device access (`LIBSEAT_BACKEND=seatd`). Its Wayland display socket lives at `/run/finn/wayland-0` (`0660 root:finn`) and its IPC state directory lives at `/run/finn/state` (`0775 root:finn`). It knows no app IDs (`make lint` enforces this).
- **`finn-session` runs as `finn`** on `$XDG_RUNTIME_DIR/finn/session.sock` (`0600`), verifies `SO_PEERCRED` (`cred.uid == getuid() || cred.uid == 0`) on Linux, and spawns apps via `posix_spawnp`. It holds no root privileges and exposes no TCP listener.
- **One-shot OOBE provisioning (`tools/finn-oobe-apply`)**: First-boot setup grants a narrow `/etc/sudoers.d/finn-oobe` rule for `/usr/local/libexec/finn-oobe-apply` only. Upon completion, the helper writes `/var/lib/finn/oobe_complete` and deletes its own sudoers drop-in so it cannot be re-invoked.
- **Installed root account**: `vm/finn-chroot.sh` locks the `root` password (`passwd -l root`) on installed machines. `base/setup.sh` enables `NOPASSWD` sudo only when `FINN_DEV_VM=1` is set explicitly.
- **No bespoke `finnd` daemon**: Standard Linux services (`systemd-logind`, `NetworkManager`, `BlueZ`, `PipeWire`, `pkexec`) handle system actions where available.

## Launch environment hygiene

`finn::compat::build_linux_launch_env()` (`src/compat/linux_sandbox.cpp`) strips dynamic-linker and toolkit plugin hijack variables (`LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT`, `QT_PLUGIN_PATH`, `GTK_PATH`, etc.) before spawning child applications. `.desktop` `Exec=` lines and installer commands are tokenized into explicit `argv` vectors without invoking `/bin/sh`.

## Update, package & image integrity

- **Offline recovery images**: `src/recovery/recovery_engine.cpp` implements FIPS 180-4 SHA-512 verification against `.sha512` sidecars before offline installation.
- **A/B OTA updates (`src/ota/finn-update`)**: Uses pure-Python RFC 8032 Ed25519 signature verification over canonical JSON manifests and SHA-256 verification of downloaded/patched slot images before switching `systemd-boot` or `GRUB` slots, with automatic rollback after failed boots via `finn-boot-health.service`.
- **`.finnapp` packages**: `finn-sdk package` / `verify` support Ed25519 signatures over deterministic archives.
- **First-party `[finnos]` repo (`repo/`)**: Ships out-of-tree Ed25519 GPG keygen and signed `repo-add` publishing scripts.
- **Windows `winget` downloads**: `apps/store/providers/winget_provider.cpp` verifies installer SHA-256 hashes from `microsoft/winget-pkgs` manifests.

## Honest gaps

- **No OS-level native app sandbox**: Native apps and unpacked AppImages run directly with user `finn` privileges (`COMPAT.STATUS` honestly reports `linux_sandbox=env-only`). `finn-app.json` permissions are declarative metadata in v1. Optional `bwrap` namespace wrapping is only planned when `bwrap` is installed and explicitly requested; it is not an enforced security boundary. Sandboxed distribution currently relies on `flatpak --user`.
- **No `ext-session-lock-v1` protocol yet**: `finn-comp` currently coordinates lock requests through `/run/finn/state/lock-request`, and `CompositorServer::session_locked_` is not yet cleared on unlock or gating all compositor input paths.
- **Direct `libinput` open in `finn-comp`**: `startLibinputGestures()` currently calls `::open()` directly on input devices while running as root rather than opening via `seatd`.
- **No Secure Boot, `dm-verity`, or TPM2 sealing**: `finn-boot` does not yet perform Shim/SBAT verification or TPM2 PCR measurement, and root partitions are not `dm-verity`-sealed.
- **Unprovisioned production trust roots**: `base/pkgbuilds/finnos-keyring/finnos.gpg` is currently an empty placeholder, and `ota/finnos-ota-pubkey.pem.example` is an example key; production signing keys and hosted `repo.finnos.org` infrastructure are not yet provisioned.
- **AppImage & guest runtime signatures**: AppImages are not cryptographically signature-verified, and OpenHarmony (`finn-ohos`) / Android (`finn-android`) runtimes do not enforce upstream platform code-signing chains.

## Reporting security issues

Do not open a public GitHub issue for a privilege-boundary or authentication vulnerability. Report it privately to the maintainers.
