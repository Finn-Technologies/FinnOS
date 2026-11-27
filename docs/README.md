# Documentation

## Core Architecture & Design
| Page | Answers |
|---|---|
| [architecture.md](architecture.md) | What runs, what owns what, boot chain, compatibility, and honest gaps |
| [code-map.md](code-map.md) | Module tour, IPC & launch flows, and where to change what |
| [design/contract.md](design/contract.md) | Normative system contract, authority boundaries, app contract, and non-goals |
| [design/cpp-guide.md](design/cpp-guide.md) | C++20 + Qt 6/QML coding rules and ownership conventions |
| [design/milestones.md](design/milestones.md) | Shipped milestones and current backlog |
| [design.md](design.md) | Figma frames (`FinnOS - Abhi`), key measurements, and Figwright |
| [design-language.md](design-language.md) | Unified design tokens, breakpoints, typography, spacing, radii, and component states |
| [motion.md](motion.md) | Live motion durations, spring presets, per-surface motion, and reduced motion |
| [motion-spec.md](motion-spec.md) | Analytic spring solver mathematics, presets, and 60/120 Hz budgets |
| [oobe-mobile-design.md](oobe-mobile-design.md) | 10-step OOBE flow design and adaptive form-factor notes |
| [reference-designs.md](reference-designs.md) | External OS design research and what FinnOS adopts, adapts, or declines |

## SDK, Apps & Protocols
| Page | Answers |
|---|---|
| [apps.md](apps.md) | Writing native Qt 6/QML & C++20 apps with `FinnSDK` |
| [sdk.md](sdk.md) | `FinnSDK` overview, `libfinnui` + `libfinnui_qml`, Qt 6 plugins, and `finn-sdk` CLI |
| [sdk/getting-started.md](sdk/getting-started.md) | Step-by-step `finn-sdk` walkthrough |
| [sdk/api-overview.md](sdk/api-overview.md) | C++20 widget & QML `FinnUI 1.0` API reference |
| [sdk/abi-versioning.md](sdk/abi-versioning.md) | `libfinnui` / `libfinnui_qml` SOVERSION & ABI policy |
| [sdk/publishing.md](sdk/publishing.md) | `finn-app.json` schema, `.finnapp` archive format, and Ed25519 signing |
| [sdk/simulator.md](sdk/simulator.md) | `finn-sdk simulate` presets and `AdaptiveFormFactor` |
| [flourish.md](flourish.md) | Optional Chromium web browser (`apps/flourish`) |
| [session-protocol.md](session-protocol.md) | Exact line-oriented `finn-session` socket protocol and launch environment |

## Compatibility Coverage
| Page | Answers |
|---|---|
| [linux-app-coverage.md](linux-app-coverage.md) | Native Linux `.desktop`, Flatpak, AppImage, ELF probe, and launch environment |
| [windows-app-coverage.md](windows-app-coverage.md) | Windows PE/ICO/MSI/registry/shortcut support via `finn-win` / Wine / Proton / Hangover |
| [darwin-framework-coverage.md](darwin-framework-coverage.md) | Clean-room macOS (`finn-darwin`) Mach-O, ObjC, Foundation, AppKit-on-Qt6, and SwiftUI coverage |
| [ohos-kit-coverage.md](ohos-kit-coverage.md) | Clean-room OpenHarmony 6.1 LTS (`finn-ohos`) `.hap`, Panda VM, ArkUI-on-Qt6, and NAPI coverage |
| [android-coverage.md](android-coverage.md) | Clean-room Android 17 (`finn-android`) APK/AXML/DEX inspection, layout previewer, and limits |

## Platform, Operations & Development
| Page | Answers |
|---|---|
| [install.md](install.md) | Live ISO, offline Recovery Mode installer, `base/setup.sh`, packages, and systemd units |
| [ota.md](ota.md) | A/B OTA architecture, Ed25519 signing, zstd deltas, boot backends, and `finn-update` CLI |
| [ota-publishing.md](ota-publishing.md) | OTA key custody, `ota/publish`, GitHub Releases layout, and staged rollouts |
| [repo.md](repo.md) | First-party `[finnos]` pacman repository signing, publishing, and upstream closure |
| [development.md](development.md) | Build dependencies, dev loop, Lima/VMware VM workflow, and logs |
| [emulator-quickstart.md](emulator-quickstart.md) | `finn-emulator` host probe and simulation CLI |
| [platform-tooling.md](platform-tooling.md) | `x86_64` / `aarch64` build matrix, toolchain, and CI jobs |
| [testing.md](testing.md) | Host Python suite, ASan/UBSan unit tests, VM checks, and untested gaps |
| [performance.md](performance.md) | Measured blur kernel microbenchmark (`make bench`) and startup instrumentation |
| [security.md](security.md) | Privilege boundaries, sockets, OTA/repo trust, and the sandbox gap |
| [hardware.md](hardware.md) | Verified VM environments and honest bare-metal hardware limits |
| [guest-tools.md](guest-tools.md) | VMware Tools, VirtualBox Guest Additions, and QEMU Guest Agent integration |
| [troubleshooting.md](troubleshooting.md) | Diagnosing boot, display, session, plugin, font, and capture issues |
| [roadmap.md](roadmap.md) | Shipped milestones and what remains next |
| [rebrand-checklist.md](rebrand-checklist.md) | Upstream base hooks, branding checks, and third-party licence status |

## Historical Audits & Point-in-Time Snapshots
These documents record earlier milestones and audits (many pre-dating the Qt 6 `QWaylandQuickCompositor` and QML shell rewrite) and are preserved as historical context:

- [audit/code-audit.md](audit/code-audit.md), [audit/ui-audit.md](audit/ui-audit.md), [audit/daily-driver.md](audit/daily-driver.md), [audit/remaining-issues.md](audit/remaining-issues.md)
- [figma-audit.md](figma-audit.md), [figma-inventory.md](figma-inventory.md), [figma-live-drift.md](figma-live-drift.md)
- [rewrite-assessment.md](rewrite-assessment.md), [sdk/audit.md](sdk/audit.md)
