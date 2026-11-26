// World keyboard layout loader (all XKB layouts from /usr/share/X11/xkb/rules/base.lst
// with a complete built-in fallback table) + live libxkbcommon evdev keymap translator
// so every keyboard layout in the world works immediately on linuxfb, eglfs, and Wayland.

#ifndef FINN_RECOVERY_WORLD_KEYBOARDS_HPP
#define FINN_RECOVERY_WORLD_KEYBOARDS_HPP

#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QObject>
#include <QString>
#include <QTextStream>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#ifdef __linux__
#include <dlfcn.h>
#endif

namespace finn::recovery {

struct WorldKeyboardLayout {
    QString xkb_layout;     // e.g. "us", "it", "de", "ara", "jp"
    QString xkb_variant;    // optional, e.g. "dvorak", "colemak", ""
    QString loadkeys_name;  // console keymap for loadkeys (e.g. "us", "it", "de-latin1")
    QString code_badge;     // short right-column badge (e.g. "US", "IT", "DE")
    QString display_name;   // e.g. "Italian", "German", "Arabic"
};

inline QString console_keymap_for_xkb(const QString& xkb) {
    if (xkb == QLatin1String("gb")) return QStringLiteral("uk");
    if (xkb == QLatin1String("de")) return QStringLiteral("de-latin1");
    if (xkb == QLatin1String("fr")) return QStringLiteral("fr-latin9");
    if (xkb == QLatin1String("pt")) return QStringLiteral("pt-latin1");
    if (xkb == QLatin1String("se")) return QStringLiteral("sv-latin1");
    if (xkb == QLatin1String("ch")) return QStringLiteral("sg");
    if (xkb == QLatin1String("jp")) return QStringLiteral("jp106");
    if (xkb == QLatin1String("br")) return QStringLiteral("br-abnt2");
    if (xkb == QLatin1String("latam")) return QStringLiteral("la-latin1");
    if (xkb == QLatin1String("dk")) return QStringLiteral("dk-latin1");
    if (xkb == QLatin1String("no")) return QStringLiteral("no-latin1");
    if (xkb == QLatin1String("fi")) return QStringLiteral("fi-latin1");
    if (xkb == QLatin1String("tr")) return QStringLiteral("trq");
    if (xkb == QLatin1String("pl")) return QStringLiteral("pl2");
    if (xkb == QLatin1String("cz")) return QStringLiteral("cz-lat2");
    if (xkb == QLatin1String("sk")) return QStringLiteral("sk-qwerty");
    return xkb;
}

inline std::vector<WorldKeyboardLayout> load_world_keyboard_layouts() {
    std::vector<WorldKeyboardLayout> out;

    // 1. On Linux, read every XKB layout from /usr/share/X11/xkb/rules/base.lst
    //    (covers all ~100+ world keyboard layouts shipped by xkeyboard-config).
    for (const char* path : {"/usr/share/X11/xkb/rules/base.lst",
                             "/usr/share/X11/xkb/rules/evdev.lst"}) {
        QFile f(QString::fromLatin1(path));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        bool in_layouts = false;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('!'))) {
                in_layouts = (trimmed == QLatin1String("! layout"));
                continue;
            }
            if (!in_layouts || trimmed.isEmpty()) continue;
            const int sp = trimmed.indexOf(QLatin1Char(' '));
            if (sp <= 0) continue;
            const QString code = trimmed.left(sp).trimmed();
            const QString desc = trimmed.mid(sp + 1).trimmed();
            if (code.isEmpty() || desc.isEmpty() || code == QLatin1String("custom")) continue;
            out.push_back(WorldKeyboardLayout{
                code,
                {},
                console_keymap_for_xkb(code),
                code.toUpper(),
                desc,
            });
        }
        if (!out.empty()) break;
    }

    // 2. Fallback table covering major world keyboard layouts when XKB rules
    //    are not installed (e.g. macOS offscreen test host).
    if (out.empty()) {
        struct Entry { const char* xkb; const char* variant; const char* name; };
        static constexpr Entry kFallback[] = {
            {"us", "", "English (US)"},
            {"us", "intl", "English (US, International)"},
            {"us", "dvorak", "English (US, Dvorak)"},
            {"us", "colemak", "English (US, Colemak)"},
            {"gb", "", "English (UK)"},
            {"it", "", "Italian (Italiano)"},
            {"de", "", "German (Deutsch)"},
            {"fr", "", "French (Français)"},
            {"es", "", "Spanish (Español)"},
            {"latam", "", "Spanish (Latin American)"},
            {"pt", "", "Portuguese (Português)"},
            {"br", "", "Portuguese (Brazil)"},
            {"nl", "", "Dutch (Nederlands)"},
            {"be", "", "Belgian"},
            {"ch", "", "Swiss (Schweiz / Suisse)"},
            {"at", "", "Austrian"},
            {"se", "", "Swedish (Svenska)"},
            {"no", "", "Norwegian (Norsk)"},
            {"dk", "", "Danish (Dansk)"},
            {"fi", "", "Finnish (Suomi)"},
            {"is", "", "Icelandic (Íslenska)"},
            {"ie", "", "Irish"},
            {"ca", "", "Canadian"},
            {"pl", "", "Polish (Polski)"},
            {"cz", "", "Czech (Čeština)"},
            {"sk", "", "Slovak (Slovenčina)"},
            {"hu", "", "Hungarian (Magyar)"},
            {"ro", "", "Romanian (Română)"},
            {"bg", "", "Bulgarian (Български)"},
            {"hr", "", "Croatian (Hrvatski)"},
            {"si", "", "Slovenian (Slovenščina)"},
            {"rs", "", "Serbian (Српски)"},
            {"ba", "", "Bosnian"},
            {"al", "", "Albanian (Shqip)"},
            {"mk", "", "Macedonian"},
            {"gr", "", "Greek (Ελληνικά)"},
            {"tr", "", "Turkish (Türkçe)"},
            {"ru", "", "Russian (Русский)"},
            {"ua", "", "Ukrainian (Українська)"},
            {"by", "", "Belarusian"},
            {"kz", "", "Kazakh"},
            {"ee", "", "Estonian (Eesti)"},
            {"lv", "", "Latvian (Latviešu)"},
            {"lt", "", "Lithuanian (Lietuvių)"},
            {"ge", "", "Georgian (ქართული)"},
            {"am", "", "Armenian (Հայերեն)"},
            {"az", "", "Azerbaijani"},
            {"il", "", "Hebrew (עברית)"},
            {"ara", "", "Arabic (العربية)"},
            {"ir", "", "Persian (فارسی)"},
            {"in", "", "Indian (हिन्दी / Multilingual)"},
            {"pk", "", "Urdu (Pakistan)"},
            {"bd", "", "Bangla (বাংলা)"},
            {"np", "", "Nepali"},
            {"lk", "", "Sinhala"},
            {"th", "", "Thai (ไทย)"},
            {"vn", "", "Vietnamese (Tiếng Việt)"},
            {"id", "", "Indonesian"},
            {"my", "", "Malay"},
            {"ph", "", "Filipino"},
            {"jp", "", "Japanese (日本語)"},
            {"kr", "", "Korean (한국어)"},
            {"cn", "", "Chinese (中文)"},
            {"tw", "", "Chinese (Taiwan)"},
            {"za", "", "South African"},
            {"ng", "", "Nigerian"},
            {"et", "", "Amharic (Ethiopia)"},
            {"ma", "", "Arabic (Morocco)"},
            {"eg", "", "Arabic (Egypt)"},
        };
        for (const auto& e : kFallback) {
            const QString code = QString::fromLatin1(e.xkb);
            const QString var = QString::fromLatin1(e.variant);
            out.push_back(WorldKeyboardLayout{
                code,
                var,
                console_keymap_for_xkb(code),
                var.isEmpty() ? code.toUpper() : (code.toUpper() + QLatin1Char('-') + var.toUpper()),
                QString::fromUtf8(e.name),
            });
        }
    } else {
        // Put English (US) at the very top, then sort the rest alphabetically.
        std::stable_sort(out.begin(), out.end(), [](const WorldKeyboardLayout& a, const WorldKeyboardLayout& b) {
            if (a.xkb_layout == QLatin1String("us") && a.xkb_variant.isEmpty()) return true;
            if (b.xkb_layout == QLatin1String("us") && b.xkb_variant.isEmpty()) return false;
            return a.display_name.compare(b.display_name, Qt::CaseInsensitive) < 0;
        });
        // Add popular US variants right after US.
        auto insert_pos = out.empty() ? out.begin() : out.begin() + 1;
        out.insert(insert_pos, {
            WorldKeyboardLayout{QStringLiteral("us"), QStringLiteral("intl"), QStringLiteral("us-acentos"), QStringLiteral("US-INTL"), QStringLiteral("English (US, International)")},
            WorldKeyboardLayout{QStringLiteral("us"), QStringLiteral("dvorak"), QStringLiteral("dvorak"), QStringLiteral("US-DVORAK"), QStringLiteral("English (US, Dvorak)")},
            WorldKeyboardLayout{QStringLiteral("us"), QStringLiteral("colemak"), QStringLiteral("colemak"), QStringLiteral("US-COLEMAK"), QStringLiteral("English (US, Colemak)")},
        });
    }

    return out;
}

