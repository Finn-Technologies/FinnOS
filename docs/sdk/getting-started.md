# Getting Started with FinnSDK

FinnSDK is the C++20 and Qt6 application SDK for FinnOS (`libfinnui` / `FinnSDK::FinnUI`). Native apps run directly on `finn-comp` over Wayland with FinnOS custom CSD (`FinnWindow`), live theme tokens (`FinnThemeManager`), and the `finnplatformtheme` + `finnstyle` Qt6 plugins.

## 1. Check your environment

On a FinnOS machine or development VM, verify the toolchain and runtime plugins:

```sh
finn-sdk check
```

Expected dependencies for building and packaging:
- `cmake`, `ninja`, `c++` (C++20 compiler), `pkg-config`
- `FinnSDKConfig.cmake` (`find_package(FinnSDK REQUIRED)`)
- `libfinnplatformtheme.so`, `libfinnstyle.so`, `liblayer-shell.so`
- `openssl` and `zstd` (for `.finnapp` packaging and Ed25519 signing)
- `/run/finn/wayland-0` (for running GUI apps against `finn-comp`)

## 2. Scaffold a new app

```sh
finn-sdk new notes-lite --name "Notes Lite"
cd notes-lite
```

This creates:
- `CMakeLists.txt` — links `FinnSDK::FinnUI` with C++20 and `CMAKE_AUTOMOC ON`
- `Makefile` — convenience wrapper around CMake + Ninja
- `main.cpp` — starter `FinnApplication` + `FinnWindow` + `FinnButton` + `FinnToast`
- `finn-app.desktop` — Freedesktop entry (`X-Finn-Id=notes-lite`)
- `finn-app.json` — FinnOS application manifest validated against `sdk/schema/finn-app.schema.json`
- `.github/workflows/ci.yml` — GitHub Actions workflow template for manifest validation, build, and `.finnapp` packaging

You can also copy `sdk/template/` directly (which includes a richer `FinnSidebar` + `FinnCard` + `FinnToggle` counter example).

## 3. Validate the manifest

```sh
finn-sdk validate .
```

After compiling, run strict validation to verify the compiled executable and referenced local files exist:

```sh
finn-sdk validate . --strict
```

See [Publishing & Packaging](publishing.md) for the full `finn-app.json` schema and permission declarations.

## 4. Build and run

```sh
finn-sdk build .
finn-sdk run .
```

`finn-sdk run` sets:
- `WAYLAND_DISPLAY=/run/finn/wayland-0` (if unset)
- `QT_QPA_PLATFORM=wayland;xcb` (if unset)
- `QT_QPA_PLATFORMTHEME=finn`
- `QT_STYLE_OVERRIDE=finn`

To test phone, tablet, or desktop form-factor dimensions, see [Simulator](simulator.md):

```sh
finn-sdk simulate --form-factor tablet --headless .
```

## 5. Register locally in the launcher

Register the compiled binary in `$XDG_DATA_HOME/applications/finn-<id>.desktop` (default `~/.local/share/applications/`) so `finn-session`, Home Menu, and Spotlight Search discover it:

```sh
finn-sdk register notes-lite "Notes Lite" "$PWD/build/finn-notes-lite" native-apps "Lightweight notes"
```

## 6. Package a `.finnapp` archive

For local development (unsigned):

```sh
finn-sdk package . --unsigned
finn-sdk verify dist/notes-lite-0.1.0-x86_64.finnapp --allow-unsigned
```

For signed distribution, generate an Ed25519 keypair outside your repository and pass `--key`:

```sh
finn-sdk keygen ~/.config/finn-dev/publisher
finn-sdk package . --key ~/.config/finn-dev/publisher.key.pem
finn-sdk verify dist/notes-lite-0.1.0-x86_64.finnapp --pubkey ~/.config/finn-dev/publisher.pub.pem
```

See [Publishing & Packaging](publishing.md) for details.
