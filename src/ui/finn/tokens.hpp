// Shared FinnOS Qt6 design tokens, font loader, palette builder, and icon lookup.
// Reads canonical colors from src/ui/generated_tokens.hpp (ui/_tokens.fui) and
// merges system + user settings via src/system/sys_files.hpp.

#ifndef FINN_UI_FINN_TOKENS_HPP
#define FINN_UI_FINN_TOKENS_HPP

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <mutex>

#include "common/paths.hpp"
#include "system/sys_files.hpp"
#include "ui/generated_tokens.hpp"

namespace finn::ui {

inline constexpr double kRadiusSm = 4.0;
inline constexpr double kRadiusMd = 6.0;
inline constexpr double kRadiusLg = 8.0;
inline constexpr double kRadiusXl = 12.0;

struct FinnTokens {
    bool dark = false;
    bool reduced_motion = false;
    bool high_contrast = false;
    QString color_filter = QStringLiteral("none");
    int cursor_size = 28;
    double text_scale = 1.0;
    double scale = 1.0;

    QColor bone;
    QColor tile;
    QColor surface_canvas;
    QColor surface_elevated;
    QColor surface_input;
    QColor surface_hover;
    QColor surface_pressed;
    QColor tile_on;
    QColor nav_sel;
    QColor hero_tint;
    QColor hair;
    QColor ink;
    QColor mute;
    QColor dim;
    QColor accent;
    QColor track;
    QColor knob;
    QColor on;
    QColor fill;
    QColor glass;
    QColor dock_pill;