// Live in-process libxkbcommon keymap translator for Qt linuxfb/eglfs.
// On linuxfb, Qt's evdevkeyboard plugin does not reload its keymap when `loadkeys`
// runs; this filter dynamically loads libxkbcommon.so.0 (already in the live ISO)
// and translates physical hardware scan codes into the active world layout in real time.
class LiveXkbKeymapFilter final : public QObject {
public:
    explicit LiveXkbKeymapFilter(QObject* parent = nullptr) : QObject(parent) {
#ifdef __linux__
        lib_ = ::dlopen("libxkbcommon.so.0", RTLD_LAZY);
        if (lib_) {
            fn_ctx_new_ = reinterpret_cast<FnCtxNew>(::dlsym(lib_, "xkb_context_new"));
            fn_ctx_unref_ = reinterpret_cast<FnCtxUnref>(::dlsym(lib_, "xkb_context_unref"));
            fn_km_names_ = reinterpret_cast<FnKmNames>(::dlsym(lib_, "xkb_keymap_new_from_names"));
            fn_km_unref_ = reinterpret_cast<FnKmUnref>(::dlsym(lib_, "xkb_keymap_unref"));
            fn_st_new_ = reinterpret_cast<FnStNew>(::dlsym(lib_, "xkb_state_new"));
            fn_st_unref_ = reinterpret_cast<FnStUnref>(::dlsym(lib_, "xkb_state_unref"));
            fn_st_update_mask_ = reinterpret_cast<FnStUpdateMask>(::dlsym(lib_, "xkb_state_update_mask"));
            fn_st_key_utf8_ = reinterpret_cast<FnStKeyUtf8>(::dlsym(lib_, "xkb_state_key_get_utf8"));
            fn_km_mod_idx_ = reinterpret_cast<FnKmModIdx>(::dlsym(lib_, "xkb_keymap_mod_get_index"));
            if (fn_ctx_new_) ctx_ = fn_ctx_new_(0);
        }
#endif
    }

