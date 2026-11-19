// Shared FinnOS Qt theming for applications that run inside a compatibility
// runtime (finn-darwin for macOS, finn-ohos for OpenHarmony).
//
// A compat runtime draws a guest app with Qt on the FinnOS desktop. It must not
// look like a generic Qt program: it uses the same live palette, type and
// widget style a native FinnOS application gets from FinnApplication, and the
// same frameless rounded window chrome it gets from FinnWindow.
//
// Include this only from Qt-enabled builds (FINN_DARWIN_QT6 / FINN_OHOS_QT6).

#ifndef FINN_COMPAT_FINN_QT_THEME_HPP
#define FINN_COMPAT_FINN_QT_THEME_HPP

#include <QApplication>
#include <QFont>
#include <QStyle>
#include <QStyleFactory>
#include <QString>

#include "ui/finn/theme.hpp"
#include "ui/finn/tokens.hpp"

namespace finn::compat {

// Applies the FinnOS palette, font and Finn widget style to a QApplication and
// keeps them live when the user changes the theme. This mirrors
// finn::ui::FinnApplication so a guest app follows the desktop appearance
// without needing a restart.
inline void apply_finn_qt_theme(QApplication* app) {
    if (!app) return;

    // FinnWindow paints its own rounded glass chrome and control pill; QtWayland
    // must not add a second server-side title bar on top of it.
    if (!qEnvironmentVariableIsSet("QT_WAYLAND_DISABLE_WINDOWDECORATION")) {
        qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    }

    if (QStyle* style = QStyleFactory::create(QStringLiteral("finn"))) {
        app->setStyle(style);
    }

    auto& mgr = finn::ui::FinnThemeManager::instance();
    mgr.reload();
    const auto apply = [app](const finn::ui::FinnTokens& t) {
        app->setPalette(finn::ui::build_finn_palette(t));
        app->setFont(finn::ui::finn_font(10.5, QFont::Normal));
    };
    apply(mgr.tokens());
    QObject::connect(&mgr, &finn::ui::FinnThemeManager::themeChanged, app, apply);
}

// A stylesheet for plain QMainWindow hosts (the macOS AppKit bridge builds its
// NSWindow on QMainWindow because it owns a native QMenuBar). It rounds the
// surfaces, uses the Finn bone/tile/ink colours and the accent for controls,
// so these windows read as FinnOS windows even before they are reframed.
inline QString finn_guest_window_qss() {
    const finn::ui::FinnTokens t = finn::ui::load_tokens();
    const QString bone = t.bone.name();
    const QString tile = t.tile.name();
    const QString input = t.surface_input.name();
    const QString ink = t.ink.name();
    const QString mute = t.mute.name();
    const QString hair = t.hair.name(QColor::HexArgb);
    const QString accent = t.accent.name();
    const QString on = t.on.name();
    const QString track = t.track.name(QColor::HexArgb);
    const QString sel = t.nav_sel.name(QColor::HexArgb);

    return QStringLiteral(
        "QMainWindow, QWidget#finn_guest_root { background: %1; color: %2; }\n"
        "QMenuBar { background: %1; color: %2; border: 0; padding: 2px 6px; }\n"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }\n"
        "QMenuBar::item:selected { background: %10; }\n"
        "QMenu { background: %3; color: %2; border: 1px solid %7; border-radius: 8px; }\n"
        "QMenu::item { padding: 6px 18px; border-radius: 6px; }\n"
        "QMenu::item:selected { background: %10; color: %2; }\n"
        "QPushButton { background: %3; color: %2; border: 1px solid %7; "
        "border-radius: 6px; padding: 6px 14px; }\n"
        "QPushButton:hover { background: %10; }\n"
        "QPushButton:default, QPushButton:checked { background: %8; color: white; border: 0; }\n"
        "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox { "
        "background: %4; color: %2; border: 1px solid %7; border-radius: 6px; "
        "padding: 6px 10px; selection-background-color: %8; }\n"
        "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid %9; }\n"
        "QCheckBox, QRadioButton { color: %2; spacing: 8px; }\n"
        "QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; "
        "border-radius: 4px; border: 1px solid %7; background: %3; }\n"
        "QCheckBox::indicator:checked { background: %8; border: 0; }\n"
        "QSlider::groove:horizontal { height: 5px; border-radius: 3px; background: %11; }\n"
        "QSlider::sub-page:horizontal { background: %8; border-radius: 3px; }\n"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; "
        "border-radius: 7px; background: %3; border: 2px solid %8; }\n"
        "QProgressBar { background: %11; border: 0; border-radius: 4px; "
        "height: 7px; text-align: center; color: %5; }\n"
        "QProgressBar::chunk { background: %8; border-radius: 4px; }\n"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }\n"
        "QScrollBar::handle:vertical { background: %7; border-radius: 5px; min-height: 24px; }\n"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }\n"
        "QLabel { color: %2; }\n")
        .arg(bone, ink, tile, input, mute, accent, hair, accent, on, sel, track);
}

// The frameless title bar used by the macOS AppKit host. It mirrors
// FinnWindow's control pill: bone surface, hairline divider, hover wash and a
// red close affordance.
inline QString finn_guest_chrome_qss() {
    const finn::ui::FinnTokens t = finn::ui::load_tokens();
    return QStringLiteral(
        "QWidget#finn_guest_chrome { background: %1; border-bottom: 1px solid %2; }\n"
        "QLabel#finn_guest_icon { border: 0; }\n"
        "QLabel#finn_guest_title { color: %3; font-weight: 500; }\n"
        "QPushButton#finn_guest_min, QPushButton#finn_guest_max, "
        "QPushButton#finn_guest_close { background: transparent; border: 0; "
        "border-radius: 4px; color: %3; font-size: 14px; padding: 0; }\n"
        "QPushButton#finn_guest_min:hover, QPushButton#finn_guest_max:hover "
        "{ background: %4; }\n"
        "QPushButton#finn_guest_close:hover { background: %5; color: white; }\n")
        .arg(t.bone.name(), t.hair.name(QColor::HexArgb), t.ink.name(),
             t.nav_sel.name(QColor::HexArgb), t.danger.name());
}

}  // namespace finn::compat

#endif  // FINN_COMPAT_FINN_QT_THEME_HPP
