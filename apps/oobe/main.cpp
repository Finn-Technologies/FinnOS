// FinnOS First-Time Setup (finn-oobe).
// Full-screen minimalist onboarding built with FinnUI matching Figma First Time
// Setup ("hi", "How do we talk?") and the 6-ring 3D-to-slinky ribbon animation.

#include <FinnSDK/FinnSDK.hpp>

#include "lamp_widget.hpp"
#include "common/paths.hpp"
#include "shell/full_oobe_flow.hpp"
#include "shell/oobe_capability_probe.hpp"
#include "system/desktop_services.hpp"

#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>

namespace {

using namespace finn::ui;

QFont oobe_font(int pixel_size, QFont::Weight weight = QFont::Normal) {
    return finn_font_px(pixel_size, weight);
}

// Form factors share the shell's breakpoints and override variable so a
// phone/tablet capture is reproducible on the desktop (docs/design-language.md
// §1, src/shell/shell_layout.hpp). The Figma OOBE frames are 1280x832 desktop;
// phone and tablet are derived layouts, not a scaled desktop frame.
enum class OobeForm { Phone, Tablet, Desktop };

// `finn-sdk simulate --headless` exports FINN_SIM_WIDTH/HEIGHT and
// FINN_FORM_FACTOR. Prefer them over the physical screen so a simulated phone
// gets the phone layout even when captured from a desktop monitor.
int oobe_screen_width() {
    bool ok = false;
    const int simulated = qgetenv("FINN_SIM_WIDTH").toInt(&ok);
    if (ok && simulated > 0) return simulated;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const int w = screen->availableGeometry().width();
        if (w > 0) return w;
    }
    return 1280;  // the Figma OOBE frame width, and an explicit fallback
}

OobeForm oobe_form() {
    const QByteArray override = qgetenv("FINN_FORM_FACTOR").trimmed().toLower();
    if (override == "phone") return OobeForm::Phone;
    if (override == "tablet") return OobeForm::Tablet;
    if (override == "desktop") return OobeForm::Desktop;
    const int width = oobe_screen_width();
    if (width <= finn::ui::breakpoints::bp_phone_max) return OobeForm::Phone;
    if (width <= finn::ui::breakpoints::bp_tablet_max) return OobeForm::Tablet;
    return OobeForm::Desktop;
}

int oobe_gutter() {
    switch (oobe_form()) {
        case OobeForm::Phone: return 16;   // 16 dp page margin, 44 dp touch rows
        case OobeForm::Tablet: return 24;
        case OobeForm::Desktop: return 80; // Figma 36:53 page inset
    }
    return 80;
}

// Figma's content column is 350 px. On a narrow screen it must fit between
// the form-factor gutters; on desktop it stays exactly 350 px.
int oobe_content_width() {
    return qMax(240, qMin(350, oobe_screen_width() - 2 * oobe_gutter()));
}

// The flow model's step order is the product contract (connectivity and
// account before personalisation). The painted page order keeps the Figma
// "hi." -> language continuity, so these two tables are the only place the
// mapping is allowed to live.
using OobeStep = finn::shell::oobe::Step;

int ui_index_for_step(OobeStep step) {
    switch (step) {
        case OobeStep::Welcome: return 0;
        case OobeStep::Connectivity: return 2;
        case OobeStep::Account: return 3;
        case OobeStep::Privacy: return 6;
        case OobeStep::RegionLocale: return 1;
        case OobeStep::Accessibility: return 5;
        case OobeStep::Theme: return 4;
        case OobeStep::Finish: return 7;
    }
    return 0;
}

OobeStep step_for_ui_index(int index) {
    switch (index) {
        case 0: return OobeStep::Welcome;
        case 1: return OobeStep::RegionLocale;
        case 2: return OobeStep::Connectivity;
        case 3: return OobeStep::Account;
        case 5: return OobeStep::Accessibility;
        case 6: return OobeStep::Privacy;
        case 7: return OobeStep::Finish;
        case 4:
        default: return OobeStep::Theme;
    }
}

QString oobe_tr(const QString& key, const QString& locale) {
    struct Entry {
        const char* lang;
        const char* lang_prompt;
        const char* wifi_prompt;
        const char* wifi_scan;
        const char* account_prompt;
        const char* appearance_prompt;
        const char* a11y_prompt;
        const char* complete_title;
        const char* get_started;
    };
    static constexpr Entry kTable[] = {
        {"es", "¿Cómo hablamos?", "¿Dónde nos conectamos?", "Buscando redes cercanas...", "¿Cómo te llamamos?", "¿Oscuro o claro? Tú eliges.", "¿Algo que lo haga más fácil?", "Todo listo", "Empezar"},
        {"it", "Come parliamo?", "Dove ci connettiamo?", "Ricerca reti vicine...", "Come ti chiamiamo?", "Scuro o chiaro? Scegli tu.", "Qualcosa per renderlo più facile?", "Tutto pronto", "Inizia"},
        {"fr", "Comment parlons-nous ?", "Où se connecter ?", "Recherche des réseaux...", "Comment vous appelez-vous ?", "Sombre ou clair ? À vous de choisir.", "Quelque chose pour vous faciliter la vie ?", "Tout est prêt", "Commencer"},
        {"ca", "Com parlem?", "On ens connectem?", "Cercant xarxes properes...", "Com et diem?", "Fosc o clar? Tu tries.", "Alguna cosa per fer-ho més fàcil?", "Tot a punt", "Començar"},
        {"de", "Wie sprechen wir?", "Wo verbinden wir uns?", "Suche nach Netzwerken...", "Wie sollen wir dich nennen?", "Dunkel oder hell? Deine Wahl.", "Was es dir leichter macht?", "Alles bereit", "Los geht’s"},
        {"af", "Hoe praat ons?", "Waar koppel ons?", "Soek na nabygeleë netwerke...", "Wat moet ons jou noem?", "Donker of lig? Jou keuse.", "Iets om dit makliker te maak?", "Alles gereed", "Begin"},
        {"zh_CN", "我们使用哪种语言？", "连接到哪里？", "正在扫描附近网络…", "我们怎么称呼你？", "深色还是浅色？由你决定。", "需要帮你调得更顺手吗？", "一切就绪", "开始使用"},
        {"zh_TW", "我們使用哪種語言？", "連線到哪裡？", "正在掃描附近網路…", "我們該怎麼稱呼你？", "深色還是淺色？由你決定。", "需要幫你調得更順手嗎？", "一切就緒", "開始使用"},
        {"hi", "हम किस भाषा में बात करें?", "हम कहाँ जुड़ें?", "आस-पास के नेटवर्क खोजे जा रहे हैं...", "हम आपको क्या कहकर बुलाएँ?", "डार्क या लाइट? आपकी पसंद।", "इसे थोड़ा आसान बनाएँ?", "सब तैयार है", "शुरू करें"},
        {"ko", "어떤 언어를 사용할까요?", "어디에 연결할까요?", "주변 네트워크 검색 중...", "어떻게 불러드릴까요?", "어둡게, 아니면 밝게? 선택해 보세요.", "조금 더 편하게 맞춰드릴까요?", "모두 준비되었습니다", "시작하기"},
        {"ja", "どの言語を使いますか？", "どこに接続しますか？", "近くのネットワークを検索中…", "お名前を教えてください", "ダークかライトか、お好みで。", "もっと使いやすくしますか？", "準備が整いました", "始める"},
        {"ar", "بأي لغة نتحدث؟", "أين نتصل؟", "جارٍ البحث عن الشبكات القريبة...", "ماذا نناديك؟", "داكن أم فاتح؟ الخيار لك.", "هل نجعل الأمور أسهل لك؟", "كل شيء جاهز", "ابدأ الآن"},
        {"pt", "Como falamos?", "Onde nos ligamos?", "A procurar redes próximas...", "Como devemos chamar-te?", "Escuro ou claro? Tu escolhes.", "Algo para tornar mais fácil?", "Tudo pronto", "Começar"},
        {"nl", "Hoe spreken we?", "Waar verbinden we?", "Zoeken naar netwerken...", "Hoe mogen we je noemen?", "Donker of licht? Aan jou de keus.", "Iets om het makkelijker te maken?", "Alles klaar", "Aan de slag"},
        {"pl", "W jakim języku rozmawiamy?", "Gdzie się łączymy?", "Skanowanie pobliskich sieci...", "Jak mamy się do Ciebie zwracać?", "Ciemny czy jasny? Twój wybór.", "Coś, co ułatwi korzystanie?", "Wszystko gotowe", "Rozpocznij"},
        {"ru", "На каком языке мы говорим?", "Куда подключимся?", "Поиск ближайших сетей...", "Как к вам обращаться?", "Тёмная или светлая? Выбор за вами.", "Сделать управление удобнее?", "Всё готово", "Начать"},
        {"uk", "Якою мовою спілкуємось?", "Де підключимося?", "Пошук мереж поруч...", "Як до вас звертатися?", "Темна чи світла? Обирайте.", "Зробити користування зручнішим?", "Усе готово", "Почати"},
        {"tr", "Hangi dilde konuşalım?", "Nereye bağlanalım?", "Yakındaki ağlar taranıyor...", "Size nasıl hitap edelim?", "Koyu mu açık mı? Seçim sizin.", "Daha kolay hale getirelim mi?", "Her şey hazır", "Başla"},
    };

    const QString short_loc = locale.section(QLatin1Char('.'), 0, 0);
    const QString lang_only = short_loc.section(QLatin1Char('_'), 0, 0);
    const Entry* match = nullptr;
    for (const auto& e : kTable) {
        if (short_loc == QLatin1String(e.lang) || lang_only == QLatin1String(e.lang)) {
            match = &e;
            break;
        }
    }
    if (key == QLatin1String("lang_prompt"))
        return match ? QString::fromUtf8(match->lang_prompt) : QStringLiteral("How do we talk?");
    if (key == QLatin1String("wifi_prompt"))
        return match ? QString::fromUtf8(match->wifi_prompt) : QStringLiteral("Where do we connect?");
    if (key == QLatin1String("wifi_scan"))
        return match ? QString::fromUtf8(match->wifi_scan) : QStringLiteral("Scanning nearby networks...");
    if (key == QLatin1String("account_prompt"))
        return match ? QString::fromUtf8(match->account_prompt) : QStringLiteral("What should we call you?");
    if (key == QLatin1String("appearance_prompt"))
        return match ? QString::fromUtf8(match->appearance_prompt) : QStringLiteral("Dark or light? Your choice.");
    if (key == QLatin1String("a11y_prompt"))
        return match ? QString::fromUtf8(match->a11y_prompt) : QStringLiteral("Need a hand?");
    if (key == QLatin1String("complete_title"))
        return match ? QString::fromUtf8(match->complete_title) : QStringLiteral("You\u2019re all set");
    if (key == QLatin1String("get_started"))
        return match ? QString::fromUtf8(match->get_started) : QStringLiteral("Get started");
    return key;
}

// Top Branding lockup matching Figma 36:45 (20x20 four-shape mark + 15px gap + "FinnOS" 20px).
class OobeBrandingHeader final : public QWidget {
    Q_OBJECT

public:
    explicit OobeBrandingHeader(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(112, 24);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const QColor ink = t.ink;
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(0.0, 2.0, 20.0, 20.0),
            t.dark ? QStringLiteral("finn-logo-light") : QStringLiteral("finn-logo-dark"),
            ink);