    QColor danger;
    QColor danger_hover;
    QColor success;
    QColor warning;
};

inline QColor parse_accent_or_default(const std::string& spec, std::uint32_t fallback_argb) {
    if (!spec.empty()) {
        const QColor parsed(QString::fromStdString(spec));
        if (parsed.isValid()) return parsed;
    }
    return QColor::fromRgba(fallback_argb);
}

inline bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

inline FinnTokens tokens_from_settings(const finn::sys::SettingsData& s) {
    FinnTokens t;
    t.dark = s.dark;
    t.reduced_motion = s.reduced_motion || env_truthy("FINN_REDUCED_MOTION");
    t.high_contrast = s.high_contrast;
    t.color_filter = QString::fromStdString(s.color_filter.empty() ? "none" : s.color_filter);
    t.cursor_size = (s.cursor_size >= 16 && s.cursor_size <= 96) ? s.cursor_size : 28;
    t.text_scale = (s.text_scale >= 0.75 && s.text_scale <= 2.5) ? s.text_scale : 1.0;
    t.scale = (s.scale >= 0.75 && s.scale <= 3.0) ? s.scale : 1.0;

    if (s.dark) {
        t.bone = QColor::fromRgba(colors::bone_dark);
        t.tile = QColor::fromRgba(colors::tile_dark);
        t.surface_canvas = QColor(0x00, 0x00, 0x00);
        t.surface_elevated = QColor::fromRgba(colors::tile_dark);
        t.surface_input = QColor(0x18, 0x1A, 0x20);
        t.surface_hover = QColor(255, 255, 255, 20);
        t.surface_pressed = QColor(255, 255, 255, 34);
        t.tile_on = QColor::fromRgba(colors::tile_on_dark);
        t.nav_sel = QColor::fromRgba(colors::nav_sel_dark);
        t.hero_tint = QColor::fromRgba(colors::hero_tint_dark);
        t.hair = QColor::fromRgba(colors::hair_dark);
        t.ink = QColor::fromRgba(colors::ink_dark);
        t.mute = QColor::fromRgba(colors::mute_dark);
        t.dim = QColor::fromRgba(colors::dim_dark);
        t.accent = parse_accent_or_default(s.accent, colors::accent_dark);
        t.track = QColor::fromRgba(colors::track_dark);
        t.knob = QColor::fromRgba(colors::knob_dark);
        t.on = QColor::fromRgba(colors::on_dark);
        t.fill = QColor::fromRgba(colors::fill_dark);
        QColor g = QColor::fromRgba(colors::glass_dark);
        g.setAlpha(s.high_contrast ? 255 : 226);
        t.glass = g;
        t.dock_pill = QColor(g.red(), g.green(), g.blue(), s.high_contrast ? 255 : 228);

        t.danger = QColor(0xF8, 0x71, 0x71);
        t.danger_hover = QColor(0xFF, 0x02, 0x02);
        t.success = QColor(0x22, 0xC5, 0x5E);
        t.warning = QColor(0xF5, 0x9E, 0x0B);
    } else {
        t.bone = QColor::fromRgba(colors::bone_light);
        t.tile = QColor::fromRgba(colors::tile_light);
        t.surface_canvas = QColor(0xF5, 0xF6, 0xF8);
        t.surface_elevated = QColor::fromRgba(colors::tile_light);
        t.surface_input = QColor(0xF8, 0xFA, 0xFC);
        t.surface_hover = QColor(0, 0, 0, 14);
        t.surface_pressed = QColor(0, 0, 0, 24);
        t.tile_on = QColor::fromRgba(colors::tile_on_light);
        t.nav_sel = QColor::fromRgba(colors::nav_sel_light);
        t.hero_tint = QColor::fromRgba(colors::hero_tint_light);
        t.hair = QColor::fromRgba(colors::hair_light);
        t.ink = QColor::fromRgba(colors::ink_light);
        t.mute = QColor::fromRgba(colors::mute_light);
        t.dim = QColor::fromRgba(colors::dim_light);
        t.accent = parse_accent_or_default(s.accent, colors::accent_light);
        t.track = QColor::fromRgba(colors::track_light);
        t.knob = QColor::fromRgba(colors::knob_light);
        t.on = QColor::fromRgba(colors::on_light);
        t.fill = QColor::fromRgba(colors::fill_light);
        QColor g = QColor::fromRgba(colors::glass_light);
        g.setAlpha(s.high_contrast ? 255 : 179);  // #FFFFFFB2 (70% white in Figma)
        t.glass = g;
        t.dock_pill = QColor(g.red(), g.green(), g.blue(), s.high_contrast ? 255 : 179);

        t.danger = QColor(0xEF, 0x44, 0x44);
        t.danger_hover = QColor(0xFF, 0x02, 0x02);
        t.success = QColor(0x16, 0xA3, 0x4A);
        t.warning = QColor(0xD9, 0x77, 0x06);
    }

    if (s.high_contrast) {
        t.hair = s.dark ? QColor(255, 255, 255, 220) : QColor(0, 0, 0, 220);
        t.dim = s.dark ? QColor(235, 235, 235) : QColor(20, 20, 20);
        t.mute = s.dark ? QColor(200, 200, 200) : QColor(50, 50, 50);
    }
    return t;
}

inline FinnTokens load_tokens() {
    return tokens_from_settings(finn::sys::load_merged_settings());
}

inline QPalette build_finn_palette(const FinnTokens& t) {
    QPalette pal;
    pal.setColor(QPalette::Window, t.bone);
    pal.setColor(QPalette::WindowText, t.ink);
    pal.setColor(QPalette::Base, t.surface_elevated);
    pal.setColor(QPalette::AlternateBase, t.surface_canvas);
    pal.setColor(QPalette::ToolTipBase, t.tile);
    pal.setColor(QPalette::ToolTipText, t.ink);
    pal.setColor(QPalette::PlaceholderText, t.mute);
    pal.setColor(QPalette::Text, t.ink);
    pal.setColor(QPalette::Button, t.surface_input);
    pal.setColor(QPalette::ButtonText, t.ink);
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::Light, t.tile);
    pal.setColor(QPalette::Midlight, t.surface_canvas);
    pal.setColor(QPalette::Mid, t.hair);
    pal.setColor(QPalette::Dark, t.dim);
    pal.setColor(QPalette::Shadow, QColor(0, 0, 0, t.dark ? 90 : 32));
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, t.on);
    pal.setColor(QPalette::LinkVisited, t.accent.darker(115));
    pal.setColor(QPalette::Accent, t.accent);

    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.mute);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.mute);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.mute);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, t.track);
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, t.dim);
    return pal;
}

