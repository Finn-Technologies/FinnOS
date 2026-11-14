# FinnSDK

FinnOS native applications are **C++20 + Qt6** Wayland clients styled with
FinnOS's custom platform integration (`QT_QPA_PLATFORMTHEME=finn`,
`QT_STYLE_OVERRIDE=finn`) and built on `libfinnui` (`FinnSDK::FinnUI`).

Native FinnOS apps get first-class integration:
- Custom frameless rounded Wayland CSD via `FinnWindow` with the Figma `83x24`
  Minimize / Maximize / Close pill, collapsible sidebar, and native system move/resize.
- Live Dark Mode, Accent Color, and Reduced Motion updates via `FinnThemeManager`.
- Cohesive custom components (`FinnCard`, `FinnButton`, `FinnToggle`, `FinnSlider`,
  `FinnSearchField`, `FinnSidebar`, `FinnSettingRow`, `FinnProgressBar`, `FinnToast`,
  `FinnDialog`, `FinnEmptyState`) matching `ui/_tokens.fui`.
- Desktop/layer surfaces via `FinnLayerWindow` (`LayerShellQt::Interface`).

## Quickstart

Scaffold, build, and run a new app inside FinnOS:

```sh
finn-sdk new notes-lite --name "Notes Lite"
finn-sdk build notes-lite
finn-sdk run notes-lite
```

Or copy `sdk/template/`:

```sh
cp -r sdk/template my-app
cd my-app
make
make run
```

Register your app so it appears in the FinnOS Home Menu and Spotlight Search:

```sh
finn-sdk register notes-lite "Notes Lite" /path/to/my-app/build/finn-notes-lite
```

## Minimal Example

```cpp
#include <FinnSDK/FinnSDK.hpp>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(argc, argv, "hello", "Hello FinnOS");
    finn::ui::FinnWindow window("Hello FinnOS", "native-apps");
    window.resize(680, 440);

    auto* body = new QWidget(&window);
    auto* lay = new QVBoxLayout(body);
    lay->addWidget(new finn::ui::FinnLabel(
        "Built for FinnOS", finn::ui::FinnLabel::Role::TitleLarge));
    window.setContentWidget(body);
    window.show();
    return app.exec();
}
```

## App Compatibility Tiers

FinnOS prioritizes app integration in three tiers:
1. **Native (`FinnSDK`)** — Wayland C++20 + Qt6 apps built with `FinnSDK::FinnUI`
   and registered in `/usr/share/finn/apps.registry` or `~/.local/share/applications`.
2. **Linux Desktop** — Standard Wayland (`Qt6`, `GTK`), Xwayland (`DISPLAY`),
   and Flatpak apps launched from discovered `.desktop` files or `finn-open`.
3. **Windows (`.exe`)** — Inspected by PE header (`x86`, `x86_64`, `ARM64`) and
   launched cleanly inside per-app Wine prefixes when Wine / emulation is installed.