        p.setFont(oobe_font(20, QFont::Normal));
        p.setPen(ink);
        p.drawText(QRectF(35.0, 0.0, width() - 35.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FinnOS"));
    }

private:
    qreal opacity_ = 1.0;
};

// Circular 56x56 chevron navigation button matching Figma 36:48 / 36:66 / 36:54.
class OobeCircleNavButton final : public QAbstractButton {
    Q_OBJECT

public:
    OobeCircleNavButton(bool is_back, bool filled_by_default,
                        QWidget* parent = nullptr)
        : QAbstractButton(parent),
          is_back_(is_back),
          filled_default_(filled_by_default) {
        setFixedSize(56, 56);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(is_back ? QStringLiteral("Back") : QStringLiteral("Next"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_p = isDown() ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            press_t_ += (target_p - press_t_) * 0.36;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_p - press_t_) < 0.01) {
                hover_t_ = target_h;
                press_t_ = target_p;
                anim_timer_.stop();
            }
            update();
        });
    }

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        setAttribute(Qt::WA_TransparentForMouseEvents, opacity_ < 0.25);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const qreal micro_scale = 1.0 + 0.045 * hover_t_ - 0.095 * press_t_;
        const QPointF center(width() * 0.5, height() * 0.5);
        p.translate(center);
        p.scale(micro_scale, micro_scale);
        p.translate(-center);

        const QRectF circle_r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool en = isEnabled();

        QColor fill = Qt::transparent;
        if (filled_default_) {
            fill = t.tile;
            if (press_t_ > 0.02) {
                const QColor down_c = t.surface_pressed;
                fill = QColor(
                    qRound(fill.red() + (down_c.red() - fill.red()) * press_t_),
                    qRound(fill.green() + (down_c.green() - fill.green()) * press_t_),
                    qRound(fill.blue() + (down_c.blue() - fill.blue()) * press_t_));
            } else if (hover_t_ > 0.02) {
                const QColor hov_c = t.surface_hover;
                fill = QColor(
                    qRound(fill.red() + (hov_c.red() - fill.red()) * hover_t_),
                    qRound(fill.green() + (hov_c.green() - fill.green()) * hover_t_),
                    qRound(fill.blue() + (hov_c.blue() - fill.blue()) * hover_t_));
            }
        } else if (en) {
            const qreal a = hover_t_ * (t.dark ? 22.0 : 195.0) + press_t_ * (t.dark ? 16.0 : 40.0);
            if (a > 0.5) {
                fill = QColor(t.fill.red(), t.fill.green(), t.fill.blue(),
                              std::min(255, qRound(a)));
            }
        }

        if (fill.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(circle_r);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle_r.adjusted(1.0, 1.0, -1.0, -1.0));
        }

        // Exact chevron vector from Figma 36:46 (36x36 box inside 56x56 at offset 10,10):
        // M13 29 C13 29 24 20.8987 24 18 C24 15.1011 13 7 13 7 (stroke-width 2.75)
        QColor stroke_col = t.ink;
        if (!en) stroke_col.setAlpha(72);

        p.save();
        p.translate(10.0, 10.0);
        if (is_back_) {
            p.translate(36.0, 0.0);
            p.scale(-1.0, 1.0);
        }
        QPainterPath chev;
        chev.moveTo(13.0, 29.0);
        chev.cubicTo(13.0, 29.0, 24.0, 20.8987, 24.0, 18.0);
        chev.cubicTo(24.0, 15.1011, 13.0, 7.0, 13.0, 7.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(stroke_col, 2.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chev);
        p.restore();
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void mousePressEvent(QMouseEvent* e) override {
        QAbstractButton::mousePressEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        QAbstractButton::mouseReleaseEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

private:
    bool is_back_ = false;
    bool filled_default_ = false;
    bool hovered_ = false;
    qreal hover_t_ = 0.0;
    qreal press_t_ = 0.0;
    qreal opacity_ = 1.0;
    QTimer anim_timer_;
};

// Pill button matching the video's "Get started" action pill on the final screen.
class OobeGetStartedPill final : public QAbstractButton {
    Q_OBJECT

public:
    explicit OobeGetStartedPill(const QString& label, QWidget* parent = nullptr)
        : QAbstractButton(parent) {
        setText(label);
        setAccessibleName(label);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(142, 48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = t.tile;
        if (isDown()) {
            bg = t.surface_pressed;
        } else if (hovered_) {
            bg = t.surface_hover;
        }
        p.setBrush(bg);
        p.setPen(QPen(hasFocus() ? t.accent : t.hair, hasFocus() ? 2.0 : 1.0));
        p.drawRoundedRect(r, 24.0, 24.0);

        p.setFont(oobe_font(15, QFont::Medium));
        p.setPen(t.ink);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

private:
    bool hovered_ = false;
};

// Selectable 350x37 row matching Figma 37:98 ("Language" item in "How do we talk?").
// Selected row draws a solid white #FFFFFF pill (cornerRadius 10); unselected is transparent.
class OobeOptionRow final : public QWidget {
    Q_OBJECT

public:
    OobeOptionRow(const QString& label, const QString& detail = {},
                  bool selected = false, QWidget* parent = nullptr)
        : QWidget(parent),
          label_(label),
          detail_(detail),
          selected_(selected) {
        setFixedSize(oobe_content_width(), 37);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(detail.isEmpty() ? label : QStringLiteral("%1, %2").arg(label, detail));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        sel_t_ = selected_ ? 1.0 : 0.0;
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_s = selected_ ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            sel_t_ += (target_s - sel_t_) * 0.24;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_s - sel_t_) < 0.01) {
                hover_t_ = target_h;
                sel_t_ = target_s;
                anim_timer_.stop();
            }
            update();
        });
    }

    void setSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    bool isSelected() const { return selected_; }
    QString label() const { return label_; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(0.0, 0.0, width(), height());
        const qreal base_a = t.dark
            ? (hover_t_ * 14.0 + sel_t_ * 28.0 + (pressed_ ? 12.0 : 0.0))
            : (hover_t_ * 110.0 + sel_t_ * 255.0 + (pressed_ ? 50.0 : 0.0));
        if (base_a > 0.5) {
            const QColor pill_base = t.dark ? t.tile_on : t.tile;
            const QColor bg(pill_base.red(), pill_base.green(), pill_base.blue(),
                            std::min(255, qRound(t.dark ? base_a * 8.0 : base_a)));
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 10.0, 10.0);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), 10.0, 10.0);
        }

        const qreal nudge_x = 2.5 * hover_t_ + 3.0 * sel_t_;
        const QColor ink = t.ink;
        p.setFont(oobe_font(18, QFont::Normal));
        p.setPen(ink);
        const QRectF text_r(8.0 + nudge_x, 0.0, width() - 16.0 - nudge_x, height());
        p.drawText(text_r, Qt::AlignLeft | Qt::AlignVCenter, label_);

        if (!detail_.isEmpty()) {
            p.setFont(oobe_font(13, QFont::Normal));
            p.setPen(t.dim);
            p.drawText(QRectF(8.0, 0.0, width() - 16.0, height()),
                       Qt::AlignRight | Qt::AlignVCenter, detail_);
        }
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        pressed_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        } else {
            QWidget::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(event->pos())) emit clicked();
        } else {
            QWidget::mouseReleaseEvent(event);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            emit clicked();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QString label_;
    QString detail_;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal hover_t_ = 0.0;
    qreal sel_t_ = 0.0;
    QTimer anim_timer_;
};

// 3D-ready appearance preview stage showing the Finn logo on the active Light/Dark backdrop.
class OobeAppearancePreview final : public QWidget {
    Q_OBJECT

public:
    explicit OobeAppearancePreview(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(oobe_content_width(), 200);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor stage_bg = t.bone;
        const QColor border = t.hair;

        p.setPen(QPen(border, 1.0));
        p.setBrush(stage_bg);
        p.drawRoundedRect(bounds, 16.0, 16.0);

        // Soft ambient glow behind the logo stage (placeholder until 3D model integration).
        QRadialGradient glow(bounds.center(), bounds.width() * 0.42);
        QColor glow_c = t.ink;
        glow_c.setAlpha(t.dark ? 22 : 12);
        QColor glow_end = t.ink;
        glow_end.setAlpha(0);
        glow.setColorAt(0.0, glow_c);
        glow.setColorAt(1.0, glow_end);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawRoundedRect(bounds, 16.0, 16.0);

        const qreal logo_sz = 64.0;
        const QRectF logo_rect(bounds.center().x() - logo_sz * 0.5,
                               bounds.center().y() - logo_sz * 0.5 - 6.0,
                               logo_sz, logo_sz);
        FinnThemeManager::instance().paintIcon(
            &p, logo_rect,
            t.dark ? QStringLiteral("finn-logo-light") : QStringLiteral("finn-logo-dark"),
            t.ink);

        p.setFont(oobe_font(12, QFont::Medium));
        p.setPen(t.dim);
        p.drawText(QRectF(bounds.left(), bounds.bottom() - 32.0, bounds.width(), 22.0),
                   Qt::AlignCenter,
                   t.dark ? QStringLiteral("Dark Mode") : QStringLiteral("Light Mode"));
    }
};

// Visual Light/Dark mode selector card featuring the white/black 4-shape Finn logo.
class OobeAppearanceCard final : public QWidget {
    Q_OBJECT

public:
    OobeAppearanceCard(bool dark_card, const QString& label, bool selected,
                       QWidget* parent = nullptr)
        : QWidget(parent),
          dark_card_(dark_card),
          label_(label),
          selected_(selected) {
        setFixedSize(169, 122);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("%1 theme").arg(label));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        QColor card_bg = t.tile;
        if (pressed_) {
            card_bg = t.surface_pressed;
        } else if (hovered_ && !selected_) {
            card_bg = t.surface_hover;
        }

        const QColor border = (selected_ || hasFocus()) ? t.accent : t.hair;
        p.setPen(QPen(border, (selected_ || hasFocus()) ? 2.5 : 1.0));
        p.setBrush(card_bg);
        p.drawRoundedRect(r, 14.0, 14.0);

        const QRectF swatch(r.left() + 10.0, r.top() + 10.0, r.width() - 20.0, 70.0);
        const QColor swatch_bg = QColor::fromRgba(dark_card_ ? colors::bone_dark : colors::bone_light);
        const QColor swatch_border = QColor::fromRgba(dark_card_ ? colors::hair_dark : colors::hair_light);
        const QColor swatch_ink = QColor::fromRgba(dark_card_ ? colors::ink_dark : colors::ink_light);
        p.setPen(QPen(swatch_border, 1.0));
        p.setBrush(swatch_bg);
        p.drawRoundedRect(swatch, 9.0, 9.0);

        const qreal logo_sz = 34.0;
        const QRectF logo_r(swatch.center().x() - logo_sz * 0.5,
                            swatch.center().y() - logo_sz * 0.5,
                            logo_sz, logo_sz);
        FinnThemeManager::instance().paintIcon(
            &p, logo_r,
            dark_card_ ? QStringLiteral("finn-logo-light") : QStringLiteral("finn-logo-dark"),
            swatch_ink);

        p.setFont(oobe_font(15, selected_ ? QFont::DemiBold : QFont::Normal));
        p.setPen(t.ink);
        p.drawText(QRectF(r.left(), swatch.bottom() + 4.0, r.width(), 28.0),
                   Qt::AlignCenter, label_);
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        pressed_ = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        } else {
            QWidget::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(event->pos())) emit clicked();
        } else {
            QWidget::mouseReleaseEvent(event);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            emit clicked();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    bool dark_card_ = false;
    QString label_;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal hover_t_ = 0.0;
    qreal sel_t_ = 0.0;
    QTimer anim_timer_;
};

// Minimalist 350px text input row matching Figma 37:98 pill geometry.
class OobeInputField final : public QWidget {
    Q_OBJECT

public:
    OobeInputField(const QString& placeholder, bool password = false,
                   QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 42);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 4, 12, 4);
        lay->setSpacing(8);

        edit_ = new QLineEdit(this);
        edit_->setFrame(false);
        edit_->setPlaceholderText(placeholder);
        edit_->setAccessibleName(placeholder);
        edit_->setFont(oobe_font(17, QFont::Normal));
        if (password) edit_->setEchoMode(QLineEdit::Password);
        edit_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        lay->addWidget(edit_, 1);

        connect(edit_, &QLineEdit::textChanged, this, [this]() { update(); });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeInputField::syncColors);
        syncColors();
    }

    QLineEdit* lineEdit() const { return edit_; }
    QString text() const { return edit_->text(); }
    void setText(const QString& t) { edit_->setText(t); }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(edit_->hasFocus() ? QPen(t.accent, 1.5) : QPen(t.hair, 1.0));
        p.setBrush(t.surface_input);
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 10.0, 10.0);
    }

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = edit_->palette();
        pal.setColor(QPalette::Text, t.ink);
        pal.setColor(QPalette::PlaceholderText, t.mute);
        edit_->setPalette(pal);
        update();
    }

    QLineEdit* edit_ = nullptr;
};

// Minimalist 350px toggle row matching Figma 37:98 typography and spacing.
class OobeToggleRow final : public QWidget {
    Q_OBJECT

public:
    OobeToggleRow(const QString& label, bool initial_checked,
                  QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 39);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);

        label_ = new QLabel(label, this);
        label_->setFont(oobe_font(18, QFont::Normal));
        lay->addWidget(label_, 1);

        toggle_ = new FinnToggle(initial_checked, this);
        toggle_->setAccessibleName(label);
        toggle_->setFocusPolicy(Qt::TabFocus);
        lay->addWidget(toggle_);

        connect(toggle_, &QAbstractButton::toggled, this,
                &OobeToggleRow::toggled);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeToggleRow::syncColors);
        syncColors();
    }

    FinnToggle* toggle() const { return toggle_; }

signals:
    void toggled(bool checked);

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = label_->palette();
        pal.setColor(QPalette::WindowText, t.ink);
        label_->setPalette(pal);
        update();
    }

    QLabel* label_ = nullptr;
    FinnToggle* toggle_ = nullptr;
};

// Circular gradient swatch for picking the Figma-based PFP color variant.
class OobePfpSwatch final : public QAbstractButton {
    Q_OBJECT

public:
    OobePfpSwatch(const AvatarPreset& preset, QWidget* parent = nullptr)
        : QAbstractButton(parent), preset_(preset) {
        setFixedSize(32, 32);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("%1 avatar color").arg(preset.name));
        setCursor(Qt::PointingHandCursor);
        setToolTip(preset.name);
        setCheckable(true);
    }

    const QString& presetId() const { return preset_.id; }

protected:
    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        if (isChecked() || hasFocus()) {
            const QColor ring = hasFocus() ? t.accent : t.ink;
            p.setPen(QPen(ring, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        } else if (hovered_) {
            p.setPen(QPen(t.hair, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        }

        const qreal inset = isChecked() ? 5.0 : 4.0;
        const QRectF dot = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        QLinearGradient grad(dot.topLeft(), dot.bottomLeft());
        grad.setColorAt(0.0, preset_.top_color);
        grad.setColorAt(1.0, preset_.bottom_color);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(dot);
    }

private:
    AvatarPreset preset_;
    bool hovered_ = false;
};

// Progressive bottom blur overlay matching Figma 37:96 (LAYER_BLUR PROGRESSIVE,
// startOffset y=0.607 to 1.0, radius 20 -> CSS blur bands 2.15px..8.60px).
QImage box_blur_image(const QImage& src, int radius) {
    if (radius <= 0 || src.isNull()) return src;
    QImage a = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage b(a.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = a.width();
    const int h = a.height();
    const int d = 2 * radius + 1;

    for (int pass = 0; pass < 2; ++pass) {
        // Horizontal pass
        for (int y = 0; y < h; ++y) {
            const QRgb* in_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            QRgb* out_row = reinterpret_cast<QRgb*>(b.scanLine(y));
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = in_row[std::clamp(k, 0, w - 1)];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int x = 0; x < w; ++x) {
                out_row[x] = qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = in_row[std::clamp(x - radius, 0, w - 1)];
                const QRgb add = in_row[std::clamp(x + radius + 1, 0, w - 1)];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
        // Vertical pass
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(k, 0, h - 1)))[x];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(a.scanLine(y))[x] =
                    qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y - radius, 0, h - 1)))[x];
                const QRgb add = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y + radius + 1, 0, h - 1)))[x];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
    }
    return a;
}