    ~LiveXkbKeymapFilter() override {
#ifdef __linux__
        if (state_ && fn_st_unref_) fn_st_unref_(state_);
        if (keymap_ && fn_km_unref_) fn_km_unref_(keymap_);
        if (ctx_ && fn_ctx_unref_) fn_ctx_unref_(ctx_);
        if (lib_) ::dlclose(lib_);
#endif
    }

    bool setLayout(const QString& layout, const QString& variant) {
        active_ = !(layout == QLatin1String("us") && variant.isEmpty());
#ifdef __linux__
        if (!ctx_ || !fn_km_names_ || !fn_st_new_) return false;
        const QByteArray lay_b = layout.toUtf8();
        const QByteArray var_b = variant.toUtf8();
        XkbRuleNames names{nullptr, nullptr, lay_b.constData(),
                           var_b.isEmpty() ? nullptr : var_b.constData(), nullptr};
        void* km = fn_km_names_(ctx_, &names, 0);
        if (!km) return false;
        void* st = fn_st_new_(km);
        if (!st) {
            fn_km_unref_(km);
            return false;
        }
        if (state_ && fn_st_unref_) fn_st_unref_(state_);
        if (keymap_ && fn_km_unref_) fn_km_unref_(keymap_);
        keymap_ = km;
        state_ = st;
        if (fn_km_mod_idx_) {
            shift_mod_ = fn_km_mod_idx_(keymap_, "Shift");
            caps_mod_ = fn_km_mod_idx_(keymap_, "Lock");
            altgr_mod_ = fn_km_mod_idx_(keymap_, "Mod5");
        }
        return true;
#else
        (void)layout;
        (void)variant;
        return false;
#endif
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
#ifdef __linux__
        if (!active_ || !state_ || !fn_st_key_utf8_ || in_dispatch_) {
            return QObject::eventFilter(obj, ev);
        }
        if (ev->type() != QEvent::KeyPress) {
            return QObject::eventFilter(obj, ev);
        }
        auto* ke = static_cast<QKeyEvent*>(ev);
        const auto mods = ke->modifiers();
        // Never intercept Ctrl/Meta shortcuts or non-hardware synthetic events.
        if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
            return QObject::eventFilter(obj, ev);
        }
        const std::uint32_t scan = ke->nativeScanCode();
        if (scan == 0) return QObject::eventFilter(obj, ev);
        // Ignore control/navigation keys (Enter, Backspace, Tab, Arrows, Escape, F-keys).
        const int k = ke->key();
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Backspace ||
            k == Qt::Key_Tab || k == Qt::Key_Backtab || k == Qt::Key_Escape ||
            (k >= Qt::Key_Home && k <= Qt::Key_PageDown) ||
            (k >= Qt::Key_F1 && k <= Qt::Key_F35)) {
            return QObject::eventFilter(obj, ev);
        }

        std::uint32_t depressed = 0;
        if ((mods & Qt::ShiftModifier) && shift_mod_ < 32) depressed |= (1u << shift_mod_);
        if ((mods & Qt::GroupSwitchModifier) && altgr_mod_ < 32) depressed |= (1u << altgr_mod_);
        if (fn_st_update_mask_) {
            fn_st_update_mask_(state_, depressed, 0, 0, 0, 0, 0);
        }

        // Qt evdevkeyboard reports Linux evdev keycodes (or X11 keycodes = evdev + 8).
        // Evdev keycodes for main typing keys are < 128; xkbcommon expects evdev + 8.
        const std::uint32_t xkb_code = (scan < 8) ? (scan + 8) : (scan < 248 ? scan + 8 : scan);
        char buf[32] = {};
        const int n = fn_st_key_utf8_(state_, xkb_code, buf, sizeof(buf));
        if (n > 0) {
            const QString mapped = QString::fromUtf8(buf, n);
            if (!mapped.isEmpty() && mapped != ke->text() && mapped[0].isPrint()) {
                in_dispatch_ = true;
                QKeyEvent synthetic(QEvent::KeyPress, mapped[0].unicode(), mods, mapped,
                                    ke->isAutoRepeat(), static_cast<ushort>(ke->count()));
                QCoreApplication::sendEvent(obj, &synthetic);
                in_dispatch_ = false;
                return true;
            }
        }
