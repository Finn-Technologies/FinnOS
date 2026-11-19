#include "command_bar.hpp"

#include <QAbstractButton>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace flourish {

namespace {

class CommandRow final : public QAbstractButton {
    Q_OBJECT

public:
    CommandRow(const QString& title, const QString& subtitle,
               const QString& badge, bool selected, QWidget* parent = nullptr)
        : QAbstractButton(parent), selected_(selected) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(38);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(title);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 0, 12, 0);
        lay->setSpacing(10);

        auto* title_lbl = new finn::ui::FinnLabel(
            title, finn::ui::FinnLabel::Role::Body,
            finn::ui::FinnLabel::Tone::Ink, this);
        title_lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        lay->addWidget(title_lbl, 0);

        if (!subtitle.isEmpty()) {
            auto* sub_lbl = new finn::ui::FinnLabel(
                subtitle, finn::ui::FinnLabel::Role::Caption,
                finn::ui::FinnLabel::Tone::Dim, this);
            sub_lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            lay->addWidget(sub_lbl, 1);
        } else {
            lay->addStretch(1);
        }

        if (!badge.isEmpty()) {
            auto* b = new finn::ui::FinnBadge(
                badge, selected ? finn::ui::FinnBadge::Tone::Accent
                                : finn::ui::FinnBadge::Tone::Neutral,
                this);
            b->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            lay->addWidget(b, 0, Qt::AlignVCenter);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (selected_ || hovered_) {
            const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setBrush(selected_ ? t.nav_sel : t.surface_hover);
            p.setPen(selected_ ? QPen(t.accent, 1.0) : QPen(Qt::NoPen));
            p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
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
    bool selected_ = false;
    bool hovered_ = false;
};

}  // namespace

CommandBarOverlay::CommandBarOverlay(QWidget* parent) : QWidget(parent) {
    hide();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 72, 24, 24);
    outer->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    panel_ = new finn::ui::FinnGlassPanel(finn::ui::kRadiusXl, this);
    panel_->setFixedWidth(560);
    panel_opacity_ = new QGraphicsOpacityEffect(panel_);
    panel_opacity_->setOpacity(0.0);
    panel_->setGraphicsEffect(panel_opacity_);
    auto* col = new QVBoxLayout(panel_);
    col->setContentsMargins(16, 16, 16, 12);
    col->setSpacing(12);

    input_ = new finn::ui::FinnSearchField(
        QStringLiteral("Search or enter address"), panel_);
    input_->setAccessibleName(QStringLiteral("Command bar address or search input"));
    input_->setFixedHeight(38);
    input_->lineEdit()->installEventFilter(this);
    col->addWidget(input_);

    col->addWidget(new finn::ui::FinnDivider(Qt::Horizontal, panel_));

    rows_layout_ = new QVBoxLayout();
    rows_layout_->setContentsMargins(0, 0, 0, 0);
    rows_layout_->setSpacing(3);
    col->addLayout(rows_layout_);

    outer->addWidget(panel_);

    connect(input_, &finn::ui::FinnSearchField::textChanged,
            this, &CommandBarOverlay::rebuildSuggestions);
    connect(input_, &finn::ui::FinnSearchField::submitted,
            this, [this](const QString&) { executeSelected(); });
}

