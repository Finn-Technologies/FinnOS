#include "intro_overlay.hpp"

#include <QAbstractButton>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace flourish {

namespace {

class SpaceRowButton final : public QAbstractButton {
    Q_OBJECT

public:
    SpaceRowButton(const BrowserSpace& space, const QString& subtitle,
                   bool selected, QWidget* parent = nullptr)
        : QAbstractButton(parent),
          space_(space),
          subtitle_(subtitle),
          selected_(selected) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(52);
        setAttribute(Qt::WA_Hover, true);
    }

    void setSelectedState(bool selected) {
        selected_ = selected;
        update();
    }

    QString spaceId() const { return space_.id; }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = selected_ ? t.nav_sel : t.tile;
        if (!selected_ && hovered_) {
            bg = t.surface_hover;
        }
        p.setBrush(bg);
        p.setPen(QPen(selected_ ? space_.accent : t.hair, selected_ ? 1.5 : 1.0));
        p.drawRoundedRect(r, finn::ui::kRadiusLg, finn::ui::kRadiusLg);

        // Space Hugeicon
        const QString icon_name =
            space_.icon_name.isEmpty() ? QStringLiteral("home-09") : space_.icon_name;
        const QPixmap ico = finn::ui::FinnThemeManager::instance().iconPixmap(
            icon_name, QSize(18, 18), selected_ ? space_.accent : t.dim, devicePixelRatioF());
        p.drawPixmap(14, (height() - 18) / 2, ico);

        // Space name + subtitle
        p.setFont(finn::ui::finn_font(12.0, QFont::Medium));
        p.setPen(t.ink);
        p.drawText(QRect(42, 8, width() - 80, 18), Qt::AlignVCenter | Qt::AlignLeft, space_.name);

        p.setFont(finn::ui::finn_font(10.0, QFont::Normal));
        p.setPen(t.dim);
        p.drawText(QRect(42, 27, width() - 80, 16), Qt::AlignVCenter | Qt::AlignLeft, subtitle_);

        if (selected_) {
            p.setBrush(space_.accent);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(width() - 24.0, (height() - 8.0) / 2.0, 8.0, 8.0));
        }
    }

    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    BrowserSpace space_;
    QString subtitle_;
    bool selected_ = false;
    bool hovered_ = false;
};