#else
        (void)obj;
        (void)ev;
#endif
        return QObject::eventFilter(obj, ev);
    }

private:
    bool active_ = false;
#ifdef __linux__
    struct XkbRuleNames {
        const char* rules;
        const char* model;
        const char* layout;
        const char* variant;
        const char* options;
    };
    using FnCtxNew = void* (*)(int);
    using FnCtxUnref = void (*)(void*);
    using FnKmNames = void* (*)(void*, const XkbRuleNames*, int);
    using FnKmUnref = void (*)(void*);
    using FnStNew = void* (*)(void*);
    using FnStUnref = void (*)(void*);
    using FnStUpdateMask = std::uint32_t (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t,
                                             std::uint32_t, std::uint32_t, std::uint32_t);
    using FnStKeyUtf8 = int (*)(void*, std::uint32_t, char*, std::size_t);
    using FnKmModIdx = std::uint32_t (*)(void*, const char*);

    void* lib_ = nullptr;
    void* ctx_ = nullptr;
    void* keymap_ = nullptr;
    void* state_ = nullptr;
    FnCtxNew fn_ctx_new_ = nullptr;
    FnCtxUnref fn_ctx_unref_ = nullptr;
    FnKmNames fn_km_names_ = nullptr;
    FnKmUnref fn_km_unref_ = nullptr;
    FnStNew fn_st_new_ = nullptr;
    FnStUnref fn_st_unref_ = nullptr;
    FnStUpdateMask fn_st_update_mask_ = nullptr;
    FnStKeyUtf8 fn_st_key_utf8_ = nullptr;
    FnKmModIdx fn_km_mod_idx_ = nullptr;
    std::uint32_t shift_mod_ = 0;
    std::uint32_t caps_mod_ = 1;
    std::uint32_t altgr_mod_ = 7;
    bool in_dispatch_ = false;
#endif
};

}  // namespace finn::recovery

#endif  // FINN_RECOVERY_WORLD_KEYBOARDS_HPP
