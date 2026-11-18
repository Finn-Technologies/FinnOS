# Rebrand and Attribution Checklist

Tracks FinnOS Phase 4 rebrand status, upstream hooks intentionally kept, branding
leaks fixed (and remaining leaks outside this workstream's ownership), dev
tooling references, the `ID_LIKE=arch` decision, and `UNVERIFIED` license
entries from [NOTICE](../NOTICE).

---

## 1. Decision on `ID_LIKE=arch` (`base/airootfs/etc/os-release:4`)

**Decision: Keep `ID_LIKE=arch`.**

Per `os-release(5)`:
- `NAME="FinnOS"` and `PRETTY_NAME="FinnOS"` (`base/airootfs/etc/os-release:1-2`)
  are the user-facing identity strings.
- `ID=finnos` (`base/airootfs/etc/os-release:3`) is the machine-readable OS ID.
- `ID_LIKE=arch` is a non-branded machine-readable family hint used by tools
  (package helpers, diagnostic scripts, Flatpak/portal integration) to know
  that the base userland uses `pacman` and Arch-compatible package layouts.

Because FinnOS still boots an Arch-derived package set (`[core]` / `[extra]`
fallback in `base/pacman.conf` and `pacman` / `archiso`), removing `ID_LIKE=arch`
would misrepresent the system ABI/packaging family to tooling. `ID_LIKE=arch`
should be removed only if and when FinnOS replaces upstream Arch package
repositories and packaging conventions entirely.

---

## 2. Repository URL Fix (Finding M4)

- `git remote -v` points to `https://github.com/Finn-Technologies/FinnOS.git`.
- `base/airootfs/etc/os-release:11` previously used
  `BUG_REPORT_URL="https://github.com/finnos/finnos/issues"`.
- Fixed to `BUG_REPORT_URL="https://github.com/Finn-Technologies/FinnOS/issues"`.

---

## 3. Arch Reference Inventory (3 Groups)

### Group A — Functional upstream hooks kept (with reason)

These references are functional runtime, initramfs, bootloader, or package-manager
hooks that cannot be renamed while FinnOS builds from `archiso` and `pacman`.

| Surface | Location (`file:line`) | Hook / Identifier | Reason kept |
|---|---|---|---|
| `os-release` | `base/airootfs/etc/os-release:4` | `ID_LIKE=arch` | Machine-readable packaging family hint per `os-release(5)` |
| Syslinux boot params | `base/boot/syslinux/syslinux.cfg:31,40` | `archisobasedir=finn archisolabel=FINNOS` | Required by `mkinitcpio-archiso` initramfs hook to locate the squashfs live root |
| systemd-boot entries | `base/boot/loader/entries/01-finnos-install.conf:16`, `02-finnos-live.conf:8` | `archisobasedir=finn archisolabel=FINNOS` | Required by `mkinitcpio-archiso` initramfs hook on UEFI boot |
| GRUB ISO menu | `base/boot/grub/grub.cfg:34` | `archisobasedir=finn archisolabel=FINNOS` | Required by `mkinitcpio-archiso` initramfs hook on manual GRUB ISO boot |
| Live initramfs config | `base/airootfs/etc/mkinitcpio.conf:33` | `HOOKS=(... archiso archiso_loop_mnt ...)` | Boots live ISO squashfs root |
| Keyring init unit | `base/airootfs/etc/systemd/system/pacman-init.service:18,23-24` | `Before=... archlinux-keyring-wkd-sync.service`, `pacman-key` | Unit ordering dependency on upstream keyring package's systemd unit (not user-visible) |
| Keyring tmpfs mount | `base/airootfs/etc/systemd/system/etc-pacman.d-gnupg.mount:1-13` | `/etc/pacman.d/gnupg` | Writable tmpfs keyring for live ISO `pacman-key` |
| ISO profile & builder | `base/profiledef.sh:1,16`, `base/mkfinniso:441-526` | `mkarchiso`, `archiso`, `mkinitcpio-archiso` | Live ISO builder and required initramfs hook packages |
| Setup & installer scripts | `base/setup.sh:44-76`, `vm/finn-install.sh:235-622`, `vm/finn-chroot.sh:2,88-108` | `pacman`, `pacstrap`, `arch-chroot`, `arch-install-scripts` | Package manager and chroot bootstrap on target disk |
| Shell live-ISO detector | `src/shell/main.cpp:214` | `QFileInfo::exists("/run/archiso")` | Detects live ISO session at runtime |
| Pacman config | `base/pacman.conf`, `base/pacman.multilib.conf` | `pacman` directives, `[core]`/`[extra]` fallback | Managed by `@repo` (`[finnos]` repo + upstream fallback) |
| Store / compat helpers | `apps/settings/main.cpp:918`, `apps/store/main.cpp:848`, `src/compat/android_runner.cpp:46` | `pacman -Qu`, `pacman -S waydroid` | Invokes actual `pacman` binary on host |

### Group B — Branding leaks fixed (and remaining leaks owned elsewhere)

#### Fixed in this change (Findings M1, M2, M4, m1, m4)
| Finding | Location (`file:line`) | Before | After |
|---|---|---|---|
| M4 | `base/airootfs/etc/os-release:11` | `BUG_REPORT_URL="https://github.com/finnos/finnos/issues"` | `https://github.com/Finn-Technologies/FinnOS/issues` |
| M2 | `apps/flourish/tab_model.cpp:48-49` | `https://wiki.archlinux.org` / `"ArchWiki"` | `https://finnos.org/docs/` / `"FinnOS Docs"` |
| M2 | `apps/flourish/intro_overlay.cpp:141,159` | `"wiki.archlinux.org"` / `"ArchWiki"` | `"finnos.org/docs"` / `"FinnOS Docs"` |
| M1 | `README.md:3,19` | `"built on Arch Linux"`, `"build on Arch Linux or the Lima dev VM"` | `"built on a minimal Linux base"`, `"build on a pacman-based Linux host or in the Lima dev VM"` |
| M1 | `CONTRIBUTING.md:17` | `"The build machine is Arch or the dev VM."` | `"The build machine is a pacman-based Linux host (such as the dev VM)."` |
| M1 | `base/README.md:3,14` | `"Arch Linux base"`, `"turns an Arch installation into FinnOS"` | `"Linux system base"`, `"configures a pacman-based rootfs for FinnOS"` |
| M1 | `docs/install.md:3,9` | `"Arch Linux provides..."`, `"configures an Arch Linux installation"` | `"The base Linux system provides..."`, `"configures a pacman-based root filesystem"` |
| M1 | `docs/development.md:4,16,44` | `"Arch Linux machine"`, `"Arch dependencies"`, `"missing Arch packages"` | `"pacman-based Linux host"`, `"Package dependencies"`, `"missing packages"` |
| M1 | `docs/hardware.md:17` | `"Arch Linux ARM64 has no native x86 wine package"` | `"Upstream aarch64 package repositories have no native x86 wine package"` |
| M1 | `docs/security.md:37` | `"rely on Arch's pacman signing"` | `"rely on upstream pacman signing"` |
| M1 | `docs/testing.md:35,58` | `"Arch + Qt6"`, `"Arch Linux ARM64"` | `"a pacman-based Linux host + Qt6"`, `"aarch64"` |
| m4 | `vm/README.md:25,40` | `"Arch Linux ARM under QEMU"`, `"Arch Linux ARM cloud image"` | `"upstream aarch64 cloud image under QEMU ... dev-only bootstrap tooling"`, `"upstream ARM64 cloud image"` |
| m1 | `.gitignore:15-29` | Missing binary ignores for additional apps & unit tests | Added `/apps/*/finn-*`, `/tests/unit/test_spring`, `/tests/unit/test_ota` (alongside existing `test_search`, `test_compat`, `test_sys_files`) |

#### Remaining references outside `@rebrand` ownership (reported to `@coordinator` / owners)
| Finding | Location (`file:line`) | Text | Owner |
|---|---|---|---|
| M1 (app UI) | `apps/settings/main.cpp:974` | `"Linux Applications (Arch / Flatpak / AppImage)"` | App UI (`@coordinator` to assign) |
| M1 (app UI) | `apps/store/main.cpp:851` | `"=== Arch Packages (pacman -Qu) ==="` | App UI (`@coordinator` to assign) |
| m2 (comments) | `base/packages.x86_64:7-10,37,50,103` | Comments naming `"Dell OptiPlex 3020"` and `"general Arch x86_64 profile"` | `@repo` |
| M1 (contract) | `docs/design/contract.md:8` | `"Arch Linux: the kernel, firmware, systemd..."` (explicitly marked engineering-base exception on lines 15–16) | `@design` |
| B3 (fallback) | `vm/finn-install.sh:256,287,504` | Fallback comments/env (`FINN_ARCH_FALLBACK_MIRROR`) | `@repo` |

### Group C — Dev tooling & CI references (kept as functional build infrastructure)

| Location (`file:line`) | Reference | Purpose |
|---|---|---|
| `vm/lima-setup.sh:10,50-51` | `Arch Linux ARM`, `archlinuxarm-keyring`, `pacman-key --populate archlinuxarm` | Bootstraps the 2022 `aarch64` Lima QEMU cloud image for local macOS builds |
| `vm/lima.yaml:5,15` | `Arch-Linux-aarch64-cloudimg-20220323.0.qcow2` | Upstream `aarch64` cloud image URL for Lima dev VM |
| `vm/test-boot.sh:47` | `archisobasedir=finn archisolabel=FINNOS` | QEMU ISO boot harness |
| `vm/create-dev-vm.sh:7,9` | Comments referencing Arch container / stock-Arch | Dev VM script comments |
| `.github/workflows/ci.yml:28`, `runtime.yml:22`, `iso.yml:84` | `archlinux:base-devel`, `archlinux:latest` | GitHub Actions CI build containers |
| `sdk/template/.github/workflows/ci.yml:30` | `container: archlinux:base-devel` | Third-party SDK CI container |
| `scripts/build-runtime-pkg.sh:9-34` | `makepkg` / `archlinux` container error/comments | Runtime `.pkg.tar.zst` builder |

---

## 4. `UNVERIFIED` Licenses and Provenance (`NOTICE` Finding B4)

Per rule: no license was guessed. Items without verified in-tree license files
or embedded metadata are marked `UNVERIFIED` in [NOTICE](../NOTICE):

| Item | Where | Why unverified on macOS host | Action needed |
|---|---|---|---|
| `assets/wallpaper/default.webp`, `assets/wallpaper/setup.webp` | In-tree assets | No EXIF/XMP/copyright metadata in WebP files | Confirm author/Figma export provenance or replace with documented license |
| `assets/models/finn_lamp_lit.glb`, `assets/models/finn_lamp_unlit.glb` | In-tree assets | Blender glTF export has no copyright/license metadata | Confirm FinnOS authorship and record in `NOTICE` |
| Unlisted `assets/icons/shell/*.svg`, `assets/icons/apps/*.svg`, `assets/pointers/*.svg` | In-tree assets | `LICENSE-hugeicons.md` names only `display.svg` and `moon.svg`; several other SVGs use HugeIcons-style names (`-01`, `-02`, `-09`) | Audit each SVG against HugeIcons vs. first-party Figma export |
| Distribution packages in `base/packages.x86_64`, `base/packages.aarch64` | Base image | Exact `/usr/share/licenses/<pkg>/` files (especially `linux-firmware`, `intel-ucode`, `amd-ucode`, `ttf-dejavu`, `noto-fonts*`, `wine*`, `networkmanager`, `bluez`, `upower`, `flatpak`) only exist on a built rootfs | Run a license harvest from `/usr/share/licenses/` inside the built VM/ISO and reconcile `NOTICE` |

---

## 5. Reproducible Check Proof (`tools/check-branding.sh`)

Command run from repo root:

```sh
./tools/check-branding.sh
```

Output:

```text
branding: clean (boot/identity, apps/src/sdk, README/docs, os-release)
```