class LiveSpacePreview final : public QWidget {
    Q_OBJECT

public:
    explicit LiveSpacePreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(460, 380);
    }

    void setSpace(const BrowserSpace& space) {
        space_ = space;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(t.tile);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(frame, finn::ui::kRadiusXl, finn::ui::kRadiusXl);

        // Mini sidebar on the left
        const qreal sb_w = 152.0;
        const QRectF sb_rect(frame.left(), frame.top(), sb_w, frame.height());
        QPainterPath sb_clip;
        sb_clip.addRoundedRect(frame, finn::ui::kRadiusXl, finn::ui::kRadiusXl);
        p.save();
        p.setClipPath(sb_clip);
        p.fillRect(sb_rect, t.bone);

        // Hairline vertical divider
        p.setPen(QPen(t.hair, 1.0));
        p.drawLine(QPointF(sb_w, frame.top()), QPointF(sb_w, frame.bottom()));

        // Mini URL pill
        const QRectF url_pill(14, 16, sb_w - 28, 26);
        p.setBrush(t.surface_input);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(url_pill, 6, 6);
        p.setFont(finn::ui::finn_font(9.5, QFont::Normal));
        p.setPen(t.dim);
        const QString host = space_.id == QStringLiteral("work")
                                 ? QStringLiteral("linear.app")
                             : space_.id == QStringLiteral("dev")
                                 ? QStringLiteral("finnos.org/docs")
                                 : QStringLiteral("google.com");
        p.drawText(url_pill.adjusted(10, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, host);

        // Mini favorite tiles
        const qreal tile_w = (sb_w - 28 - 8) / 2.0;
        for (int i = 0; i < 2; ++i) {
            const QRectF ft(14 + i * (tile_w + 8), 52, tile_w, 34);
            p.setBrush(t.tile);
            p.setPen(QPen(t.hair, 1.0));
            p.drawRoundedRect(ft, 6, 6);
        }

        // Sample tabs for the chosen space
        QStringList sample_tabs;
        if (space_.id == QStringLiteral("work")) {
            sample_tabs = {QStringLiteral("Roadmap Q4"), QStringLiteral("Design Review"), QStringLiteral("Calendar")};
        } else if (space_.id == QStringLiteral("dev")) {
            sample_tabs = {QStringLiteral("FinnOS Docs"), QStringLiteral("FinnSDK Docs"), QStringLiteral("localhost:3000")};
        } else {
            sample_tabs = {QStringLiteral("Google"), QStringLiteral("Wikipedia"), QStringLiteral("Reading List")};
        }

        for (int i = 0; i < sample_tabs.size(); ++i) {
            const QRectF row(12, 108 + i * 32, sb_w - 24, 26);
            const bool active = (i == 0);
            if (active) {
                p.setBrush(t.nav_sel);
                p.setPen(QPen(t.hair, 1.0));
                p.drawRoundedRect(row, 5, 5);
                p.setBrush(space_.accent);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(QRectF(14, row.top() + 5, 2.5, row.height() - 10), 1.2, 1.2);
            }
            p.setFont(finn::ui::finn_font(9.5, active ? QFont::Medium : QFont::Normal));
            p.setPen(active ? t.ink : t.dim);
            p.drawText(row.adjusted(12, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, sample_tabs[i]);
        }

        // Bottom space icon bar in mini sidebar (icon-only, Arc-style)
        const QString sp_icons[3] = {
            QStringLiteral("home-09"),
            QStringLiteral("briefcase-01"),
            QStringLiteral("code-01"),
        };
        const QString sp_ids[3] = {
            QStringLiteral("personal"),
            QStringLiteral("work"),
            QStringLiteral("dev"),
        };
        for (int i = 0; i < 3; ++i) {
            const QRectF ic_box(14 + i * 28, frame.height() - 36, 24, 22);
            const bool cur = (space_.id == sp_ids[i]);
            if (cur) {
                p.setBrush(t.nav_sel);
                p.setPen(QPen(space_.accent, 1.0));
                p.drawRoundedRect(ic_box, 5, 5);
            }
            const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
                sp_icons[i], QSize(13, 13), cur ? space_.accent : t.dim, devicePixelRatioF());
            p.drawPixmap(static_cast<int>(ic_box.left()) + 5,
                         static_cast<int>(ic_box.top()) + 4, pm);
        }

        // Right inset web canvas
        const QRectF canvas(sb_w + 12, 16, frame.width() - sb_w - 24, frame.height() - 32);
        p.setBrush(t.bone);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(canvas, 8, 8);

        // Quiet editorial skeleton inside web canvas
        const qreal cx = canvas.left() + 28;
        const qreal cy = canvas.top() + 34;
        p.setBrush(space_.accent);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(cx, cy, 48, 6), 3, 3);

        p.setBrush(t.ink);
        p.setOpacity(0.14);
        p.drawRoundedRect(QRectF(cx, cy + 20, canvas.width() - 80, 14), 4, 4);
        p.drawRoundedRect(QRectF(cx, cy + 44, canvas.width() - 120, 10), 4, 4);
        p.drawRoundedRect(QRectF(cx, cy + 62, canvas.width() - 96, 10), 4, 4);

        p.setOpacity(0.07);
        p.drawRoundedRect(QRectF(cx, cy + 96, (canvas.width() - 68) / 2.0, 96), 8, 8);
        p.drawRoundedRect(QRectF(cx + (canvas.width() - 68) / 2.0 + 12, cy + 96,
                                 (canvas.width() - 68) / 2.0, 96), 8, 8);

        p.restore();
    }

private:
    BrowserSpace space_{QStringLiteral("personal"), QStringLiteral("Personal"),
                        QStringLiteral("home-09"), QColor(0x00, 0x7B, 0xFF)};
};

}  // namespace

