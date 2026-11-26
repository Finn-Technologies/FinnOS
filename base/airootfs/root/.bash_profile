# Booted with the "Install FinnOS" or "FinnOS Recovery" entry.
if grep -qE '(^| )finn\.(install|recovery)($| )' /proc/cmdline 2>/dev/null; then
  # Unattended installs (finn.disk=...) or serial-only logins stay on the CLI.
  if grep -q 'finn\.disk=' /proc/cmdline 2>/dev/null || [ "$(tty 2>/dev/null)" = "/dev/ttyS0" ]; then
    echo
    echo "FinnOS: starting the installer in 3 seconds."
    echo "Press Ctrl-C to stay in the live shell."
    sleep 3
    /usr/local/bin/finn-install
  elif [ -x /usr/local/bin/recovery-mode ]; then
    # Wait briefly for DRM/framebuffer device nodes on slower hardware.
    for _ in 1 2 3 4 5 6; do
      [ -e /dev/dri/card0 ] || [ -e /dev/fb0 ] && break
      sleep 0.5
    done
    if [ -e /dev/dri/card0 ] || [ -e /dev/fb0 ]; then
      [ -w /dev/ttyS0 ] && printf '\nFinnOS Recovery Mode: starting recovery-mode\n' >/dev/ttyS0 2>/dev/null || true
      # Stop Plymouth cleanly before taking DRM/KMS master or framebuffer.
      command -v plymouth >/dev/null 2>&1 && plymouth quit --retain-splash 2>/dev/null || true
      ldconfig 2>/dev/null || true
      rc=0
      # Use linuxfb directly whenever /dev/fb0 exists: eglfs_kms hangs with a black
      # screen ("Failed to create KMS FB!") on GPUs whose DRM plane format mismatches
      # Qt's GBM surface, whereas linuxfb renders reliably on every bare-metal GPU.
      # Redirect stdout/stderr to a log file so Qt warnings never draw console text
      # over the framebuffer.
      export QT_LOGGING_RULES="*.debug=false;qt.qpa.*=false"
      if [ -e /dev/fb0 ]; then
        QT_QPA_PLATFORM=linuxfb /usr/local/bin/recovery-mode >/tmp/recovery-mode.log 2>&1 || rc=$?
      else
        QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_INTEGRATION=eglfs_kms QT_QPA_EGLFS_HIDECURSOR=0 \
          /usr/local/bin/recovery-mode >/tmp/recovery-mode.log 2>&1 || rc=$?
      fi
      if [ "$rc" -eq 42 ]; then
        echo
        echo "FinnOS live shell. Run \"recovery-mode\" to return to Recovery Mode, or \"finn-install\" for CLI."
      elif [ "$rc" -ne 0 ]; then
        echo
        echo "FinnOS: graphical Recovery Mode unavailable (exit $rc); falling back to CLI installer."
        /usr/local/bin/finn-install
      fi
    else
      echo
      echo "FinnOS: no display device found; falling back to CLI installer."
      /usr/local/bin/finn-install
    fi
  else
    echo
    echo "FinnOS: starting the installer in 3 seconds."
    echo "Press Ctrl-C to stay in the live shell."
    sleep 3
    /usr/local/bin/finn-install
  fi
fi
