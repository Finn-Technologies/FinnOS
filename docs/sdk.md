# FinnSDK

`FinnSDK` is FinnOS's C++20 and Qt 6 / QML application SDK, providing shared UI runtimes, design tokens, analytic spring physics, Qt 6 system plugins, CMake/pkg-config packaging, a starter template, and the `finn-sdk` developer CLI.

## Libraries & plugins

| Artifact | Target / Package | Role |
|---|---|---|
| `libfinnui.so.1.0.0` (`SOVERSION 1`) | `FinnSDK::FinnUI` / `finnsdk.pc` | 29 C++20 Qt 6 widgets, `FinnApplication`, `FinnWindow`, `FinnLayerWindow`, `FinnThemeManager`, `SpringAnimator`, `AdaptiveFormFactor`, `SessionClient` |
| `libfinnui_qml.so.1.0.0` (`SOVERSION 1`) | `FinnSDK::FinnUIQml` | `FinnUI 1.0` QML module: 19 AOT-compiled controls (`FinnApplicationWindow`, `FinnGlassPanel`, `FinnButton`, etc.), `FinnThemeBridge`, `FinnSpringValue`, `FinnSquircle`, `image://finn-icon/` |
| `libfinnplatformtheme.so` | `QT_QPA_PLATFORMTHEME=finn` | Live palette, Fixel Display fonts, SVG icon theme, reduced-motion preference |
| `libfinnstyle.so` | `QT_STYLE_OVERRIDE=finn` | Custom `QProxyStyle` with tokenized radii, hairlines, sliders, and scrollbars |

*(Note: `CMakeLists.txt` and `src/ui/CMakeLists.txt` build `VERSION 1.0.0` / `SOVERSION 1`; the root `VERSION` file tracks the OS release string `0.2.0`.)*

## `finn-sdk` CLI (`tools/finn-sdk`)

```sh
finn-sdk check                              # verify headers, libs, Qt 6 plugins, CMake package
finn-sdk new myapp --name "My App"          # scaffold from sdk/template/
finn-sdk validate myapp/finn-app.json       # validate against sdk/schema/finn-app.schema.json
finn-sdk build myapp                        # CMake + Ninja build
finn-sdk run myapp                          # launch with FinnOS Qt 6 theme env
finn-sdk register myapp                     # register in ~/.local/share/finn/apps/
finn-sdk keygen                             # generate Ed25519 signing keypair
finn-sdk package myapp                      # create deterministic signed .finnapp archive
finn-sdk verify myapp.finnapp               # verify signature and manifest
finn-sdk simulate --form-factor desktop|tablet|phone|tv myapp
```

## Compatibility tiers on FinnOS

1. **Native FinnOS (`FinnSDK::FinnUIQml` / `FinnSDK::FinnUI`)** — First-class Qt 6/QML and C++20 apps following live tokens, spring physics, and adaptive form factors.
2. **Linux apps (`.desktop`, Flatpak, AppImage)** — Native Wayland execution with FinnOS Qt 6 theme injection (`docs/linux-app-coverage.md`).
3. **Windows apps (`.exe`, `.msi`)** — Executed via `finn-win` + Wine/Proton/Hangover ARM64 WoW64 (`docs/windows-app-coverage.md`).
4. **macOS apps (`.app`, `.pkg`, `.dmg`)** — Clean-room partial runtime `finn-darwin` (`docs/darwin-framework-coverage.md`).
5. **OpenHarmony 6.1 LTS (`.hap`, `.hsp`, `.app`)** — Clean-room partial runtime `finn-ohos` (`docs/ohos-kit-coverage.md`).
6. **Android 17 (`.apk`, `.xapk`, `.apkm`, `.apks`)** — Clean-room inspector & AXML layout previewer `finn-android` (`docs/android-coverage.md`).

## Further reading

- [sdk/getting-started.md](sdk/getting-started.md)
- [sdk/api-overview.md](sdk/api-overview.md)
- [sdk/abi-versioning.md](sdk/abi-versioning.md)
- [sdk/publishing.md](sdk/publishing.md)
- [sdk/simulator.md](sdk/simulator.md)
