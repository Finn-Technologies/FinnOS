# The FinnOS archiso profile.
#
# mkarchiso sources this file, so it is shell, not INI: every value is a
# variable in the caller's namespace, and the arrays are the ones archiso
# documents. Nothing here may run a command — mkarchiso reads it from
# whatever directory it is building and expects it to be a definition, not
# a build step. The build steps are in mkfinniso.

iso_name='finnos'
iso_label='FINNOS'
iso_publisher='Finn Technologies <https://finnos.org>'
iso_application='FinnOS Live/Install Medium'
iso_version="$(date +%Y.%m.%d)"
install_dir='finn'
# mkarchiso requires the profile-relative pacman configuration to be explicit.
pacman_conf='pacman.conf'

# The disc is laid out under /finn, not under /arch: the live root is
# found by the archiso initramfs hook from install_dir, and a disc that
# says FinnOS everywhere but on its own filesystem is a disc that lies.

arch='x86_64'
buildmodes=('iso')

# Both firmwares, because the machine this ships to is not always new
# enough to have UEFI, and not always set to boot it when it does. ArchISO's
# consolidated modes build the BIOS syslinux image and UEFI systemd-boot ESP.
bootmodes=('bios.syslinux' 'uefi.systemd-boot')

# ARM64 boots through UEFI only; there is no BIOS on an ARM board, and
# syslinux does not exist for it. The release target is x86_64, and this
# branch exists so the same profile can be built for the ARM dev machine
# without a second copy of everything.
if [ "${FINN_ARCH:-$(uname -m)}" = 'aarch64' ]; then
  arch='aarch64'
  bootmodes=('uefi.systemd-boot')
fi

# What the live user may run as root. mkarchiso applies these to the files
# it copies into the ISO, so they are only needed for paths whose shipped
# mode is wrong for the disc.
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/etc/gshadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.ssh"]="0:0:700"
  ["/etc/sudoers.d"]="0:0:750"
  ["/usr/local/bin/finn-install"]="0:0:755"
  ["/opt/finnos/vm/finn-install.sh"]="0:0:755"
  ["/opt/finnos/vm/finn-chroot.sh"]="0:0:755"
  ["/opt/finnos/base/mkfinniso"]="0:0:755"
)
