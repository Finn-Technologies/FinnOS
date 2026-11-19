# FinnSDK ABI & Versioning Policy

This document describes the current library versioning of `libfinnui` (`FinnSDK`) and the honest limits of ABI compatibility today.

---

## 1. Current Versioning State

From `CMakeLists.txt` and `src/ui/CMakeLists.txt:51-118`:

- **Project / SDK Version**: `0.2.0`
- **Shared Library**: `libfinnui.so.0.2.0` with `SOVERSION 0`
- **CMake Package Compatibility**: `write_basic_package_version_file(... VERSION 0.2.0 COMPATIBILITY SameMajorVersion)`
- **pkg-config**: `finnsdk.pc` / `finnui.pc` report `Version: 0.2.0` (note: `src/ui/finnsdk.pc.in:1` currently hardcodes `prefix=/usr`)
- **Manifest minimum SDK field**: `finn-app.json` includes `sdk_min_version` (default `"0.2.0"`).

---

## 2. Pre-1.0 Compatibility Policy (`0.x`)

While FinnSDK is in `0.x` (`SOVERSION 0`):

1. **Source-level preference**: Minor releases (`0.2.x` -> `0.3.x`) may add new headers, classes, enum values, or non-virtual member functions. Breaking API or ABI changes bump the minor version (`0.x.0`) until `1.0.0`.
2. **Patch releases (`0.2.x`)**: Intended to remain binary-compatible within the same minor release whenever possible, though **no automated ABI checker enforces this yet**.
3. **C++20 + Qt6 ABI coupling**: `libfinnui` exposes `QWidget`/`QObject` subclasses (`FinnWindow`, `FinnApplication`, `FinnCard`, etc.) directly in public headers (`src/ui/finn/*.hpp`). Therefore, a compiled app's ABI depends on:
   - Matching **Qt6 major.minor** ABI (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6Svg`)
   - Matching **C++ standard library** (`libstdc++` / `libc++` C++20 ABI)
   - Matching **FinnOS `libfinnui.so.0`** build.
4. **Pure-C++ headers**: `ui/finn/animation.hpp` and `ui/finn/spring.hpp` are header-only and Qt-free.

---

## 3. What Is Not Enforced Yet

- **No ELF symbol version script** (`.map` / `FINNUI_0.2` symbol versioning) in `src/ui/CMakeLists.txt`.
- **No `-fvisibility=hidden` export macro discipline** across `src/ui/finn/*.hpp`.
- **No automated ABI diff gate in CI** (`abidiff` / `abi-compliance-checker`).
- **No runtime `sdk_min_version` loader check** when launching binaries (currently validated statically by `finn-sdk validate` on `finn-app.json`).

Until `1.0.0`, third-party developers distributing compiled `.finnapp` binaries should rebuild against the target FinnOS release's `FinnSDK` headers and Qt6 version.
