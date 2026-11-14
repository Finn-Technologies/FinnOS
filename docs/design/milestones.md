# Development milestones

Each milestone tracks shipped behavior verified on the VM and host suite.

## M0–M5 — Compositor, Session, Shell & First-Party Apps
- [x] `finn-comp` on wlroots 0.20 with DRM (`seatd`) & headless backends,
      `xdg-shell`, `wlr-layer-shell`, `wlr-foreign-toplevel-management`,
      screencopy, and on-demand `Xwayland`.
- [x] `finn-session` UNIX socket broker (`SO_PEERCRED`), catalogue +
      `/var/lib/finn/installed`, plus `.desktop` / Flatpak / Wine launch support.
- [x] `libfinnui` (`FinnSDK::FinnUI`) + Qt6 platform theme (`libfinnplatformtheme.so`)
      and style plugin (`libfinnstyle.so`), customized to Figma tokens (`ui/_tokens.fui`)
      and Fixel Display.
- [x] `finn-shell`: Dock, running indicators, Window Switcher, Home menu,
      Spotlight Search + evaluator, Control Center (appearance toggle + real
      UPower/NM/BlueZ/PipeWire state), and floating notifications.
- [x] 7 Native apps (`Settings`, `Finn Files`, `Finn Terminal`, `Finn Notes`,
      `Finn Store`, `Finn Viewer`, `Finn Monitor`) + `finn-wallpaper`.
- [x] Developer SDK (`FinnSDK`, `finn-sdk` CLI, `sdk/template`) and `finn-open`.

## Next Milestones (M6–M7)
- [ ] `finnd` root control socket for privileged machine controls (power, sysfs
      backlight) and allowing `finn-comp` to drop root completely via `seatd`.
- [ ] Per-app transient systemd unit / namespace sandbox isolation.
- [ ] Compositor-level scene backdrop blur behind translucent surfaces.
- [ ] Multi-monitor layout/scaling and OTA A/B update slots.
