# Peony

> Status: Accepted architectural direction
> Implementation: Software-rendered desktop slice in progress; bounded VirtIO-GPU control and 2D presentation smoke with owned display-buffer mapping verified separately

Peony is FinnOS’s native graphical and application platform, not merely a theme or desktop skin. It includes Peony Display, Peony Shell, Peony Framework, graphics and text rendering, input routing, accessibility, design system, adaptive application scenes, and desktop, tablet, mobile, and converged environments.

Peony is native to FinnOS rather than Wayland, X11, Flutter, Android Views, UIKit, or another imported application framework. The current slice provides a `no_std` canvas, alpha-aware primitives, a fixed z-order window compositor, software cursor, shell widgets, core desktop applications, bounded `DamageRegion` tracking, and canvas clipping. A separate kernel-side VirtIO-GPU control query, five-command 2D setup, and two-command Peony damage follow-up are verified in QEMU, but they are not yet connected to a continuous Peony surface-presentation service. Full compositor protocols, accessibility semantics, text shaping/localization, multi-process surfaces, and application APIs remain in progress.

Damage is clipped to display bounds and coalesced to a fixed-capacity set. Compositor changes may request a full display for shell/modal transitions or a smaller region for cursor, window, and taskbar changes. The semantic keyboard contract and baseline shell shortcut routing are host-tested, but no hardware keyboard service is connected yet. The GPU display-buffer policy checks dimensions, page ownership, and a conservative four-level table-page bound; both desktop paths map the owned range, render it, submit the bounded GPU session, copy the result to GOP, and retain the backing for device lifetime. This is not continuous compositor rendering or acceleration evidence.