IntroOverlay::IntroOverlay(const QVector<BrowserSpace>& spaces,
                           const QString& active_space_id,
                           QWidget* parent)
    : QWidget(parent), spaces_(spaces), chosen_space_id_(active_space_id) {
    opacity_ = new QGraphicsOpacityEffect(this);
    opacity_->setOpacity(0.0);
    setGraphicsEffect(opacity_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 12, 16);
    root->setSpacing(0);

    // Top-right window controls so full-screen intro keeps standard CSD controls
    auto* top_row = new QHBoxLayout();
    top_row->addStretch(1);
    controls_ = new finn::ui::FinnWindowControls(this);
    connect(controls_, &finn::ui::FinnWindowControls::minimizeRequested,
            this, &IntroOverlay::minimizeRequested);
    connect(controls_, &finn::ui::FinnWindowControls::maximizeRequested,
            this, &IntroOverlay::maximizeRequested);
    connect(controls_, &finn::ui::FinnWindowControls::closeRequested,
            this, &IntroOverlay::closeRequested);
    top_row->addWidget(controls_);
    root->addLayout(top_row);

    // Asymmetric split body
    auto* body = new QHBoxLayout();
    body->setContentsMargins(56, 24, 56, 40);
    body->setSpacing(56);

    // Left editorial column
    auto* left = new QVBoxLayout();
    left->setSpacing(12);
    left->addStretch(1);

    auto* brand_row = new QHBoxLayout();
    brand_row->setSpacing(8);
    auto* brand_icon = new QLabel(this);
    brand_icon->setFixedSize(20, 20);
    brand_icon->setPixmap(finn::ui::FinnThemeManager::instance().iconPixmap(
        QStringLiteral("browser"), QSize(18, 18), {}, devicePixelRatioF()));
    brand_row->addWidget(brand_icon);
    brand_row->addWidget(new finn::ui::FinnLabel(
        QStringLiteral("Flourish"), finn::ui::FinnLabel::Role::Headline,
        finn::ui::FinnLabel::Tone::Ink, this));
    brand_row->addStretch(1);
    left->addLayout(brand_row);

    auto* heading = new finn::ui::FinnLabel(
        QStringLiteral("Browse in spaces"),
        finn::ui::FinnLabel::Role::TitleLarge,
        finn::ui::FinnLabel::Tone::Ink, this);
    auto* sub = new finn::ui::FinnLabel(
        QStringLiteral("Keep personal, work, and project tabs separate."),
        finn::ui::FinnLabel::Role::Body,
        finn::ui::FinnLabel::Tone::Dim, this);
    sub->setWordWrap(true);
    left->addWidget(heading);
    left->addWidget(sub);
    left->addSpacing(12);

    auto* preview = new LiveSpacePreview(this);
    preview_ = preview;

    auto* spaces_col = new QVBoxLayout();
    spaces_col->setSpacing(8);
    QVector<SpaceRowButton*> row_btns;
    for (int i = 0; i < std::min<int>(spaces_.size(), 3); ++i) {
        const auto& sp = spaces_[i];
        const QString desc = sp.id == QStringLiteral("work")
                                 ? QStringLiteral("Meetings, docs, and team tools")
                             : sp.id == QStringLiteral("dev")
                                 ? QStringLiteral("Repositories, references, and local servers")
                                 : QStringLiteral("Everyday search and reading");
        auto* btn = new SpaceRowButton(sp, desc, sp.id == chosen_space_id_, this);
        row_btns.push_back(btn);
        spaces_col->addWidget(btn);
        if (sp.id == chosen_space_id_) {
            preview->setSpace(sp);
        }
    }
    for (int i = 0; i < row_btns.size(); ++i) {
        auto* btn = row_btns[i];
        const BrowserSpace sp = spaces_[i];
        connect(btn, &QAbstractButton::clicked, this, [this, btn, row_btns, preview, sp]() {
            chosen_space_id_ = btn->spaceId();
            for (auto* other : row_btns) {
                other->setSelectedState(other == btn);
            }
            preview->setSpace(sp);
        });
    }
    left->addLayout(spaces_col);
    left->addSpacing(16);

    auto* start_btn = new finn::ui::FinnButton(
        QStringLiteral("Start Browsing"),
        finn::ui::FinnButton::Variant::Primary, {}, this);
    start_btn->setFixedHeight(38);
    connect(start_btn, &QAbstractButton::clicked, this, &IntroOverlay::dismissWithAnimation);
    left->addWidget(start_btn, 0, Qt::AlignLeft);
    left->addStretch(1);

    // Right live preview column
    auto* right = new QVBoxLayout();
    right->setSpacing(12);
    right->addStretch(1);
    right->addWidget(preview, 1);

    auto* hints = new finn::ui::FinnLabel(
        QStringLiteral("Ctrl+T Command Bar  ·  Ctrl+S Sidebar  ·  Ctrl+Shift+D Split View"),
        finn::ui::FinnLabel::Role::Caption,
        finn::ui::FinnLabel::Tone::Mute, this);
    right->addWidget(hints, 0, Qt::AlignCenter);
    right->addStretch(1);

    body->addLayout(left, 4);
    body->addLayout(right, 6);
    root->addLayout(body, 1);
}

void IntroOverlay::syncToWindowBounds(const QRect& frame_rect, bool maximized) {
    maximized_ = maximized;
    setGeometry(frame_rect);
    if (controls_) {
        controls_->setMaximizedState(maximized);
    }
}

void IntroOverlay::playEntrance() {
    show();
    raise();

    auto* anim = new QPropertyAnimation(opacity_, "opacity", this);
    anim->setDuration(240);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void IntroOverlay::dismissWithAnimation() {
    auto* anim = new QPropertyAnimation(opacity_, "opacity", this);
    anim->setDuration(180);
    anim->setStartValue(opacity_->opacity());
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(anim, &QAbstractAnimation::finished, this, [this]() {
        hide();
        emit finished(chosen_space_id_);
        deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void IntroOverlay::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = maximized_ ? 0.0 : finn::ui::kRadiusXl;
    p.setBrush(t.bone);
    p.setPen(maximized_ ? QPen(Qt::NoPen) : QPen(t.hair, 1.0));
    p.drawRoundedRect(r, radius, radius);
}

}  // namespace flourish

#include "intro_overlay.moc"
