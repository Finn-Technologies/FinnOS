# FinnSDK ABI & Versioning Policy

## Current library versions

`CMakeLists.txt` and `src/ui/CMakeLists.txt` build both shared libraries at:
- **`libfinnui.so.1.0.0`** (`SONAME libfinnui.so.1`, `SOVERSION 1`)
- **`libfinnui_qml.so.1.0.0`** (`SONAME libfinnui_qml.so.1`, `SOVERSION 1`, QML module `FinnUI 1.0`)

The root `VERSION` file (`0.2.0`) tracks the overall FinnOS release string and is installed to `/usr/share/finn/version`. `FinnSDKConfigVersion.cmake` uses `SameMajorVersion` compatibility for `find_package(FinnSDK)`.

## Versioning rules

- **Patch (`1.0.x`)**: Bug fixes and internal painting/QML improvements that preserve binary and QML source compatibility.
- **Minor (`1.x.0`)**: Backwards-compatible additions (new widgets, new QML controls, new tokens, new non-virtual methods).
- **Major (`x.0.0`, `SOVERSION` bump)**: Breaking C++ ABI changes (class layout changes, removed symbols, altered virtual tables) or breaking `FinnUI` QML property/signal changes.

## Honest current gaps

- There is no automated `abidiff` / `abi-compliance-checker` gate in CI yet; ABI stability is currently maintained by review discipline and `SOVERSION 1` tracking.
- `libfinnui` and `libfinnui_qml` expose Qt 6 types in public headers, so binary compatibility also depends on the system Qt 6 minor ABI.