class OobeProgressiveList final : public QWidget {
    Q_OBJECT

public:
    explicit OobeProgressiveList(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(oobe_content_width() + 6);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        scroll_ = new QScrollArea(this);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->viewport()->setAutoFillBackground(false);
        scroll_->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        scroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

        host_ = new QWidget(scroll_);
        host_->setAutoFillBackground(false);
        host_->setAttribute(Qt::WA_TranslucentBackground, true);
        list_lay_ = new QVBoxLayout(host_);
        list_lay_->setContentsMargins(0, 0, 6, 80);
        list_lay_->setSpacing(10);
        scroll_->setWidget(host_);

        connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged,
                this, qOverload<>(&QWidget::update));
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    QVBoxLayout* listLayout() const { return list_lay_; }
    QWidget* hostWidget() const { return host_; }
    QScrollBar* verticalScrollBar() const { return scroll_ ? scroll_->verticalScrollBar() : nullptr; }
    int scrollOffset() const { return scroll_ && scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        scroll_->setGeometry(rect());
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        return QWidget::eventFilter(watched, event);
    }

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
};

// Overlay widget placed over the bottom of OobeProgressiveList to apply the
// progressive blur & fade from y=60.7% to 100% without blocking mouse events.
// Caches the 3-pass software blur and invalidates only on geometry, scroll, theme,
// or explicit list-content changes (M19).
class ProgressiveBlurOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit ProgressiveBlurOverlay(OobeProgressiveList* target_list, QWidget* parent = nullptr)
        : QWidget(parent), target_(target_list) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        if (target_ && target_->verticalScrollBar()) {
            connect(target_->verticalScrollBar(), &QScrollBar::valueChanged,
                    this, &ProgressiveBlurOverlay::invalidateCache);
        }
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &ProgressiveBlurOverlay::invalidateCache);
    }

    void invalidateCache() {
        cache_dirty_ = true;
        update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        cache_dirty_ = true;
    }

    void paintEvent(QPaintEvent*) override {
        if (!target_ || height() < 40 || in_capture_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        const QColor bg = t.bone;

        const int blur_top = qRound(height() * 0.607);
        const int blur_h = height() - blur_top;
        if (blur_h <= 8) return;

        const int scroll_y = target_->scrollOffset();
        if (cache_dirty_ || cached_comp_.isNull() || cached_size_ != size() ||
            cached_dark_ != t.dark || cached_scroll_ != scroll_y) {
            in_capture_ = true;
            QImage snap(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            snap.fill(bg);
            target_->render(&snap, QPoint(0, 0), QRegion(0, blur_top, width(), blur_h),
                            QWidget::DrawChildren);
            in_capture_ = false;

            const QImage b1 = box_blur_image(snap, 2);
            const QImage b2 = box_blur_image(b1, 4);
            const QImage b3 = box_blur_image(b2, 6);

            const int band = blur_h / 4;
            const int feather = std::max(12, blur_h / 5);

            QImage comp(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            comp.fill(Qt::transparent);
            {
                QPainter cp(&comp);
                cp.setRenderHint(QPainter::Antialiasing, true);
                cp.drawImage(0, 0, b1, 0, 0, width(), band * 2);
                cp.drawImage(0, band, b2, 0, band, width(), band * 2);
                cp.drawImage(0, band * 2, b3, 0, band * 2, width(), blur_h - band * 2);

                // Feather top entry of the blur region so there is no hard horizontal step.
                QLinearGradient mask(0, 0, 0, feather);
                mask.setColorAt(0.0, QColor(0, 0, 0, 0));
                mask.setColorAt(1.0, QColor(0, 0, 0, 255));
                cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cp.fillRect(QRect(0, 0, width(), feather), mask);
            }
            cached_comp_ = std::move(comp);
            cached_size_ = size();
            cached_dark_ = t.dark;
            cached_scroll_ = scroll_y;
            cache_dirty_ = false;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawImage(0, blur_top, cached_comp_);

        // Smooth progressive surface fade all the way to opaque bg at bottom edge.
        QLinearGradient fade(0, blur_top, 0, height());
        QColor c0 = bg;
        c0.setAlpha(0);
        QColor c1 = bg;
        c1.setAlpha(110);
        QColor c2 = bg;
        c2.setAlpha(255);
        fade.setColorAt(0.0, c0);
        fade.setColorAt(0.55, c1);
        fade.setColorAt(1.0, c2);
        p.fillRect(QRect(0, blur_top, width(), blur_h), fade);
    }

private:
    OobeProgressiveList* target_ = nullptr;
    bool in_capture_ = false;
    bool cache_dirty_ = true;
    QImage cached_comp_;
    QSize cached_size_;
    bool cached_dark_ = false;
    int cached_scroll_ = -1;
};

// Seamless transition overlay when advancing from Step 0 ("hi.") to Step 1
// ("How do we talk?"): continues from the end of "hi." into the same upright
// oval spring as the intro, draws forward across the whole screen while the
// camera pans, fades into Step 1, and reveals "How do we talk?" left-to-right
// with a glowing leading edge.
// Shared feathered left-to-right neon text reveal + soft bloom halo, used by both
// OobeHiToLangOverlay (Step 0 -> 1) and OobePageEntranceOverlay (Steps 1..6).
struct NeonTextReveal {
    QImage crisp_img;
    QImage glow_img;
    int glow_pad = 32;

    static QRectF labelTextRect(QLabel* lbl, QWidget* root) {
        if (!lbl || !root) return {};
        const QPoint tl = lbl->mapTo(root, QPoint(0, 0));
        const QFontMetricsF fm(lbl->font());
        const int flags = Qt::AlignCenter | (lbl->wordWrap() ? Qt::TextWordWrap : 0);
        const QRectF br = fm.boundingRect(
            QRectF(0.0, 0.0, std::max(1, lbl->width()), 400.0), flags, lbl->text());
        const double tx = tl.x() + (lbl->width() - br.width()) * 0.5;
        const double ty = tl.y() + (lbl->height() - br.height()) * 0.5;
        return QRectF(tx, ty, br.width(), br.height());
    }

    void ensurePrepared(QLabel* lbl, const QRectF& target_rect,
                        const QColor& text_col, bool dark) {
        if (!crisp_img.isNull() || !lbl || target_rect.width() <= 4.0) return;
        constexpr int kPad = 32;
        const int gw = static_cast<int>(std::ceil(target_rect.width())) + kPad * 2;
        const int gh = static_cast<int>(std::ceil(target_rect.height())) + kPad * 2;
        const QRectF text_box(kPad, kPad, target_rect.width(), target_rect.height());
        const int flags = Qt::AlignCenter | (lbl->wordWrap() ? Qt::TextWordWrap : 0);

        crisp_img = QImage(gw, gh, QImage::Format_ARGB32_Premultiplied);
        crisp_img.fill(Qt::transparent);
        {
            QPainter cp(&crisp_img);
            cp.setRenderHint(QPainter::Antialiasing, true);
            cp.setRenderHint(QPainter::TextAntialiasing, true);
            cp.setFont(lbl->font());
            cp.setPen(text_col);
            cp.drawText(text_box, flags, lbl->text());
        }

        QImage raw_glow(gw, gh, QImage::Format_ARGB32_Premultiplied);
        raw_glow.fill(Qt::transparent);
        {
            QPainter gp(&raw_glow);
            gp.setRenderHint(QPainter::Antialiasing, true);
            gp.setRenderHint(QPainter::TextAntialiasing, true);
            gp.setFont(lbl->font());
            gp.setPen(dark ? QColor(255, 255, 255, 255) : QColor(255, 255, 255, 240));
            gp.drawText(text_box, flags, lbl->text());
            if (!dark) {
                gp.setPen(QColor(110, 150, 255, 130));
                gp.drawText(text_box, flags, lbl->text());
            }
        }
        glow_img = box_blur_image(box_blur_image(raw_glow, 9), 5);
        glow_pad = kPad;
    }

    void paint(QPainter& p, const QRectF& target_rect, const QColor& text_col,
               const QColor& blue, double reveal_u, double neon_u,
               double bloom_alpha, double text_opacity) const {
        if (crisp_img.isNull() || (reveal_u <= 0.001 && bloom_alpha <= 0.01)) return;
        const QColor coral(0xFF, 0x57, 0x22);
        const QColor amber(0xFF, 0x95, 0x00);
        const QColor violet(0x8B, 0x5C, 0xF6);
        const int gw = crisp_img.width();
        const int gh = crisp_img.height();

        QImage tinted_text(gw, gh, QImage::Format_ARGB32_Premultiplied);
        tinted_text.fill(Qt::transparent);
        {
            QPainter tp(&tinted_text);
            tp.drawImage(0, 0, crisp_img);
            const double wave_head = glow_pad + (target_rect.width() + 150.0) * neon_u;
            QLinearGradient text_grad(wave_head - 140.0, 0.0, wave_head + 35.0, 0.0);
            text_grad.setColorAt(0.00, text_col);
            text_grad.setColorAt(0.28, violet);
            text_grad.setColorAt(0.54, coral);
            text_grad.setColorAt(0.76, amber);
            text_grad.setColorAt(0.92, blue);
            text_grad.setColorAt(1.00, text_col);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(0, 0, gw, gh, text_grad);
        }

        QImage comp(gw, gh, QImage::Format_ARGB32_Premultiplied);
        comp.fill(Qt::transparent);
        {
            QPainter cp(&comp);
            if (bloom_alpha > 0.01) {
                cp.setOpacity(bloom_alpha * 0.70);
                cp.drawImage(0, 0, glow_img);
            }
            if (text_opacity > 0.005) {
                cp.setOpacity(text_opacity);
                cp.drawImage(0, 0, tinted_text);
            }
            if (reveal_u < 1.0) {
                const double sweep_x = -12.0 + (gw + 48.0) * reveal_u;
                QLinearGradient feather(sweep_x - 52.0, 0.0, sweep_x + 12.0, 0.0);
                feather.setColorAt(0.0, QColor(255, 255, 255, 255));
                feather.setColorAt(0.65, QColor(255, 255, 255, 160));
                feather.setColorAt(1.0, QColor(255, 255, 255, 0));
                cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cp.setOpacity(1.0);
                cp.fillRect(0, 0, gw, gh, feather);
            }
        }
        p.drawImage(QPointF(target_rect.left() - glow_pad,
                            target_rect.top() - glow_pad),
                    comp);
    }
};

// Smooth ease-in-out directional slide transition overlay for OOBE steps
// (forward: right-to-left, back: left-to-right), keeping background and nav arrows fixed.
class OobeSlideTransitionOverlay final : public QWidget {
public:
    OobeSlideTransitionOverlay(QWidget* parent, QWidget* stack_widget,
                               QPixmap old_snap, QPixmap new_snap, bool forward)
        : QWidget(parent),
          stack_widget_(stack_widget),
          old_snap_(std::move(old_snap)),
          new_snap_(std::move(new_snap)),
          forward_(forward) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (stack_widget_) {
            setGeometry(stack_widget_->geometry());
            stack_widget_->setVisible(false);
        } else if (parent) {
            setGeometry(parent->rect());
        }
        elapsed_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(8);
        connect(timer_, &QTimer::timeout, this, [this]() {
            if (stack_widget_ && geometry() != stack_widget_->geometry()) {
                setGeometry(stack_widget_->geometry());
            }
            if (elapsed_.nsecsElapsed() / 1e9 >= kDurationSec) {
                finish();
                return;
            }
            update();
        });
        timer_->start();
        show();
        raise();
    }

    ~OobeSlideTransitionOverlay() override {
        if (!cancelled_ && stack_widget_) {
            stack_widget_->setVisible(true);
        }
    }

    QPixmap grabCurrentFrame() {
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(width() * dpr), qRound(height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        render(&pix);
        return pix;
    }

    void cancel() {
        cancelled_ = true;
        if (timer_) timer_->stop();
        hide();
        deleteLater();
    }

    void finish() {
        if (timer_) timer_->stop();
        if (stack_widget_) {
            stack_widget_->setVisible(true);
        }
        hide();
        deleteLater();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double t = std::clamp((elapsed_.nsecsElapsed() / 1e9) / kDurationSec, 0.0, 1.0);
        // Smooth ease-in-out with a long, gentle settle tail and subtle directional travel.
        const double u = (t < 0.35)
                             ? (0.4066 * std::pow(t / 0.35, 2.2))
                             : (1.0 - 0.5934 * std::pow((1.0 - t) / 0.65, 2.8));
        const double fade_out_t = std::clamp(t / 0.48, 0.0, 1.0);
        const double fade_out = 1.0 - fade_out_t * fade_out_t * (3.0 - 2.0 * fade_out_t);
        const double fade_in_t = std::clamp((t - 0.10) / 0.52, 0.0, 1.0);
        const double fade_in = fade_in_t * fade_in_t * (3.0 - 2.0 * fade_in_t);

        constexpr qreal kOutTravel = 140.0;
        constexpr qreal kInTravel = 180.0;
        const qreal dir = forward_ ? 1.0 : -1.0;

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (fade_out > 0.001) {
            p.setOpacity(fade_out);
            p.drawPixmap(QPointF(-dir * kOutTravel * u, 0.0), old_snap_);
        }
        if (fade_in > 0.001) {
            p.setOpacity(fade_in);
            p.drawPixmap(QPointF(dir * kInTravel * (1.0 - u), 0.0), new_snap_);
        }
        p.setOpacity(1.0);
    }

private:
    static constexpr double kDurationSec = 0.58;
    QPointer<QWidget> stack_widget_;
    QPixmap old_snap_;
    QPixmap new_snap_;
    bool forward_ = true;
    bool cancelled_ = false;
    QElapsedTimer elapsed_;
    QTimer* timer_ = nullptr;
};

class OobeHiToLangOverlay : public QWidget {
public:
    OobeHiToLangOverlay(QWidget* parent, QLabel* target_prompt)
        : QWidget(parent), target_prompt_(target_prompt) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (parent) {
            setGeometry(parent->rect());
        }
        elapsed_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(16);
        connect(timer_, &QTimer::timeout, this, [this]() {
            if (parentWidget() && size() != parentWidget()->size()) {
                setGeometry(parentWidget()->rect());
            }
            const double t = elapsed_.elapsed() / 1000.0;
            if (t >= 3.55) {
                timer_->stop();
                hide();
                deleteLater();
                return;
            }
            update();
        });
        timer_->start();
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double t = elapsed_.elapsed() / 1000.0;
        const int w = width();
        const int h = height();
        if (w <= 0 || h <= 0) return;

        constexpr double kPi = 3.14159265358979323846;
        const auto& tok = FinnThemeManager::instance().tokens();
        const QColor bg = tok.bone;
        const QColor ink = tok.ink;
        const QColor text_col = tok.ink;
        const QColor coral(0xFF, 0x57, 0x22);
        const QColor amber(0xFF, 0x95, 0x00);
        const QColor violet(0x8B, 0x5C, 0xF6);
        const QColor blue = tok.accent;

        auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
        auto smooth01 = [&](double a, double b) {
            const double u = clamp01((t - a) / std::max(0.001, b - a));
            return u * u * (3.0 - 2.0 * u);
        };
        auto ease_out_cubic = [](double u) {
            const double inv = 1.0 - std::clamp(u, 0.0, 1.0);
            return 1.0 - inv * inv * inv;
        };

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 1. Keep Step 1 hidden behind opaque bg while:
        //    0.00..2.48s : "hi." -> spring (same pace as paintHiIntro) -> brief pause as POV
        //                  puts spring on the left side -> uncoils at target_rect.left()
        //    2.40..2.98s : ONLY "How do we talk?" text animates in with feathered bloom
        //    3.00..3.45s : veil fades out so the rest of Step 1 appears underneath
        const double veil_fade = smooth01(3.00, 3.45);
        const int veil_alpha = static_cast<int>(std::lround(255.0 * (1.0 - veil_fade)));
        if (veil_alpha > 0) {
            QColor veil = bg;
            veil.setAlpha(veil_alpha);
            p.fillRect(rect(), veil);
        }

        // Determine target text rect ("How do we talk?") in overlay coordinates.
        QRectF target_rect(w * 0.25 - 95.0, h * 0.50 - 16.0, 190.0, 32.0);
        if (target_prompt_ && parentWidget()) {
            target_rect = NeonTextReveal::labelTextRect(target_prompt_, parentWidget());
        }

        // 2. Smooth feathered left-to-right glowing text reveal ("How do we talk?")
        //    seamlessly ignited as the spring tip reaches target_rect.left() (2.26..2.94s),
        //    then veil fades out (2.96..3.42s) to reveal the rest of Step 1.
        if (t >= 2.20 && t < 3.52 && target_prompt_) {
            text_reveal_.ensurePrepared(target_prompt_, target_rect, text_col, tok.dark);

            const double reveal_raw = clamp01((t - 2.26) / (2.94 - 2.26));
            const double reveal_u = ease_out_cubic(reveal_raw);
            const double neon_u = clamp01((t - 2.26) / (3.26 - 2.26));
            const double bloom_in = smooth01(2.26, 2.52);
            const double bloom_out = 1.0 - smooth01(2.96, 3.45);
            const double bloom_alpha = bloom_in * bloom_out;

            text_reveal_.paint(p, target_rect, text_col, blue, reveal_u, neon_u,
                               bloom_alpha, veil_alpha / 255.0);
        }

        // 3. Exact "hi." stroke continuing forward into the vertically-centered
        //    full-screen upright oval spring at the exact same pace as paintHiIntro,
        //    staying visible at target_rect.left() until "How do we talk?" is
        //    fully shown (2.94s), then fading out cleanly (2.88..3.08s).
        if (t <= 3.12) {
            struct Pt { double x; double y; };
            static constexpr Pt kHiMain[] = {
                {-8.0, 20.5},  {-3.5, 20.8},  {0.8, 20.6},   {3.5, 19.2},   {6.8, 15.8},
                {9.8, 11.4},   {12.2, 6.8},   {13.6, 3.4},   {13.9, 1.4},   {12.8, 0.7},
                {11.1, 1.3},   {9.3, 3.6},    {7.4, 7.5},    {5.6, 12.6},   {4.1, 18.2},
                {2.9, 23.6},   {2.2, 27.4},   {2.4, 29.0},   {3.4, 28.2},   {5.2, 25.4},
                {7.8, 21.6},   {10.6, 17.8},  {13.4, 15.0},  {15.8, 13.8},  {17.6, 14.3},
                {18.4, 16.2},  {18.1, 19.4},  {17.1, 23.2},  {16.3, 26.6},  {16.5, 28.6},
                {17.8, 29.3},  {19.8, 28.4},  {22.2, 25.8},  {24.6, 22.0},  {26.8, 17.6},
                {28.3, 14.4},  {28.8, 13.5},  {28.4, 15.2},  {27.3, 18.8},  {26.1, 23.0},
                {25.4, 26.5},  {25.9, 28.4},  {27.4, 29.0},  {29.8, 28.0},  {32.4, 25.6},
                {34.8, 22.6},  {36.6, 20.4},
            };
            static constexpr Pt kHiIDot[] = {
                {30.3, 6.9}, {31.6, 7.5}, {32.1, 8.6}, {31.5, 9.7}, {30.3, 10.2},
                {29.0, 9.7}, {28.4, 8.6}, {29.0, 7.5}, {30.3, 6.9},
            };
            static constexpr Pt kHiPeriod[] = {
                {37.9, 25.9}, {39.2, 26.5}, {39.7, 27.8}, {39.1, 29.1}, {37.9, 29.7},
                {36.7, 29.1}, {36.1, 27.8}, {36.7, 26.5}, {37.9, 25.9},
            };
            constexpr int kMainCount = static_cast<int>(sizeof(kHiMain) / sizeof(kHiMain[0]));

            const double cx = w * 0.5;
            const double cy = h * 0.499;
            const double hi_ox = cx - 41.0 * 0.5;
            const double hi_oy = cy - 31.0 * 0.5;
            const double r_max = std::min(w, h) * 0.165;

            const double coil_start_x = hi_ox + kHiMain[kMainCount - 1].x;
            const double coil_start_y = hi_oy + kHiMain[kMainCount - 1].y;
            const double coil_world_span = w * 1.85;
            const double coil_world_end = coil_start_x + coil_world_span;
            const double cam_x_final = coil_world_end - (target_rect.left() - 2.0);

            // Continuous spring motion at the exact same pace as paintHiIntro (no mid pause).
            const double p_stroke = clamp01(t / 2.38);
            const double head_u =
                p_stroke * (0.82 + 0.18 * (3.0 * p_stroke - 2.0 * p_stroke * p_stroke));
            const double cam_x = cam_x_final * smooth01(0.15, 2.38);
            const double ox = hi_ox - cam_x;

            const double dots_alpha = 1.0 - smooth01(0.28, 0.72);
            if (dots_alpha > 0.01) {
                p.save();
                QColor dc = ink;
                dc.setAlpha(static_cast<int>(255 * dots_alpha));
                p.setPen(Qt::NoPen);
                p.setBrush(dc);
                for (const auto* arr : {kHiIDot, kHiPeriod}) {
                    QPainterPath dp;
                    dp.moveTo(ox + arr[0].x, hi_oy + arr[0].y);
                    for (int i = 1; i < 9; ++i) {
                        dp.lineTo(ox + arr[i].x, hi_oy + arr[i].y);
                    }
                    dp.closeSubpath();
                    p.drawPath(dp);
                }
                p.restore();
            }

            constexpr double kHiSplit = 0.14;
            const double head_s = kHiSplit + (1.0 - kHiSplit) * head_u;
            const double tail_s = std::min(head_s - 0.012, smooth01(0.85, 3.06));

            if (head_s > tail_s + 0.002) {
                constexpr double kLoops = 12.5;
                const double pitch = coil_world_span / kLoops;
                const double loop_rx = pitch * 0.40;
                const double loop_ry = r_max * 0.90;

                auto sample_coil = [&](double u) -> QPointF {
                    const double eu = clamp01(u / 0.10);
                    const double entry_amp = eu * eu * (3.0 - 2.0 * eu);
                    const double xu = clamp01((u - 0.86) / 0.14);
                    const double exit_amp = 1.0 - xu * xu * (3.0 - 2.0 * xu);
                    const double amp = entry_amp * exit_amp;
                    const double rx = loop_rx * amp;
                    const double ry = loop_ry * amp;
                    const double wave = std::sin(2.0 * kPi * 2.0 * u + t * 2.1) *
                                        (r_max * 0.16) * clamp01(u / 0.08) * (1.0 - u);
                    const double base_x = coil_start_x + coil_world_span * u - cam_x;
                    const double mid_y =
                        coil_start_y + ((cy + r_max - loop_ry) - coil_start_y)
                                           * clamp01(u / 0.14);
                    const double end_t = clamp01((u - 0.76) / 0.24);
                    const double end_ease = end_t * end_t * (3.0 - 2.0 * end_t);
                    const double center_y =
                        mid_y + (target_rect.center().y() - mid_y) * end_ease + wave;
                    const double angle = 2.0 * kPi * kLoops * u;
                    return QPointF(base_x + rx * std::sin(angle),
                                   center_y + ry * std::cos(angle));
                };

                QPainterPath ribbon_path;
                bool started = false;
                QPointF tail_pt(ox + kHiMain[0].x, hi_oy + kHiMain[0].y);
                QPointF nib_pt = tail_pt;

                if (tail_s < kHiSplit) {
                    const double hi_start_u = clamp01(tail_s / kHiSplit);
                    const int min_idx = std::clamp(
                        static_cast<int>(std::floor(hi_start_u * (kMainCount - 1))),
                        0, kMainCount - 2);
                    const double frac = hi_start_u * (kMainCount - 1) - min_idx;
                    tail_pt = QPointF(
                        ox + kHiMain[min_idx].x + (kHiMain[min_idx + 1].x - kHiMain[min_idx].x) * frac,
                        hi_oy + kHiMain[min_idx].y + (kHiMain[min_idx + 1].y - kHiMain[min_idx].y) * frac);
                    ribbon_path.moveTo(tail_pt);
                    started = true;
                    for (int idx = min_idx + 1; idx < kMainCount - 1; ++idx) {
                        const QPointF p0(ox + kHiMain[idx].x, hi_oy + kHiMain[idx].y);
                        const QPointF p1(ox + kHiMain[idx + 1].x, hi_oy + kHiMain[idx + 1].y);
                        ribbon_path.quadTo(p0, (p0 + p1) * 0.5);
                    }
                    ribbon_path.lineTo(QPointF(ox + kHiMain[kMainCount - 1].x,
                                               hi_oy + kHiMain[kMainCount - 1].y));
                }

                if (head_s > kHiSplit) {
                    const double coil_start_u =
                        tail_s <= kHiSplit ? 0.0 : clamp01((tail_s - kHiSplit) / (1.0 - kHiSplit));
                    const double coil_end_u = clamp01((head_s - kHiSplit) / (1.0 - kHiSplit));
                    if (tail_s >= kHiSplit) {
                        tail_pt = sample_coil(coil_start_u);
                    }
                    constexpr int kCoilSamples = 420;
                    for (int i = 0; i <= kCoilSamples; ++i) {
                        const double u =
                            coil_start_u + (coil_end_u - coil_start_u) * (static_cast<double>(i) / kCoilSamples);
                        const QPointF pt = sample_coil(u);
                        if (!started) {
                            ribbon_path.moveTo(pt);
                            started = true;
                        } else {
                            ribbon_path.lineTo(pt);
                        }
                        nib_pt = pt;
                    }
                }

                const double grad_x0 = std::min(tail_pt.x(), nib_pt.x() - 220.0);
                const double grad_x1 = std::max(nib_pt.x() + 18.0, grad_x0 + 40.0);
                QLinearGradient ribbon_grad(QPointF(grad_x0, cy), QPointF(grad_x1, cy));
                ribbon_grad.setColorAt(0.00, ink);
                ribbon_grad.setColorAt(0.45, ink);
                ribbon_grad.setColorAt(0.68, violet);
                ribbon_grad.setColorAt(0.82, coral);
                ribbon_grad.setColorAt(0.92, amber);
                ribbon_grad.setColorAt(1.00, blue);

                const double ribbon_alpha = 1.0 - smooth01(2.88, 3.08);
                p.save();
                p.setOpacity(ribbon_alpha);
                p.setPen(QPen(QBrush(ribbon_grad), 3.2,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(ribbon_path);
                p.restore();
            }
        }
    }

private:
    QLabel* target_prompt_ = nullptr;
    NeonTextReveal text_reveal_;
    QElapsedTimer elapsed_;
    QTimer* timer_ = nullptr;
};

class OobeWindow final : public FinnLayerWindow {
    Q_OBJECT

public:
    explicit OobeWindow(bool update_mode = false, double fixed_phase = -1.0,
                        finn::shell::oobe::CapabilitySnapshot caps = {})
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-oobe")),
          flow_(std::move(caps)),
          update_mode_(update_mode),
          fixed_phase_(fixed_phase) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);
        setAccessibleName(QStringLiteral("FinnOS First-Time Setup"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new FinnPageStack(this);
        stack_->addWidget(buildHiPage());
        stack_->addWidget(buildLanguagePage());
        stack_->addWidget(buildWifiPage());
        stack_->addWidget(buildAccountPage());
        stack_->addWidget(buildAppearancePage());
        stack_->addWidget(buildAccessibilityPage());
        stack_->addWidget(buildPrivacyPage());   // UI 6 -> model Step::Privacy
        stack_->addWidget(buildCompletePage());  // UI 7 -> model Step::Finish
        root->addWidget(stack_, 1);


        // Shared Back/Next circular navigation bar pinned across steps 1..5 so
        // arrows stay completely stationary during page slide transitions.
        shared_nav_box_ = new QWidget(this);
        shared_nav_box_->setFixedSize(132, 56);
        auto* nav_row = new QHBoxLayout(shared_nav_box_);
        nav_row->setContentsMargins(0, 0, 0, 0);
        nav_row->setSpacing(20);
        auto* back_btn = new OobeCircleNavButton(true, false, shared_nav_box_);
        shared_next_btn_ = new OobeCircleNavButton(false, false, shared_nav_box_);
        connect(back_btn, &QAbstractButton::clicked, this, &OobeWindow::previousStep);
        connect(shared_next_btn_, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        nav_row->addWidget(back_btn);
        nav_row->addWidget(shared_next_btn_);
        shared_nav_box_->setVisible(false);
        shared_nav_box_->raise();

        gate_hint_lbl_ = new QLabel(this);
        gate_hint_lbl_->setAlignment(Qt::AlignCenter);
        gate_hint_lbl_->setFont(oobe_font(12, QFont::Normal));
        gate_hint_lbl_->setWordWrap(true);
        gate_hint_lbl_->setFixedWidth(460);
        gate_hint_lbl_->setVisible(false);

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::stateUpdated,
                this, &OobeWindow::syncNetwork);
        finn::sys::FinnDesktopServices::instance().refreshAll();

        setStep(update_mode_ ? 7 : 0);
        positionSharedNav();
    }

    void openStep(int idx) {
        if (idx >= 0 && idx < stack_->count()) setStep(idx);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        FinnLayerWindow::resizeEvent(event);
        positionSharedNav();
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.fillRect(rect(), t.bone);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QWidget* fw = focusWidget();
            if (qobject_cast<QAbstractButton*>(fw) ||
                qobject_cast<OobeOptionRow*>(fw) ||
                qobject_cast<OobeAppearanceCard*>(fw)) {
                FinnLayerWindow::keyPressEvent(event);
                return;
            }
            advanceStep();
            return;
        }
        FinnLayerWindow::keyPressEvent(event);
    }

private:
    // Helper that builds the Figma 36:53 split two-column layout:
    //   - Left prompt ("How do we talk?") at x=225..320, y=408 (24px Fixel Display Regular)
    //   - Right 350px column at x=805, y=195
    //   - Centered Back/Next 56x56 circular chevron buttons at x=574, y=631 (itemSpacing 20)
    QWidget* createSplitStepPage(const QString& icon_name,
                                 const QString& prompt_text,
                                 const QString& sub_caption,
                                 QWidget* right_column,
                                 QLabel** out_sub_label = nullptr,
                                 bool flush_right_top = false,
                                 QLabel** out_prompt_label = nullptr) {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        const OobeForm form = oobe_form();
        const int gutter = oobe_gutter();
        auto* main_col = new QVBoxLayout(page);
        // The shared Back/Next bar floats 145 px above the bottom on desktop;
        // on phone it sits in a reserved 132 dp strip so it never covers the
        // last list row.
        main_col->setContentsMargins(gutter, form == OobeForm::Phone ? gutter : 0,
                                     gutter, form == OobeForm::Phone ? 132 : 0);
        main_col->setSpacing(0);

        QBoxLayout* content_row = form == OobeForm::Phone
            ? static_cast<QBoxLayout*>(new QVBoxLayout())
            : static_cast<QBoxLayout*>(new QHBoxLayout());
        content_row->setContentsMargins(0, 0, 0, 0);
        content_row->setSpacing(form == OobeForm::Phone ? 12
                                : form == OobeForm::Tablet ? 16 : 32);

        // Left column: 46x46 icon + 40px gap + conversational question centered
        // vertically (Figma 36:53). On phone it is a stacked header instead of a
        // side column; on tablet it keeps the split but drops the desktop inset.
        auto* left_host = new QWidget(page);
        auto* left_col = new QVBoxLayout(left_host);
        left_col->setContentsMargins(form == OobeForm::Desktop ? 80 : 0, 0,
                                     form == OobeForm::Desktop ? 20 : 0, 0);
        left_col->setSpacing(0);
        if (form != OobeForm::Phone) left_col->addStretch(49);

        auto* icon_lbl = new QLabel(left_host);
        icon_lbl->setFixedSize(46, 46);
        icon_lbl->setAlignment(Qt::AlignCenter);
        left_col->addWidget(icon_lbl, 0, Qt::AlignHCenter);
        left_col->addSpacing(form == OobeForm::Phone ? 12
                             : form == OobeForm::Tablet ? 24 : 40);

        auto* prompt_lbl = new QLabel(prompt_text, left_host);
        prompt_lbl->setFont(oobe_font(form == OobeForm::Phone ? 20 : 24,
                                      QFont::Normal));
        prompt_lbl->setAlignment(Qt::AlignCenter);
        prompt_lbl->setWordWrap(true);
        prompt_lbl->setFixedWidth(qMin(300, oobe_content_width()));
        prompt_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (out_prompt_label) *out_prompt_label = prompt_lbl;
        left_col->addWidget(prompt_lbl, 0, Qt::AlignHCenter);

        QLabel* sub_lbl = nullptr;
        if (!sub_caption.isEmpty() || out_sub_label != nullptr) {
            left_col->addSpacing(10);
            sub_lbl = new QLabel(sub_caption, left_host);
            sub_lbl->setFont(oobe_font(14, QFont::Normal));
            sub_lbl->setAlignment(Qt::AlignCenter);
            sub_lbl->setWordWrap(true);
            sub_lbl->setFixedWidth(qMin(280, oobe_content_width()));
            sub_lbl->setVisible(!sub_caption.isEmpty());
            left_col->addWidget(sub_lbl, 0, Qt::AlignHCenter);
            if (out_sub_label) *out_sub_label = sub_lbl;
        }
        if (form != OobeForm::Phone) left_col->addStretch(51);

        auto sync_left_colors = [icon_lbl, icon_name, prompt_lbl, sub_lbl]() {
            const auto& t = FinnThemeManager::instance().tokens();
            const QColor ink = t.ink;
            const qreal dpr = icon_lbl->devicePixelRatioF();
            icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
                icon_name, QSize(46, 46), ink, dpr));

            QPalette p1 = prompt_lbl->palette();
            p1.setColor(QPalette::WindowText, ink);
            prompt_lbl->setPalette(p1);
            if (sub_lbl) {
                QPalette p2 = sub_lbl->palette();
                p2.setColor(QPalette::WindowText, t.dim);
                sub_lbl->setPalette(p2);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_left_colors);
        sync_left_colors();

        // Right column: Figma's x=805, y=195 on desktop; a centered card on
        // tablet and a full-width stacked list on phone.
        auto* right_host = new QWidget(page);
        auto* right_col = new QVBoxLayout(right_host);
        const int right_top = form == OobeForm::Desktop && !flush_right_top ? 195 : 0;
        right_col->setContentsMargins(0, right_top,
                                      form == OobeForm::Desktop ? 45 : 0, 0);
        right_col->setSpacing(0);
        right_col->addWidget(right_column, 1,
                             form == OobeForm::Desktop ? Qt::AlignRight
                                                       : Qt::AlignHCenter);

        if (form == OobeForm::Phone) {
            content_row->addWidget(left_host, 0, Qt::AlignHCenter);
            content_row->addWidget(right_host, 1);
        } else {
            content_row->addWidget(left_host, 1);
            content_row->addSpacing(form == OobeForm::Tablet ? 16 : 132);
            content_row->addWidget(right_host, 1, Qt::AlignRight);
        }

        main_col->addLayout(content_row, 1);
        return page;
    }

    // Step 0: Figma 36:3 ("hi") — continuous rounding arcs that draw into "hi.",
    // and only after that reveal the top Branding (36:45) and Next circle button (36:48)
    // in the exact Figma positions.
    QWidget* buildHiPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);

        hi_ribbon_ = new FinnRibbonAnimation(FinnRibbonAnimation::Mode::HiIntro, page);
        if (fixed_phase_ >= 0.0) hi_ribbon_->setPhaseTime(fixed_phase_);
        grid->addWidget(hi_ribbon_, 0, 0);

        auto* chrome_layer = new QWidget(page);
        chrome_layer->setAttribute(Qt::WA_NoSystemBackground, true);
        auto* col = new QVBoxLayout(chrome_layer);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);

        // Top stretch so Branding sits at y=180/832 (~21.6% from top)
        col->addStretch(216);

        hi_branding_ = new OobeBrandingHeader(chrome_layer);
        col->addWidget(hi_branding_, 0, Qt::AlignHCenter);

        // Middle stretch across the center where "hi." is drawn at y=50%
        col->addStretch(518);

        hi_next_btn_ = new OobeCircleNavButton(false, true, chrome_layer);
        connect(hi_next_btn_, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        col->addWidget(hi_next_btn_, 0, Qt::AlignHCenter);

        // Bottom stretch so Next button sits at y=631/832 (~75.8% from top)
        col->addStretch(174);

        grid->addWidget(chrome_layer, 0, 0);

        auto sync_reveal = [this]() {
            if (!hi_ribbon_) return;
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
        };
        connect(hi_ribbon_, &FinnRibbonAnimation::frameAdvanced, page,
                [sync_reveal](double) { sync_reveal(); });
        sync_reveal();

        return page;
    }

    // Step 1: Figma 36:53 ("How do we talk?") with world languages + progressive blur
    QWidget* buildLanguagePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);

        // Keep prog_list and blur_ov stacked inside list_wrap
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        struct LangEntry {
            const char* name;
            const char* locale;
        };
        static constexpr LangEntry kLanguages[] = {
            {"English (UK)", "en_GB.UTF-8"},
            {"English (US)", "en_US.UTF-8"},
            {"Spanish", "es_ES.UTF-8"},
            {"Italian", "it_IT.UTF-8"},
            {"French", "fr_FR.UTF-8"},
            {"Catal\u00E0", "ca_ES.UTF-8"},
            {"German", "de_DE.UTF-8"},
            {"Afrikaans", "af_ZA.UTF-8"},
            {"Chinese (Simplified)", "zh_CN.UTF-8"},
            {"Hindi", "hi_IN.UTF-8"},
            {"Korean", "ko_KR.UTF-8"},
            {"Japanese", "ja_JP.UTF-8"},
            {"Arabic", "ar_SA.UTF-8"},
            {"Chinese (Traditional)", "zh_TW.UTF-8"},
            {"Portuguese (Portugal)", "pt_PT.UTF-8"},
            {"Portuguese (Brazil)", "pt_BR.UTF-8"},
            {"Dutch", "nl_NL.UTF-8"},
            {"Polish", "pl_PL.UTF-8"},
            {"Russian", "ru_RU.UTF-8"},
            {"Ukrainian", "uk_UA.UTF-8"},
            {"Turkish", "tr_TR.UTF-8"},
            {"Swedish", "sv_SE.UTF-8"},
            {"Norwegian", "nb_NO.UTF-8"},
            {"Danish", "da_DK.UTF-8"},
            {"Finnish", "fi_FI.UTF-8"},
            {"Greek", "el_GR.UTF-8"},
            {"Czech", "cs_CZ.UTF-8"},
            {"Slovak", "sk_SK.UTF-8"},
            {"Hungarian", "hu_HU.UTF-8"},
            {"Romanian", "ro_RO.UTF-8"},
            {"Bulgarian", "bg_BG.UTF-8"},
            {"Croatian", "hr_HR.UTF-8"},
            {"Serbian", "sr_RS.UTF-8"},
            {"Slovenian", "sl_SI.UTF-8"},
            {"Estonian", "et_EE.UTF-8"},
            {"Latvian", "lv_LV.UTF-8"},
            {"Lithuanian", "lt_LT.UTF-8"},
            {"Icelandic", "is_IS.UTF-8"},
            {"Irish", "ga_IE.UTF-8"},
            {"Welsh", "cy_GB.UTF-8"},
            {"Basque", "eu_ES.UTF-8"},
            {"Galician", "gl_ES.UTF-8"},
            {"Hebrew", "he_IL.UTF-8"},
            {"Persian", "fa_IR.UTF-8"},
            {"Urdu", "ur_PK.UTF-8"},
            {"Bengali", "bn_BD.UTF-8"},
            {"Punjabi", "pa_IN.UTF-8"},
            {"Tamil", "ta_IN.UTF-8"},
            {"Telugu", "te_IN.UTF-8"},
            {"Marathi", "mr_IN.UTF-8"},
            {"Gujarati", "gu_IN.UTF-8"},
            {"Vietnamese", "vi_VN.UTF-8"},
            {"Thai", "th_TH.UTF-8"},
            {"Indonesian", "id_ID.UTF-8"},
            {"Malay", "ms_MY.UTF-8"},
            {"Filipino", "fil_PH.UTF-8"},
            {"Swahili", "sw_KE.UTF-8"},
            {"Amharic", "am_ET.UTF-8"},
        };

        // Pre-select saved locale if present in ~/.config/finn/locale.conf
        QString initial_locale = QStringLiteral("en_GB.UTF-8");
        {
            QFile lf(QDir::homePath() + QStringLiteral("/.config/finn/locale.conf"));
            if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString content = QString::fromUtf8(lf.readAll());
                for (const QString& line : content.split(QLatin1Char('\n'))) {
                    if (line.startsWith(QStringLiteral("LANG="))) {
                        const QString val = line.mid(5).trimmed();
                        if (!val.isEmpty()) initial_locale = val;
                    }
                }
            }
        }

        int initial_idx = 0;
        constexpr int kLangCount = static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));

        // The flow model validates locale/region against a real catalogue.
        // These are exactly the languages this page can select and the
        // host's real timezones, so no invented value can pass the gate.
        {
            finn::shell::oobe::LocaleCatalog catalog;
            for (int i = 0; i < kLangCount; ++i) {
                const QString loc = QString::fromLatin1(kLanguages[i].locale);
                catalog.locales.push_back(loc.toStdString());
                // Must match syncFlowFromUi(): "en_GB.UTF-8" -> "GB".
                catalog.regions.push_back(
                    loc.section(QLatin1Char('_'), -1)
                       .section(QLatin1Char('.'), 0, 0).toStdString());
            }
            for (const QString z : QTimeZone::availableTimeZoneIds()) {
                catalog.timezones.push_back(z.toStdString());
            }
            flow_.setCatalog(std::move(catalog));
        }
        for (int i = 0; i < kLangCount; ++i) {
            if (QString::fromLatin1(kLanguages[i].locale) == initial_locale) {
                initial_idx = i;
                break;
            }
        }
        selected_language_ = QString::fromUtf8(kLanguages[initial_idx].name);
        selected_locale_ = QString::fromLatin1(kLanguages[initial_idx].locale);

        language_rows_.clear();
        for (int i = 0; i < kLangCount; ++i) {
            const QString lang_name = QString::fromUtf8(kLanguages[i].name);
            const QString locale_code = QString::fromLatin1(kLanguages[i].locale);
            auto* row = new OobeOptionRow(lang_name, {}, i == initial_idx,
                                          prog_list->hostWidget());
            language_rows_.push_back(row);
            connect(row, &OobeOptionRow::clicked, this,
                    [this, i, lang_name, locale_code, blur_ov]() {
                        for (int j = 0; j < language_rows_.size(); ++j) {
                            language_rows_[j]->setSelected(j == i);
                        }
                        applyLanguageSelection(lang_name, locale_code);
                        blur_ov->invalidateCache();
                    });
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        if (selected_locale_ != QLatin1String("en_GB.UTF-8")) {
            applyLanguageSelection(selected_language_, selected_locale_);
        }

        return createSplitStepPage(QStringLiteral("language-skill"),
                                   oobe_tr(QStringLiteral("lang_prompt"), selected_locale_),
                                   {}, list_wrap, nullptr, false, &lang_prompt_lbl_);
    }

    // Step 2: Wi-Fi / Network in the same Figma 36:53 split layout
    QWidget* buildWifiPage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        wifi_prog_list_ = new OobeProgressiveList(list_wrap);
        wifi_blur_ov_ = new ProgressiveBlurOverlay(wifi_prog_list_, list_wrap);

        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(wifi_prog_list_, 0, 0);
        stack_lay->addWidget(wifi_blur_ov_, 0, 0);

        // Offline is an explicit, named choice. The model refuses to record
        // "connected" without a measured connector and a named network.
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);
        col->addWidget(list_wrap, 1);
        continue_offline_cb_ = new QCheckBox(
            QStringLiteral("Continue without connecting to a network"), host);
        connect(continue_offline_cb_, &QCheckBox::toggled, this,
                [this]() { refreshFlowGate(); });
        col->addWidget(continue_offline_cb_, 0, Qt::AlignHCenter);

        return createSplitStepPage(QStringLiteral("wifi-02"),
                                   oobe_tr(QStringLiteral("wifi_prompt"), selected_locale_),
                                   {},
                                   host, nullptr, false, &wifi_prompt_lbl_);
    }

    // Step 3: Account setup with Figma PFP color picker + account fields
    QWidget* buildAccountPage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(0);

        // Live Figma PFP preview + color variant swatches
        pfp_preview_lbl_ = new QLabel(col_host);
        pfp_preview_lbl_->setFixedSize(72, 72);
        pfp_preview_lbl_->setAlignment(Qt::AlignCenter);
        col->addWidget(pfp_preview_lbl_, 0, Qt::AlignHCenter);
        col->addSpacing(14);

        auto* swatch_row = new QWidget(col_host);
        auto* swatch_lay = new QHBoxLayout(swatch_row);
        swatch_lay->setContentsMargins(0, 0, 0, 0);
        swatch_lay->setSpacing(6);

        pfp_swatches_.clear();
        for (const auto& preset : FinnThemeManager::avatarPresets()) {
            auto* sw = new OobePfpSwatch(preset, swatch_row);
            connect(sw, &QAbstractButton::clicked, this, [id = preset.id]() {
                FinnThemeManager::instance().setAvatarColor(id);
            });
            pfp_swatches_.push_back(sw);
            swatch_lay->addWidget(sw);
        }
        col->addWidget(swatch_row, 0, Qt::AlignHCenter);
        col->addSpacing(18);

        auto sync_pfp = [this]() {
            auto& tm = FinnThemeManager::instance();
            const QString current_id = tm.avatarColor();
            if (pfp_preview_lbl_) {
                pfp_preview_lbl_->setPixmap(
                    tm.avatarPixmap(QSize(72, 72), pfp_preview_lbl_->devicePixelRatioF()));
            }
            for (auto* sw : pfp_swatches_) {
                if (sw) sw->setChecked(sw->presetId() == current_id);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                col_host, sync_pfp);
        sync_pfp();

        fullname_edit_ = new OobeInputField(QStringLiteral("Full Name"), false, col_host);
        username_edit_ = new OobeInputField(QStringLiteral("Account Name"), false, col_host);
        username_edit_->setText(QString::fromLocal8Bit(qgetenv("USER")));
        pw_edit_ = new OobeInputField(QStringLiteral("Password"), true, col_host);
        pw_verify_edit_ = new OobeInputField(QStringLiteral("Verify Password"), true, col_host);

        pw_hint_lbl_ = new QLabel(QStringLiteral("Passwords don\u2019t match"), col_host);
        pw_hint_lbl_->setFont(oobe_font(12, QFont::Normal));
        pw_hint_lbl_->setContentsMargins(6, 0, 0, 0);
        pw_hint_lbl_->setVisible(false);

        auto sync_pw_hint = [this]() {
            if (!pw_hint_lbl_ || !pw_edit_ || !pw_verify_edit_) return;
            const QString p1 = pw_edit_->text();
            const QString p2 = pw_verify_edit_->text();
            const bool mismatch = !p2.isEmpty() && p1 != p2;
            pw_hint_lbl_->setVisible(mismatch);
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = pw_hint_lbl_->palette();
            pal.setColor(QPalette::WindowText, t.danger);
            pw_hint_lbl_->setPalette(pal);
        };
        connect(pw_edit_->lineEdit(), &QLineEdit::textChanged, col_host, sync_pw_hint);
        connect(pw_verify_edit_->lineEdit(), &QLineEdit::textChanged, col_host, sync_pw_hint);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                col_host, sync_pw_hint);

        col->addWidget(fullname_edit_);
        col->addSpacing(10);
        col->addWidget(username_edit_);
        col->addSpacing(10);
        col->addWidget(pw_edit_);
        col->addSpacing(10);
        col->addWidget(pw_verify_edit_);
        col->addSpacing(6);
        col->addWidget(pw_hint_lbl_);
        col->addStretch(1);

        return createSplitStepPage(QStringLiteral("user"),
                                   oobe_tr(QStringLiteral("account_prompt"), selected_locale_), {},
                                   col_host, nullptr, false, &account_prompt_lbl_);
    }

    // Step 4: Appearance page with interactive 3D pendant lamp filling the entire right half,
    // wire anchored at the ceiling (y=0), and Back/Next navigation buttons overlaid on top.
    QWidget* buildAppearancePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* stack_grid = new QGridLayout(page);
        stack_grid->setContentsMargins(0, 0, 0, 0);
        stack_grid->setSpacing(0);

        // Base layer: left half prompt + right half full-height 3D lamp area
        auto* base_layer = new QWidget(page);
        base_layer->setAutoFillBackground(false);
        auto* halves = new QHBoxLayout(base_layer);
        halves->setContentsMargins(0, 0, 0, 0);
        halves->setSpacing(0);

        auto* left_host = new QWidget(base_layer);
        auto* left_col = new QVBoxLayout(left_host);
        left_col->setContentsMargins(80, 0, 20, 0);
        left_col->setSpacing(0);
        left_col->addStretch(49);

        auto* icon_lbl = new QLabel(left_host);
        icon_lbl->setFixedSize(46, 46);
        icon_lbl->setAlignment(Qt::AlignCenter);
        left_col->addWidget(icon_lbl, 0, Qt::AlignHCenter);
        left_col->addSpacing(40);

        appearance_prompt_lbl_ = new QLabel(
            oobe_tr(QStringLiteral("appearance_prompt"), selected_locale_), left_host);
        appearance_prompt_lbl_->setFont(oobe_font(24, QFont::Normal));
        appearance_prompt_lbl_->setAlignment(Qt::AlignCenter);
        appearance_prompt_lbl_->setWordWrap(true);
        appearance_prompt_lbl_->setFixedWidth(qMin(300, oobe_content_width()));
        left_col->addWidget(appearance_prompt_lbl_, 0, Qt::AlignHCenter);
        left_col->addStretch(51);

        auto sync_left_colors = [icon_lbl, this]() {
            const auto& t = FinnThemeManager::instance().tokens();
            const QColor ink = t.ink;
            const qreal dpr = icon_lbl->devicePixelRatioF();
            icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
                QStringLiteral("oobe-appearance"), QSize(46, 46), ink, dpr));
            if (appearance_prompt_lbl_) {
                QPalette p1 = appearance_prompt_lbl_->palette();
                p1.setColor(QPalette::WindowText, ink);
                appearance_prompt_lbl_->setPalette(p1);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_left_colors);
        sync_left_colors();

        auto* lamp = new finn::oobe::OobeLamp3DWidget(page);

        halves->addWidget(left_host, 1);
        halves->addStretch(1);
        stack_grid->addWidget(base_layer, 0, 0);
        stack_grid->addWidget(lamp, 0, 0);

        // A real Light/Dark choice. The cards were already part of the
        // FinnUI vocabulary but were not wired to the appearance step; the
        // flow model requires an explicit choice before finishing.
        auto* chooser = new QWidget(page);
        auto* chooser_row = new QHBoxLayout(chooser);
        chooser_row->setContentsMargins(0, 0, 0, 24);
        chooser_row->setSpacing(12);
        auto* light_card = new OobeAppearanceCard(false, QStringLiteral("Light"),
                                                  !FinnThemeManager::instance().isDark(),
                                                  chooser);
        auto* dark_card = new OobeAppearanceCard(true, QStringLiteral("Dark"),
                                                 FinnThemeManager::instance().isDark(),
                                                 chooser);
        connect(light_card, &OobeAppearanceCard::clicked, this, [this, light_card, dark_card]() {
            FinnThemeManager::instance().setDark(false);
            light_card->setSelected(true);
            dark_card->setSelected(false);
            selected_theme_ = finn::shell::oobe::ThemeChoice::Light;
            refreshFlowGate();
        });
        connect(dark_card, &OobeAppearanceCard::clicked, this, [this, light_card, dark_card]() {
            FinnThemeManager::instance().setDark(true);
            light_card->setSelected(false);
            dark_card->setSelected(true);
            selected_theme_ = finn::shell::oobe::ThemeChoice::Dark;
            refreshFlowGate();
        });
        chooser_row->addWidget(light_card, 0, Qt::AlignHCenter);
        chooser_row->addWidget(dark_card, 0, Qt::AlignHCenter);
        stack_grid->addWidget(chooser, 0, 0, Qt::AlignBottom | Qt::AlignHCenter);
        return page;
    }

    // Step 5: Accessibility toggles in the same Figma 36:53 split layout
    QWidget* buildAccessibilityPage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

        auto& tm = FinnThemeManager::instance();
        const auto s = finn::sys::load_merged_settings();

        auto* sr = a11y_sr_ = new OobeToggleRow(QStringLiteral("Screen Reader"),
                                                s.screen_reader, col_host);
        connect(sr, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setScreenReader(on);
        });
        col->addWidget(sr);

        auto* hc = a11y_hc_ = new OobeToggleRow(QStringLiteral("High Contrast"),
                                                tm.tokens().high_contrast, col_host);
        connect(hc, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setHighContrast(on);
        });
        col->addWidget(hc);

        auto* rm = a11y_rm_ = new OobeToggleRow(QStringLiteral("Reduce Motion"),
                                                tm.tokens().reduced_motion, col_host);
        connect(rm, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setReducedMotion(on);
        });
        col->addWidget(rm);

        auto* sk = new OobeToggleRow(QStringLiteral("Sticky Keys"),
                                     s.sticky_keys, col_host);
        connect(sk, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setStickyKeys(on);
        });
        col->addWidget(sk);

        connect(&tm, &FinnThemeManager::themeChanged, this,
                [sr, hc, rm, sk]() {
                    const auto& st = FinnThemeManager::instance().settings();
                    QSignalBlocker b1(sr->toggle());
                    QSignalBlocker b2(hc->toggle());
                    QSignalBlocker b3(rm->toggle());
                    QSignalBlocker b4(sk->toggle());
                    sr->toggle()->setChecked(st.screen_reader);
                    hc->toggle()->setChecked(st.high_contrast);
                    rm->toggle()->setChecked(st.reduced_motion);
                    sk->toggle()->setChecked(st.sticky_keys);
                });

        col->addStretch(1);
        return createSplitStepPage(QStringLiteral("accessibility"),
                                   oobe_tr(QStringLiteral("a11y_prompt"), selected_locale_), {},
                                   col_host, nullptr, false, &a11y_prompt_lbl_);
    }

    // Model step 3: an explicit privacy acknowledgement. FinnOS has no
    // telemetry, location or crash-upload service, so those rows are shown
    // disabled and named as absent rather than offered as working switches.
    QWidget* buildPrivacyPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        auto* info = new QLabel(
            QStringLiteral("FinnOS does not collect diagnostics, location or crash reports. "
                           "No telemetry service is present on this installation."), host);
        info->setWordWrap(true);
        info->setFont(oobe_font(14, QFont::Normal));
        col->addWidget(info);

        auto* loc = new OobeToggleRow(QStringLiteral("Location"), false, host);
        auto* diag = new OobeToggleRow(QStringLiteral("Diagnostics"), false, host);
        auto* crash = new OobeToggleRow(QStringLiteral("Crash reports"), false, host);
        for (auto* row : {loc, diag, crash}) {
            row->setEnabled(false);
            row->setToolTip(QStringLiteral("Service absent — not available in this release"));
            col->addWidget(row);
        }

        privacy_ack_ = new QCheckBox(
            QStringLiteral("I understand — continue with everything off"), host);
        connect(privacy_ack_, &QCheckBox::toggled, this,
                [this]() { refreshFlowGate(); });
        col->addWidget(privacy_ack_, 0, Qt::AlignHCenter);
        col->addStretch(1);

        return createSplitStepPage(QStringLiteral("cog"),
                                   QStringLiteral("How much should we remember?"), {},
                                   host);
    }

    // Step 5: Video-matched Completion / Update Successful Screen with 6-Ring Slinky
    QWidget* buildCompletePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);

        col->addStretch(8);

        auto* branding = new OobeBrandingHeader(page);
        col->addWidget(branding, 0, Qt::AlignHCenter);

        col->addSpacing(12);

        complete_ribbon_ = new FinnRibbonAnimation(
            FinnRibbonAnimation::Mode::SlinkyWave, page);
        complete_ribbon_->setFixedHeight(310);
        if (fixed_phase_ >= 0.0) complete_ribbon_->setPhaseTime(fixed_phase_);
        col->addWidget(complete_ribbon_);

        col->addSpacing(20);

        complete_title_ = new QLabel(
            update_mode_ ? QStringLiteral("Update successful")
                         : oobe_tr(QStringLiteral("complete_title"), selected_locale_),
            page);
        complete_title_->setFont(oobe_font(26, QFont::Medium));
        complete_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_title_, 0, Qt::AlignHCenter);

        auto sync_title_col = [this]() {
            if (!complete_title_) return;
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = complete_title_->palette();
            pal.setColor(QPalette::WindowText, t.ink);
            complete_title_->setPalette(pal);
            if (complete_status_lbl_) {
                QPalette spal = complete_status_lbl_->palette();
                spal.setColor(QPalette::WindowText,
                              complete_has_error_ ? t.danger : t.dim);
                complete_status_lbl_->setPalette(spal);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_title_col);

        col->addSpacing(28);

        start_btn_ = new OobeGetStartedPill(
            oobe_tr(QStringLiteral("get_started"), selected_locale_), page);
        connect(start_btn_, &QAbstractButton::clicked, this,
                &OobeWindow::finishSetup);
        col->addWidget(start_btn_, 0, Qt::AlignHCenter);

        col->addSpacing(12);
        complete_status_lbl_ = new QLabel(QString(), page);
        complete_status_lbl_->setFont(oobe_font(13, QFont::Normal));
        complete_status_lbl_->setAlignment(Qt::AlignCenter);
        complete_status_lbl_->setWordWrap(true);
        complete_status_lbl_->setFixedWidth(460);
        complete_status_lbl_->setVisible(false);
        col->addWidget(complete_status_lbl_, 0, Qt::AlignHCenter);

        sync_title_col();

        col->addStretch(14);
        return page;
    }

    QString promptWifiPassphrase(const QString& ssid) {
        const auto& t = FinnThemeManager::instance().tokens();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Wi-Fi Password"));
        dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dlg.setModal(true);
        dlg.setAccessibleName(QStringLiteral("Wi-Fi password prompt for %1").arg(ssid));

        auto* root = new QVBoxLayout(&dlg);
        root->setContentsMargins(20, 18, 20, 18);
        root->setSpacing(12);

        auto* title = new FinnLabel(
            QStringLiteral("Connect to %1").arg(ssid),
            FinnLabel::Role::Headline, FinnLabel::Tone::Ink, &dlg);
        root->addWidget(title);

        auto* field = new OobeInputField(QStringLiteral("Wi-Fi Password"), true, &dlg);
        field->lineEdit()->setAccessibleName(QStringLiteral("Wi-Fi password for %1").arg(ssid));
        root->addWidget(field);

        auto* btn_row = new QHBoxLayout();
        btn_row->setSpacing(8);
        btn_row->addStretch(1);
        auto* cancel_btn = new FinnButton(
            QStringLiteral("Cancel"), FinnButton::Variant::Secondary, {}, &dlg);
        auto* join_btn = new FinnButton(
            QStringLiteral("Connect"), FinnButton::Variant::Primary, {}, &dlg);
        cancel_btn->setAccessibleName(QStringLiteral("Cancel Wi-Fi connection"));
        join_btn->setAccessibleName(QStringLiteral("Connect to %1").arg(ssid));
        btn_row->addWidget(cancel_btn);
        btn_row->addWidget(join_btn);
        root->addLayout(btn_row);

        QPalette dpal = dlg.palette();
        dpal.setColor(QPalette::Window, t.tile);
        dpal.setColor(QPalette::WindowText, t.ink);
        dlg.setPalette(dpal);
        dlg.setAutoFillBackground(true);

        connect(cancel_btn, &QAbstractButton::clicked, &dlg, &QDialog::reject);
        connect(join_btn, &QAbstractButton::clicked, &dlg, &QDialog::accept);
        connect(field->lineEdit(), &QLineEdit::returnPressed, &dlg, &QDialog::accept);
        field->lineEdit()->setFocus();

        return (dlg.exec() == QDialog::Accepted) ? field->text() : QString();
    }

    void syncNetwork() {
        if (!wifi_prog_list_) return;
        const auto& net = finn::sys::FinnDesktopServices::instance().network();

        auto* lay = wifi_prog_list_->listLayout();
        while (QLayoutItem* item = lay->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }

        if (net.wired_connected) {
            auto* eth_row = new OobeOptionRow(
                QStringLiteral("Ethernet"), net.ipv4_summary, true,
                wifi_prog_list_->hostWidget());
            lay->addWidget(eth_row);
        }

        for (const auto& w : net.networks) {
            const QString detail =
                w.active ? QStringLiteral("Connected")
                         : QStringLiteral("%1%").arg(w.signal);
            auto* row = new OobeOptionRow(w.ssid, detail, w.active,
                                          wifi_prog_list_->hostWidget());
            connect(row, &OobeOptionRow::clicked, this, [this, w]() {
                if (w.active) return;
                QString pwd;
                if (w.secured) {
                    pwd = promptWifiPassphrase(w.ssid);
                    if (pwd.isEmpty()) return;
                }
                finn::sys::FinnDesktopServices::instance().connectWifi(w.ssid, pwd);
            });
            lay->addWidget(row);
        }

        auto* offline_row = new OobeOptionRow(
            QStringLiteral("Continue offline"), {},
            !net.wired_connected && net.active_wifi_ssid.isEmpty(),
            wifi_prog_list_->hostWidget());
        connect(offline_row, &OobeOptionRow::clicked, this,
                &OobeWindow::advanceStep);
        lay->addWidget(offline_row);
        lay->addStretch(1);

        if (wifi_blur_ov_) {
            const int row_count = (net.wired_connected ? 1 : 0) +
                                  static_cast<int>(net.networks.size()) + 1;
            wifi_blur_ov_->setVisible(row_count > 6);
            wifi_blur_ov_->invalidateCache();
        }
    }

    // Feed the model only from measurements or explicit user actions. A
    // setter that refuses inconsistent state leaves the model untouched, so
    // a stale "connected"/"created" can never survive a changed capability.
    void syncFlowFromUi() {
        using namespace finn::shell::oobe;
        const OobeStep step = step_for_ui_index(stack_->currentIndex());
        switch (step) {
            case OobeStep::Connectivity: {
                ConnectivityState st;
                const auto caps = flow_.capabilities();
                const auto& net = finn::sys::FinnDesktopServices::instance().network();
                if (caps.network_manager.state == Availability::Available &&
                    !net.active_wifi_ssid.isEmpty()) {
                    st.attempt = NetworkAttempt::Connected;
                    st.ssid = net.active_wifi_ssid.toStdString();
                    st.detail = "Wi-Fi link reported by the network service";
                } else if (caps.network_manager.state == Availability::Available &&
                           net.wired_connected) {
                    st.attempt = NetworkAttempt::Connected;
                    st.ssid = "Wired network";
                    st.detail = "Ethernet link reported by the network service";
                } else if (continue_offline_cb_ && continue_offline_cb_->isChecked()) {
                    st.attempt = caps.network_manager.state == Availability::Unavailable
                        ? NetworkAttempt::Unavailable : NetworkAttempt::Failed;
                    st.continue_offline = true;
                    st.detail = caps.network_manager.state == Availability::Unavailable
                        ? caps.network_manager.detail
                        : "No active network was reported by the network service";
                } else {
                    st.attempt = NetworkAttempt::NotAttempted;
                    st.detail = "no connection has been reported";
                }
                flow_.setConnectivity(std::move(st));
                break;
            }
            case OobeStep::Account: {
                AccountState st;
                const QString name = fullname_edit_ ? fullname_edit_->text().trimmed()
                                                    : QString();
                st.display_name = name.toStdString();
                if (!name.isEmpty()) {
                    // The local account is created by finishSetup() through
                    // pkexec; until then no remote account is claimed.
                    st.outcome = AccountOutcome::LocalOnly;
                    st.detail = "local profile; account is created at Finish";
                }
                flow_.setAccount(std::move(st));
                break;
            }
            case OobeStep::Privacy:
                flow_.setPrivacy(PrivacyState{false, false, false,
                                              privacy_ack_ && privacy_ack_->isChecked()});
                break;
            case OobeStep::RegionLocale: {
                RegionState st;
                st.locale = selected_locale_.toStdString();
                st.region = selected_locale_.section(QLatin1Char('_'), -1)
                                         .section(QLatin1Char('.'), 0, 0).toStdString();
                st.timezone = QTimeZone::systemTimeZoneId().toStdString();
                flow_.setRegion(std::move(st));
                break;
            }
            case OobeStep::Accessibility: {
                AccessibilityState st;
                const auto tri = [](OobeToggleRow* row) {
                    return !row ? TriState::Unset
                                : (row->toggle()->isChecked() ? TriState::On
                                                              : TriState::Off);
                };
                st.reduced_motion = tri(a11y_rm_);
                st.high_contrast = tri(a11y_hc_);
                st.screen_reader = tri(a11y_sr_);
                if (st.screen_reader == TriState::On &&
                    flow_.capabilities().accessibility_bus.state != Availability::Available) {
                    st.screen_reader = TriState::Unset;  // cannot claim it
                }
                flow_.setAccessibility(st);
                break;
            }
            case OobeStep::Theme:
                flow_.setTheme(ThemeState{selected_theme_});
                break;
            default:
                break;
        }
    }

    void refreshFlowGate() {
        syncFlowFromUi();
        const auto v = flow_.validate(step_for_ui_index(stack_->currentIndex()));
        if (shared_next_btn_) shared_next_btn_->setEnabled(v.can_advance);
        if (gate_hint_lbl_) {
            gate_hint_lbl_->setText(v.can_advance ? QString()
                                                   : QString::fromStdString(v.reason));
            gate_hint_lbl_->setVisible(!v.can_advance);
        }
    }

    void previousStep() {
        if (flow_.back()) {
            setStep(ui_index_for_step(flow_.current()));
            refreshFlowGate();
        }
    }

    void advanceStep() {
        const int cur = stack_->currentIndex();
        if (cur == 3 && pw_edit_ && pw_verify_edit_) {
            const QString p1 = pw_edit_->text();
            const QString p2 = pw_verify_edit_->text();
            if ((!p1.isEmpty() || !p2.isEmpty()) && p1 != p2) {
                if (pw_hint_lbl_) pw_hint_lbl_->setVisible(true);
                pw_verify_edit_->lineEdit()->setFocus();
                return;
            }
        }
        syncFlowFromUi();
        if (!flow_.advance()) {
            refreshFlowGate();
            return;
        }
        if (gate_hint_lbl_) gate_hint_lbl_->setVisible(false);
        setStep(ui_index_for_step(flow_.current()));
        refreshFlowGate();
    }

    void positionSharedNav() {
        if (!shared_nav_box_) return;
        const int x = (width() - shared_nav_box_->width()) / 2;
        // Desktop keeps Figma's y=631 on an 832 px frame (145 px from the
        // bottom). Phone moves the pair into the bottom safe-area strip.
        const int bottom = oobe_form() == OobeForm::Phone ? 24 : 145;
        const int y = height() - bottom - shared_nav_box_->height();
        shared_nav_box_->move(x, y);
        if (gate_hint_lbl_) {
            gate_hint_lbl_->move((width() - gate_hint_lbl_->width()) / 2, y - 34);
            gate_hint_lbl_->raise();
        }
    }

    QPixmap grabTransparentStack() const {
        if (!stack_ || stack_->size().isEmpty()) return {};
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(stack_->width() * dpr), qRound(stack_->height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        const bool was_visible = stack_->isVisible();
        if (!was_visible) stack_->setVisible(true);
        stack_->render(&pix, QPoint(), QRegion(), QWidget::DrawChildren);
        if (!was_visible) stack_->setVisible(false);
        return pix;
    }

    void setStep(int idx, bool animate_slide = true) {
        if (shared_nav_box_) {
            shared_nav_box_->setVisible(idx >= 1 && idx <= 6);
            shared_nav_box_->raise();
        }
        if (hi_lang_overlay_ && idx != 1) {
            hi_lang_overlay_->hide();
            hi_lang_overlay_->deleteLater();
            hi_lang_overlay_.clear();
        }
        const int prev = stack_->currentIndex();
        if (prev == idx) return;

        const bool is_hi_lang_pair = (prev == 0 && idx == 1) || (prev == 1 && idx == 0);
        const bool do_slide = animate_slide && !is_hi_lang_pair && prev >= 0 &&
                              fixed_phase_ < 0.0 &&
                              !FinnThemeManager::instance().tokens().reduced_motion;

        QPixmap old_snap;
        if (do_slide) {
            if (slide_overlay_) {
                old_snap = slide_overlay_->grabCurrentFrame();
                slide_overlay_->cancel();
                slide_overlay_.clear();
            } else {
                old_snap = grabTransparentStack();
            }
        } else if (slide_overlay_) {
            slide_overlay_->finish();
            slide_overlay_.clear();
        }

        stack_->setCurrentIndex(idx);
        if (shared_nav_box_) {
            shared_nav_box_->setVisible(idx >= 1 && idx <= 6);
            shared_nav_box_->raise();
        }
        if (idx == 0 && hi_ribbon_) {
            if (fixed_phase_ < 0.0) hi_ribbon_->restart();
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
            return;
        }
        if (idx == 7 && complete_ribbon_ && fixed_phase_ < 0.0) {
            complete_ribbon_->restart();
        }
        if (do_slide && !old_snap.isNull()) {
            stack_->widget(idx)->resize(stack_->size());
            if (stack_->widget(idx)->layout()) {
                stack_->widget(idx)->layout()->activate();
            }
            const QPixmap new_snap = grabTransparentStack();
            slide_overlay_ = new OobeSlideTransitionOverlay(
                this, stack_, std::move(old_snap), new_snap, idx > prev);
            if (shared_nav_box_) shared_nav_box_->raise();
        }
        if (idx >= 1 && idx <= 6) refreshFlowGate();
        else if (gate_hint_lbl_) gate_hint_lbl_->setVisible(false);
    }

    void applyLanguageSelection(const QString& lang_name, const QString& locale_code) {
        selected_language_ = lang_name;
        selected_locale_ = locale_code;

        const QString short_loc = locale_code.section(QLatin1Char('.'), 0, 0);
        QLocale::setDefault(QLocale(short_loc));
        qputenv("LANG", locale_code.toUtf8());
        qputenv("LC_ALL", locale_code.toUtf8());

        if (lang_prompt_lbl_)
            lang_prompt_lbl_->setText(oobe_tr(QStringLiteral("lang_prompt"), locale_code));
        if (wifi_prompt_lbl_)
            wifi_prompt_lbl_->setText(oobe_tr(QStringLiteral("wifi_prompt"), locale_code));
        if (account_prompt_lbl_)
            account_prompt_lbl_->setText(oobe_tr(QStringLiteral("account_prompt"), locale_code));
        if (appearance_prompt_lbl_)
            appearance_prompt_lbl_->setText(oobe_tr(QStringLiteral("appearance_prompt"), locale_code));
        if (a11y_prompt_lbl_)
            a11y_prompt_lbl_->setText(oobe_tr(QStringLiteral("a11y_prompt"), locale_code));
        if (complete_title_ && !update_mode_)
            complete_title_->setText(oobe_tr(QStringLiteral("complete_title"), locale_code));
        if (start_btn_)
            start_btn_->setText(oobe_tr(QStringLiteral("get_started"), locale_code));

        const QString conf_path =
            QDir::homePath() + QStringLiteral("/.config/finn/locale.conf");
        QDir().mkpath(QFileInfo(conf_path).absolutePath());
        QFile lf(conf_path);
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray body =
                QStringLiteral("LANG=%1\nLC_ALL=%1\nLANGUAGE=%2\n")
                    .arg(locale_code, short_loc)
                    .toUtf8();
            lf.write(body);
        }
    }

    void writeCompleteMarkerAndClose() {
        const QString marker =
            QDir::homePath() + QLatin1Char('/') +
            QString::fromLatin1(finn::kOobeCompleteRel);
        QDir().mkpath(QFileInfo(marker).absolutePath());
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("done\n");
        }
        // The headless flow's audit is written as-is. It lists the real
        // capability outcomes and never promotes Unknown/Unavailable to
        // success. The completion marker above is the only boot-visible fact.
        const QString audit_path = QDir::homePath() +
            QStringLiteral("/.local/state/finn/oobe-audit");
        QDir().mkpath(QFileInfo(audit_path).absolutePath());
        QFile af(audit_path);
        if (af.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream out(&af);
            for (const std::string& entry : flow_.audit()) {
                out << QString::fromStdString(entry) << '\n';
            }
            out << "summary|" << QString::fromStdString(flow_.report().summary) << '\n';
        }
        close();
    }

    void setCompleteStatus(const QString& msg, bool is_error) {
        complete_has_error_ = is_error;
        if (!complete_status_lbl_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette spal = complete_status_lbl_->palette();
        spal.setColor(QPalette::WindowText, is_error ? t.danger : t.dim);
        complete_status_lbl_->setPalette(spal);
        complete_status_lbl_->setText(msg);
        complete_status_lbl_->setVisible(!msg.isEmpty());
    }

    void finishSetup() {
        if (finish_in_progress_) return;
        if (finish_allow_continue_) {
            writeCompleteMarkerAndClose();
            return;
        }

        const QString full_name =
            fullname_edit_ ? fullname_edit_->text().trimmed() : QString();
        const QString env_user = QString::fromLocal8Bit(qgetenv("USER")).trimmed();
        const QString raw_user =
            username_edit_ ? username_edit_->text().trimmed() : env_user;
        const QString target_user = raw_user.isEmpty() ? env_user : raw_user;
        const QString pw1 = pw_edit_ ? pw_edit_->text() : QString();
        const QString pw2 = pw_verify_edit_ ? pw_verify_edit_->text() : QString();

        if ((!pw1.isEmpty() || !pw2.isEmpty()) && pw1 != pw2) {
            setStep(3);
            if (pw_hint_lbl_) {
                pw_hint_lbl_->setText(QStringLiteral("Passwords don\u2019t match"));
                pw_hint_lbl_->setVisible(true);
            }
            if (pw_verify_edit_) pw_verify_edit_->lineEdit()->setFocus();
            return;
        }
        static const QRegularExpression kUserRe(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
        if (!target_user.isEmpty() && !kUserRe.match(target_user).hasMatch()) {
            setStep(3);
            if (pw_hint_lbl_) {
                pw_hint_lbl_->setText(
                    QStringLiteral("Account name must match [a-z_][a-z0-9_-]*"));
                pw_hint_lbl_->setVisible(true);
            }
            if (username_edit_) username_edit_->lineEdit()->setFocus();
            return;
        }

        applyLanguageSelection(selected_language_, selected_locale_);

        // Persist user profile metadata locally so settings/shell can read it even
        // before any privileged system account update.
        const QString account_conf =
            QDir::homePath() + QStringLiteral("/.config/finn/account.conf");
        QDir().mkpath(QFileInfo(account_conf).absolutePath());
        {
            QFile af(account_conf);
            if (af.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                const QString body =
                    QStringLiteral("username=%1\nfull_name=%2\navatar_color=%3\nlocale=%4\n")
                        .arg(target_user, full_name,
                             FinnThemeManager::instance().avatarColor(),
                             selected_locale_);
                af.write(body.toUtf8());
            }
        }

        struct PendingOp {
            QString bin;
            QStringList args;
            QByteArray stdin_data;
            QString label;
        };
        auto ops = std::make_shared<QVector<PendingOp>>();
        if (!selected_locale_.isEmpty()) {
            ops->push_back({QStringLiteral("localectl"),
                            {QStringLiteral("set-locale"),
                             QStringLiteral("LANG=") + selected_locale_},
                            {},
                            QStringLiteral("System locale (%1)").arg(selected_locale_)});
        }
        if (!target_user.isEmpty() && !pw1.isEmpty()) {
            const QByteArray payload = (target_user + QLatin1Char(':') + pw1 + QLatin1Char('\n')).toUtf8();
            ops->push_back({QStringLiteral("pkexec"),
                            {QStringLiteral("chpasswd")},
                            payload,
                            QStringLiteral("Account password")});
        }
        if (!target_user.isEmpty() && !full_name.isEmpty()) {
            ops->push_back({QStringLiteral("chfn"),
                            {QStringLiteral("-f"), full_name, target_user},
                            {},
                            QStringLiteral("Full name")});
        }

        if (ops->isEmpty()) {
            writeCompleteMarkerAndClose();
            return;
        }

        finish_in_progress_ = true;
        if (start_btn_) start_btn_->setEnabled(false);
        setCompleteStatus(QStringLiteral("Applying language and account settings..."), false);

        auto errs = std::make_shared<QStringList>();
        auto run_next = std::make_shared<std::function<void(int)>>();
        *run_next = [this, ops, errs, run_next](int idx) {
            if (idx >= ops->size()) {
                finish_in_progress_ = false;
                if (start_btn_) start_btn_->setEnabled(true);
                if (errs->isEmpty()) {
                    writeCompleteMarkerAndClose();
                } else {
                    finish_allow_continue_ = true;
                    if (start_btn_) start_btn_->setText(QStringLiteral("Continue"));
                    setCompleteStatus(
                        QStringLiteral("Saved local config, but system step(s) failed: %1")
                            .arg(errs->join(QStringLiteral("; "))),
                        true);
                }
                return;
            }
            const PendingOp op = (*ops)[idx];
            auto* proc = new QProcess(this);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [proc, op, errs, run_next, idx](int code, QProcess::ExitStatus status) {
                        if (status != QProcess::NormalExit || code != 0) {
                            const QString e = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                            errs->append(e.isEmpty()
                                             ? QStringLiteral("%1 (exit %2)").arg(op.label).arg(code)
                                             : QStringLiteral("%1: %2").arg(op.label, e));
                        }
                        proc->deleteLater();
                        (*run_next)(idx + 1);
                    });
            connect(proc, &QProcess::errorOccurred, this,
                    [proc, op, errs, run_next, idx](QProcess::ProcessError) {
                        errs->append(QStringLiteral("%1 (%2 unavailable)").arg(op.label, op.bin));
                        proc->deleteLater();
                        (*run_next)(idx + 1);
                    });
            if (!op.stdin_data.isEmpty()) {
                connect(proc, &QProcess::started, this, [proc, data = op.stdin_data]() {
                    proc->write(data);
                    proc->closeWriteChannel();
                });
            }
            proc->start(op.bin, op.args);
        };
        (*run_next)(0);
    }

    // The headless, honest step machine. It owns no UI and claims no success;
    // every state below is fed from a real probe or a real user action.
    finn::shell::oobe::FullOobeFlow flow_;
    OobeCircleNavButton* shared_next_btn_ = nullptr;
    QLabel* gate_hint_lbl_ = nullptr;
    QCheckBox* continue_offline_cb_ = nullptr;
    QCheckBox* privacy_ack_ = nullptr;
    OobeToggleRow* a11y_sr_ = nullptr;
    OobeToggleRow* a11y_hc_ = nullptr;
    OobeToggleRow* a11y_rm_ = nullptr;
    finn::shell::oobe::ThemeChoice selected_theme_ =
        finn::shell::oobe::ThemeChoice::Unset;

    bool update_mode_ = false;
    double fixed_phase_ = -1.0;
    QString selected_language_ = QStringLiteral("English (UK)");
    QString selected_locale_ = QStringLiteral("en_GB.UTF-8");
    FinnPageStack* stack_ = nullptr;
    QWidget* shared_nav_box_ = nullptr;
    QPointer<OobeSlideTransitionOverlay> slide_overlay_;
    QPointer<OobeHiToLangOverlay> hi_lang_overlay_;
    FinnRibbonAnimation* hi_ribbon_ = nullptr;
    OobeBrandingHeader* hi_branding_ = nullptr;
    OobeCircleNavButton* hi_next_btn_ = nullptr;
    QLabel* lang_prompt_lbl_ = nullptr;
    QLabel* wifi_prompt_lbl_ = nullptr;
    QLabel* account_prompt_lbl_ = nullptr;
    QLabel* appearance_prompt_lbl_ = nullptr;
    QLabel* a11y_prompt_lbl_ = nullptr;
    FinnRibbonAnimation* complete_ribbon_ = nullptr;
    QLabel* complete_title_ = nullptr;
    QLabel* complete_status_lbl_ = nullptr;
    OobeGetStartedPill* start_btn_ = nullptr;
    bool finish_in_progress_ = false;
    bool finish_allow_continue_ = false;
    bool complete_has_error_ = false;

    QVector<OobeOptionRow*> language_rows_;
    OobeProgressiveList* wifi_prog_list_ = nullptr;
    ProgressiveBlurOverlay* wifi_blur_ov_ = nullptr;

    QLabel* pfp_preview_lbl_ = nullptr;
    QVector<OobePfpSwatch*> pfp_swatches_;
    OobeInputField* fullname_edit_ = nullptr;
    OobeInputField* username_edit_ = nullptr;
    OobeInputField* pw_edit_ = nullptr;
    OobeInputField* pw_verify_edit_ = nullptr;
    QLabel* pw_hint_lbl_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    int initial_step = -1;
    bool update_mode = false;
    double fixed_phase = -1.0;
    QString capabilities_path;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--capabilities") && i + 1 < argc) {
            capabilities_path = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--step") && i + 1 < argc) {
            bool ok = false;
            const int parsed = QString::fromUtf8(argv[++i]).toInt(&ok);
            if (ok) initial_step = parsed;
        } else if (arg == QLatin1String("--phase") && i + 1 < argc) {
            bool ok = false;
            const double parsed = QString::fromUtf8(argv[++i]).toDouble(&ok);
            if (ok) fixed_phase = parsed;
        } else if (arg == QLatin1String("--update") ||
                   arg == QLatin1String("--update-complete") ||
                   arg == QLatin1String("--boot")) {
            update_mode = true;
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("oobe"));
    // Prefer the shell's measured preflight. If OOBE was started directly,
    // probe here instead — a missing file is never treated as success.
    bool preflight_ok = false;
    if (capabilities_path.isEmpty()) {
        capabilities_path = QDir::homePath() + QLatin1Char('/') +
            QString::fromLatin1(finn::shell::oobe::probe::kOobeCapabilitiesRel);
    }
    auto measured_caps = finn::shell::oobe::probe::read_preflight(
        capabilities_path, &preflight_ok);
    if (!preflight_ok) measured_caps = finn::shell::oobe::probe::capabilities();
    OobeWindow w(update_mode, fixed_phase, measured_caps);
    if (initial_step >= 0) w.openStep(initial_step);
    w.show();
    return app.exec();
}

#include "main.moc"