void CommandBarOverlay::open(Mode mode,
                             const QUrl& current_url,
                             const QVector<BrowserSpace>& spaces,
                             const QVector<BrowserTab>& tabs) {
    mode_ = mode;
    spaces_ = spaces;
    tabs_ = tabs;

    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    if (mode == Mode::EditCurrentUrl && current_url.isValid() &&
        current_url.scheme() != QStringLiteral("flourish")) {
        input_->setText(current_url.toString());
        input_->lineEdit()->selectAll();
    } else {
        input_->setText(QString());
    }

    rebuildSuggestions(input_->text());
    show();
    raise();
    input_->focusInput();

    auto* anim = new QPropertyAnimation(panel_opacity_, "opacity", this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommandBarOverlay::dismiss() {
    hide();
}

bool CommandBarOverlay::eventFilter(QObject* obj, QEvent* event) {
    if (obj == input_->lineEdit() && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        if (ke->key() == Qt::Key_Down && !entries_.isEmpty()) {
            selected_index_ = (selected_index_ + 1) % entries_.size();
            renderRows();
            return true;
        }
        if (ke->key() == Qt::Key_Up && !entries_.isEmpty()) {
            selected_index_ = (selected_index_ - 1 + entries_.size()) % entries_.size();
            renderRows();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CommandBarOverlay::mousePressEvent(QMouseEvent* event) {
    if (!panel_->geometry().contains(event->pos())) {
        dismiss();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CommandBarOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    QColor scrim = t.bone;
    scrim.setAlpha(t.dark ? 150 : 90);
    p.fillRect(rect(), scrim);
}

void CommandBarOverlay::rebuildSuggestions(const QString& query) {
    entries_.clear();
    selected_index_ = 0;

    const QString trimmed = query.trimmed();
    if (!trimmed.isEmpty()) {
        const QUrl target = normalize_user_url(trimmed);
        const bool is_search = target.host() == QStringLiteral("www.google.com") &&
                               target.path() == QStringLiteral("/search") &&
                               target.hasQuery();
        entries_.push_back({
            Entry::Kind::Navigate,
            is_search ? QStringLiteral("Search \"%1\"").arg(trimmed)
                      : QStringLiteral("Open %1").arg(display_host_for_url(target)),
            target.toString(),
            mode_ == Mode::EditCurrentUrl ? QStringLiteral("Current Tab")
                                          : QStringLiteral("New Tab"),
            {},
            target,
        });
    }

    const QString needle = trimmed.toLower();
    for (const auto& tab : tabs_) {
        if (!needle.isEmpty() &&
            !tab.title.toLower().contains(needle) &&
            !tab.url.toString().toLower().contains(needle)) {
            continue;
        }
        entries_.push_back({
            Entry::Kind::SwitchTab,
            tab.title.isEmpty() ? display_host_for_url(tab.url) : tab.title,
            display_host_for_url(tab.url),
            tab.favorite ? QStringLiteral("Favorite") : QStringLiteral("Switch"),
            tab.id,
            tab.url,
        });
        if (entries_.size() >= 6) break;
    }

    for (const auto& space : spaces_) {
        if (!needle.isEmpty() && !space.name.toLower().contains(needle)) continue;
        entries_.push_back({
            Entry::Kind::SwitchSpace,
            QStringLiteral("Switch to %1 Space").arg(space.name),
            {},
            QStringLiteral("Space"),
            space.id,
            {},
        });
    }

    struct ActionDef {
        const char* id;
        const char* title;
        const char* shortcut;
    };
    const ActionDef actions[] = {
        {"split", "Toggle Split View", "Ctrl+Shift+D"},
        {"sidebar", "Toggle Sidebar", "Ctrl+S"},
        {"dark", "Toggle Dark Mode", "Theme"},
        {"downloads", "Show Downloads", "Downloads"},
        {"intro", "Show Welcome Intro", "Tour"},
    };
    for (const auto& act : actions) {
        const QString title = QString::fromUtf8(act.title);
        if (!needle.isEmpty() && !title.toLower().contains(needle)) continue;
        entries_.push_back({
            Entry::Kind::Action,
            title,
            {},
            QString::fromUtf8(act.shortcut),
            QString::fromUtf8(act.id),
            {},
        });
    }

    if (entries_.size() > 8) {
        entries_.resize(8);
    }
    renderRows();
}

void CommandBarOverlay::renderRows() {
    while (QLayoutItem* item = rows_layout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (int i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        auto* row = new CommandRow(e.label, e.subtitle, e.badge, i == selected_index_, panel_);
        connect(row, &QAbstractButton::clicked, this, [this, i]() {
            selected_index_ = i;
            executeSelected();
        });
        rows_layout_->addWidget(row);
    }
}

void CommandBarOverlay::executeSelected() {
    if (entries_.isEmpty() || selected_index_ < 0 || selected_index_ >= entries_.size()) {
        dismiss();
        return;
    }

    const Entry chosen = entries_[selected_index_];
    dismiss();

    switch (chosen.kind) {
        case Entry::Kind::Navigate:
            if (mode_ == Mode::EditCurrentUrl) {
                emit navigateInCurrentTab(chosen.url);
            } else {
                emit openInNewTab(chosen.url);
            }
            break;
        case Entry::Kind::SwitchTab:
            emit switchToTab(chosen.target_id);
            break;
        case Entry::Kind::SwitchSpace:
            emit switchToSpace(chosen.target_id);
            break;
        case Entry::Kind::Action:
            emit actionRequested(chosen.target_id);
            break;
    }
}

}  // namespace flourish

#include "command_bar.moc"
