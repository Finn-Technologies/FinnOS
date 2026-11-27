# FinnSDK (`sdk/`)

Developer SDK assets, manifest schema (`sdk/schema/finn-app.schema.json`), starter template (`sdk/template/`), and host SDK tests (`sdk/tests/test_sdk.py`).

## Quickstart

```sh
finn-sdk check
finn-sdk new hello --name "Hello Finn"
finn-sdk validate hello/finn-app.json
finn-sdk build hello
finn-sdk run hello
```

## Native app chrome & libraries

- **`FinnSDK::FinnUIQml` (`import FinnUI 1.0`, `libfinnui_qml.so.1.0.0`)**: Recommended for everyday apps — 19 AOT-compiled QML controls including adaptive `FinnApplicationWindow` (Desktop split-pane + 88×28 window controls pill, Tablet NavRail, Mobile bottom nav, TV 10-foot scale), `FinnTheme`, `FinnSpringValue`, `FinnSquircle`, and `image://finn-icon/`.
- **`FinnSDK::FinnUI` (`<FinnSDK/FinnSDK.hpp>`, `libfinnui.so.1.0.0`)**: 29 C++20 Qt 6 widget classes (`FinnApplication`, `FinnWindow`, `FinnLayerWindow`, `SpringAnimator`, `SessionClient`).

## Compatibility tiers

1. **Native FinnOS** (`FinnSDK::FinnUIQml` / `FinnSDK::FinnUI`, `.finnapp`)
2. **Linux** (`.desktop`, Flatpak, AppImage)
3. **Windows** (`finn-win`, Wine/Proton/Hangover ARM64 WoW64)
4. **macOS** (`finn-darwin`, clean-room partial runtime)
5. **OpenHarmony 6.1 LTS** (`finn-ohos`, clean-room partial runtime)
6. **Android 17** (`finn-android`, clean-room APK/AXML/DEX inspector & layout previewer)

See [docs/sdk.md](../docs/sdk.md), [docs/sdk/getting-started.md](../docs/sdk/getting-started.md), and [artifacts.md](artifacts.md).
