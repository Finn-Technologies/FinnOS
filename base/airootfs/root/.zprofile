# Booted with the "Install FinnOS" entry of the install disc.
if grep -qw finn.install /proc/cmdline 2>/dev/null; then
  echo
  echo "FinnOS: starting the installer in 10 seconds."
  echo "Press Ctrl-C to stay in the live shell."
  sleep 10
  /usr/local/bin/finn-install
fi