inline QString primary_font_family() {
    static std::once_flag once;
    static QString resolved_family = QStringLiteral("Fixel Display");
    std::call_once(once, [] {
        QStringList search_dirs;
        if (const char* env_dir = std::getenv("FINN_FONT_DIR")) {
            search_dirs << QString::fromUtf8(env_dir);
        }
        search_dirs << QString::fromUtf8(kFontDir)
                    << QStringLiteral("/usr/share/fonts/OTF")
                    << QStringLiteral("assets/fonts");

        const QStringList candidates = {
            QStringLiteral("FixelDisplay-Regular.otf"),
            QStringLiteral("FixelDisplay-Medium.otf"),
            QStringLiteral("FixelDisplay-SemiBold.otf"),
            QStringLiteral("FixelDisplay-Bold.otf"),
            QStringLiteral("FixelDisplay-RegularItalic.otf"),
        };
        for (const QString& dir : search_dirs) {
            if (!QFileInfo::exists(dir)) continue;
            bool loaded_any = false;
            for (const QString& file : candidates) {
                const QString full = dir + QLatin1Char('/') + file;
                if (QFileInfo::exists(full)) {
                    const int id = QFontDatabase::addApplicationFont(full);
                    if (id >= 0) {
                        const QStringList families = QFontDatabase::applicationFontFamilies(id);
                        if (!families.isEmpty()) resolved_family = families.first();
                        loaded_any = true;
                    }
                }
            }
            if (loaded_any) break;
        }
        if (QFontDatabase::families().contains(QStringLiteral("Fixel Display"))) {
            resolved_family = QStringLiteral("Fixel Display");
        } else if (!QFontDatabase::families().contains(resolved_family)) {
            resolved_family = QFont().defaultFamily();
        }
        // Ensure non-Latin scripts (Devanagari/Hindi, Arabic, CJK, etc.) fall
        // back to Noto fonts rather than tofu boxes when Fixel Display lacks glyphs.
        const QStringList script_fallbacks = {
            QStringLiteral("Noto Sans"),
            QStringLiteral("Noto Sans Devanagari"),
            QStringLiteral("Noto Sans Arabic"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Noto Sans CJK TC"),
            QStringLiteral("Noto Sans CJK JP"),
            QStringLiteral("Noto Sans CJK KR"),
            QStringLiteral("Noto Color Emoji"),
            QStringLiteral("DejaVu Sans"),
        };
        QFont::insertSubstitutions(QStringLiteral("Fixel Display"), script_fallbacks);
        if (resolved_family != QStringLiteral("Fixel Display")) {
            QFont::insertSubstitutions(resolved_family, script_fallbacks);
        }
    });
    return resolved_family;
}

inline QFont finn_font(double point_size = 10.5, QFont::Weight weight = QFont::Normal) {
    QFont f(primary_font_family());
    f.setPointSizeF(point_size);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

inline QFont finn_mono_font(double point_size = 10.0, QFont::Weight weight = QFont::Normal) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const char* family : {"JetBrains Mono", "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono"}) {
        if (QFontDatabase::families().contains(QString::fromUtf8(family))) {
            f.setFamily(QString::fromUtf8(family));
            break;
        }
    }
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSizeF(point_size);
    f.setWeight(weight);
    return f;
}

inline QString resolve_icon_path(const QString& name_or_path) {
    if (name_or_path.isEmpty()) return {};
    if (QFileInfo::exists(name_or_path)) return name_or_path;

    QString normalized = name_or_path;
    if (normalized.endsWith(QStringLiteral(".svg"))) {
        normalized.chop(4);
    }

    QStringList roots;
    if (const char* env_icons = std::getenv("FINN_ICON_DIR")) {
        roots << QString::fromUtf8(env_icons);
    }
    roots << QString::fromUtf8(kIconDir)
          << QStringLiteral("assets/icons");
    if (QCoreApplication::instance()) {
        roots << QCoreApplication::applicationDirPath() + QStringLiteral("/assets/icons");
    }

    for (const QString& root : roots) {
        const QStringList candidates = {
            root + QStringLiteral("/") + name_or_path,
            // The installed directory is flat; the source tree keeps apps/ and shell/.
            root + QStringLiteral("/") + normalized + QStringLiteral(".svg"),
            root + QStringLiteral("/") + normalized + QStringLiteral(".png"),
            root + QStringLiteral("/apps/") + normalized + QStringLiteral(".svg"),
            root + QStringLiteral("/shell/") + normalized + QStringLiteral(".svg"),
            root + QStringLiteral("/apps/") + normalized + QStringLiteral(".png"),
            root + QStringLiteral("/shell/") + normalized + QStringLiteral(".png"),
        };
        for (const QString& cand : candidates) {
            if (QFileInfo::exists(cand)) return cand;
        }
    }
    return {};
}

inline QString resolve_pointer_path(const QString& name_or_path) {
    if (name_or_path.isEmpty()) return {};
    if (QFileInfo::exists(name_or_path)) return name_or_path;
    QString normalized = name_or_path;
    if (normalized.endsWith(QStringLiteral(".svg"))) normalized.chop(4);
    const QStringList roots = {
        QString::fromUtf8(kPointersDir),
        QStringLiteral("assets/pointers"),
    };
    for (const QString& root : roots) {
        const QString cand = root + QStringLiteral("/") + normalized + QStringLiteral(".svg");
        if (QFileInfo::exists(cand)) return cand;
    }
    return {};
}

}  // namespace finn::ui

#endif
