# Peony UI and Design-System Plan

> Implementation status: planned only. FinnOS currently draws a framebuffer diagnostic and has no UI runtime.

## Product principles

1. **State is explicit.** Every asynchronous surface defines loading, empty, offline, permission-denied, partial, and failure states.
2. **Keyboard is first-class.** Every action is reachable without a pointer; focus order is stable and visible.
3. **Capability use is legible.** Apps explain and expose active access to files, devices, location, microphone, camera, and network.
4. **Density follows task.** System chrome remains compact; reading and touch layouts gain space without changing hierarchy.
5. **Motion communicates causality.** Animation never hides latency, blocks input, or substitutes for state.
6. **Software rendering is the baseline.** Correctness and accessibility cannot depend on GPU acceleration.

## Visual direction

Peony should use restrained, high-contrast surfaces with a distinct cool-neutral foundation and one user-selectable accent. Avoid glass effects where they reduce text contrast, excessive rounded cards, decorative gradients behind content, and icon-only primary actions. Hierarchy comes from type, spacing, and surface boundaries before shadows.

## Tokens

### Color

Tokens are semantic, not literal color names. Both themes must meet WCAG 2.2 AA: 4.5:1 normal text, 3:1 large text and UI boundaries, and a non-color focus indicator.

| Token | Light starting value | Dark starting value | Use |
|---|---|---|---|
| `surface.canvas` | `#F4F6F8` | `#11151A` | Desktop/app background |
| `surface.base` | `#FFFFFF` | `#191F26` | Primary content |
| `surface.raised` | `#FFFFFF` | `#222A33` | Menus/dialogs |
| `text.primary` | `#17202A` | `#F2F5F7` | Primary copy |
| `text.secondary` | `#52606D` | `#B8C2CC` | Supporting copy |
| `border.default` | `#C8D0D8` | `#43505C` | Control/surface boundary |
| `accent.default` | `#176B87` | `#58BED6` | Selection/action |
| `status.danger` | `#B42318` | `#FF8A80` | Destructive/error |
| `status.warning` | `#8A5200` | `#FFC45C` | Warning |
| `status.success` | `#16704A` | `#62D6A3` | Success |

Hover, pressed, selected, disabled, and focus states must be derived and contrast-tested for every semantic token. Never encode status by hue alone.

### Typography

Use a bundled, openly licensed variable sans with broad script coverage and a separate monospace family. Text shaping, bidi, fallback, and line breaking are required before claiming localization.

| Role | Size/line height | Weight |
|---|---|---|
| Display | 32/40 | 600 |
| Title | 24/32 | 600 |
| Heading | 20/28 | 600 |
| Body | 16/24 | 400 |
| Compact body | 14/20 | 400 |
| Label | 13/18 | 550 |
| Caption | 12/16 | 400 |

Users must be able to scale text to 200% without clipping, lost actions, or forced two-dimensional scrolling for ordinary content.

### Geometry

- Base spacing unit: 4 px; allowed scale: 4, 8, 12, 16, 24, 32, 48, 64.
- Pointer control minimum: 32x32 logical px; touch target minimum: 44x44.
- Content radius: 6 px; controls: 5 px; dialogs: 10 px. Pills are reserved for tags/status, not general containers.
- Borders: 1 logical px at all scale factors, snapped to device pixels.
- Elevation: at most four levels. Prefer borders; shadows use low-opacity two-layer definitions and never carry sole hierarchy.
- Layout grid: 8 px, with 16 px compact and 24 px regular content margins.

## Components and states

The initial toolkit must include text, icon, button, toggle, checkbox, radio, text field, search, select/menu, list, table, tabs, toolbar, scroll view, split view, dialog, popover, tooltip, progress, notification, and error banner. Every interactive component implements default, hover, pressed, focus-visible, selected, disabled, busy, invalid, and high-contrast states where applicable.

Focus rings are at least 2 logical px with a 2 px offset and remain visible against both adjacent surfaces. Destructive actions require clear labels and confirmation only when undo/recovery is unavailable.

## Window and shell behavior

- Opening, activation, minimization, close, move, resize, snap, modal ownership, and focus-stealing prevention have protocol tests.
- Minimum useful app size is declared by the app; the shell never silently clip-resizes below it.
- Keyboard baseline: `Alt+Tab` task switch, `Alt+F4` close, `Super` launcher, `Ctrl+Alt+T` terminal when installed, directional snap shortcuts, and complete menu traversal.
- A single active window has unambiguous emphasis without reducing inactive text contrast.
- Notifications are queued, actionable by keyboard, grouped by source, and never expose secrets on a locked session.
- Multi-monitor is post-1.0 unless one logical desktop with independent scale/color state is tested; do not fake support by mirroring only.

## Application layout

Apps use command area, navigation, content, and contextual detail only when each has a purpose. At narrow widths, side navigation becomes an explicit drawer or back stack; actions remain in stable semantic order. File manager, terminal, settings, and package UI share common selection, search, permission, progress, undo, and error patterns.

## Motion

- Micro-state transitions: 80-120 ms.
- Panels/windows: 160-220 ms.
- Deceleration for entering, acceleration for exiting; no spring overshoot for system dialogs.
- Input response begins within one frame under target load.
- Reduced-motion mode removes spatial travel and parallax while preserving state changes.
- Animations stop when obscured and cannot gate operation completion.

## Accessibility and localization gates

- WCAG 2.2 AA contrast and keyboard criteria are release minimums.
- Accessibility tree exposes roles, names, values, states, actions, relationships, bounds, focus, and live regions.
- Screen reader, switch control, full keyboard, high contrast, 200% text, reduced motion, and color-vision checks are automated/manual release gates.
- Strings are externalized; layouts support expansion of at least 30%, pluralization, RTL mirroring, and mixed-direction text.
- Pointer, touch, keyboard, and assistive input share actions rather than separate app logic.

## Performance targets

For the reference QEMU software-rendered scene at 1280x800: sustain 60 frames/s during ordinary window movement, keep compositor input-to-present p95 under 50 ms, avoid redrawing undamaged surfaces, and hold idle CPU below 2% of one reference virtual CPU after timers are mature. Targets must be measured and revised from evidence, not silently relaxed.

## Delivery sequence

1. Surface/buffer and input protocols after IPC exists.
2. Software renderer, damage tracking, focus, and three test clients.
3. Font loading/shaping, toolkit primitives, tokens, keyboard navigation, accessibility tree.
4. Shell, launcher, notifications, terminal, settings, and file manager.
5. Visual regression, latency, reduced-motion, localization, and assistive-technology tests.
6. GPU acceleration, multi-monitor, advanced color management, tablet/mobile adaptation only after the baseline is stable.
