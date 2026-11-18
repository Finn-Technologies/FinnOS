#include "sidebar_panel.hpp"

#include <QAbstractButton>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace flourish {

namespace {

class UrlPillButton final : public QAbstractButton {
    Q_OBJECT

public:
    explicit UrlPillButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(36);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(QStringLiteral("Address bar"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor bg = hovered_ ? t.surface_hover : t.surface_input;
        p.setBrush(bg);
        p.setPen(QPen(hovered_ ? t.accent : t.hair, 1.0));
        p.drawRoundedRect(r, finn::ui::kRadiusLg, finn::ui::kRadiusLg);
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
    bool hovered_ = false;
};

class FavoriteTile final : public QAbstractButton {
    Q_OBJECT

public:
    FavoriteTile(const BrowserTab& tab, bool active, const QColor& accent,
                 QWidget* parent = nullptr)
        : QAbstractButton(parent),
          tab_(tab),
          active_(active),
          accent_(accent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(48);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(tab.title.isEmpty() ? display_host_for_url(tab.url) : tab.title);
        setToolTip(tab.title + QStringLiteral("\n") + tab.url.toString());
        hover_anim_ = new QVariantAnimation(this);
        connect(hover_anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            hover_progress_ = v.toReal();
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = active_ ? t.nav_sel : t.tile;
        if (!active_ && hover_progress_ > 0.0) {
            QColor hv = t.surface_hover;
            hv.setAlpha(static_cast<int>(hv.alpha() * hover_progress_));
            bg = hv;
        }
        p.setBrush(bg);
        p.setPen(QPen(active_ ? accent_ : t.hair, active_ ? 1.4 : 1.0));
        p.drawRoundedRect(r, finn::ui::kRadiusLg, finn::ui::kRadiusLg);

        const QRect icon_rect((width() - 16) / 2, 8, 16, 16);
        if (!tab_.favicon.isNull()) {
            tab_.favicon.paint(&p, icon_rect, Qt::AlignCenter);
        } else {
            const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
                QStringLiteral("internet"), QSize(16, 16),
                active_ ? accent_ : t.dim, devicePixelRatioF());
            p.drawPixmap(icon_rect.topLeft(), pm);
        }

        p.setFont(finn::ui::finn_font(9.0, QFont::Medium));
        p.setPen(active_ ? t.ink : t.dim);
        const QString elided = p.fontMetrics().elidedText(
            tab_.title.isEmpty() ? display_host_for_url(tab_.url) : tab_.title,
            Qt::ElideRight, width() - 6);
        p.drawText(QRect(3, 28, width() - 6, 16), Qt::AlignCenter, elided);
    }

    void enterEvent(QEnterEvent* event) override {
        hover_anim_->stop();
        hover_anim_->setDuration(110);
        hover_anim_->setStartValue(hover_progress_);
        hover_anim_->setEndValue(1.0);
        hover_anim_->setEasingCurve(QEasingCurve::OutCubic);
        hover_anim_->start();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_anim_->stop();
        hover_anim_->setDuration(200);
        hover_anim_->setStartValue(hover_progress_);
        hover_anim_->setEndValue(0.0);
        hover_anim_->setEasingCurve(QEasingCurve::OutCubic);
        hover_anim_->start();
        QAbstractButton::leaveEvent(event);
    }

private:
    BrowserTab tab_;
    bool active_ = false;
    QColor accent_;
    qreal hover_progress_ = 0.0;
    QVariantAnimation* hover_anim_ = nullptr;
};

class TabRowWidget final : public QWidget {
    Q_OBJECT

public:
    TabRowWidget(const BrowserTab& tab, bool active, bool in_split,
                 const QColor& accent, QWidget* parent = nullptr)
        : QWidget(parent),
          tab_(tab),
          active_(active),
          in_split_(in_split),
          accent_(accent) {
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(tab.title.isEmpty() ? display_host_for_url(tab.url) : tab.title);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(8);

        if (tab_.loading) {
            auto* ring = new finn::ui::FinnProgressRing(14, this);
            ring->setSpinning(true);
            lay->addWidget(ring, 0, Qt::AlignVCenter);
        } else {
            auto* ico = new QLabel(this);
            ico->setFixedSize(16, 16);
            ico->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            if (!tab_.favicon.isNull()) {
                ico->setPixmap(tab_.favicon.pixmap(16, 16));
            } else {
                const auto& t = finn::ui::FinnThemeManager::instance().tokens();
                ico->setPixmap(finn::ui::FinnThemeManager::instance().iconPixmap(
                    QStringLiteral("internet"), QSize(14, 14),
                    active_ ? accent_ : t.dim, devicePixelRatioF()));
            }
            lay->addWidget(ico, 0, Qt::AlignVCenter);
        }

        const QString display_title =
            tab_.title.isEmpty() ? display_host_for_url(tab_.url) : tab_.title;
        auto* title_lbl = new finn::ui::FinnLabel(
            display_title, finn::ui::FinnLabel::Role::Body,
            active_ ? finn::ui::FinnLabel::Tone::Ink
                    : finn::ui::FinnLabel::Tone::Dim,
            this);
        title_lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        title_lbl->setMinimumWidth(0);
        lay->addWidget(title_lbl, 1);

        if (in_split_) {
            auto* split_badge = new finn::ui::FinnBadge(
                QStringLiteral("Split"), finn::ui::FinnBadge::Tone::Accent, this);
            split_badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            lay->addWidget(split_badge, 0, Qt::AlignVCenter);
        }

        close_btn_ = new finn::ui::FinnIconButton(QStringLiteral("x"), 22, 11, this);
        close_btn_->setVisible(active_);
        close_btn_->setToolTip(QStringLiteral("Close Tab"));
        connect(close_btn_, &QAbstractButton::clicked, this, [this]() {
            emit closeClicked(tab_.id);
        });
        lay->addWidget(close_btn_, 0, Qt::AlignVCenter);
    }

signals:
    void activated(const QString& tab_id);
    void closeClicked(const QString& tab_id);
    void contextActionRequested(const QPoint& global_pos, const QString& tab_id);

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (active_) {
            p.setBrush(t.nav_sel);
            p.setPen(QPen(t.hair, 1.0));
            p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);

            p.setBrush(accent_.isValid() ? accent_ : t.accent);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(2.5, 7.0, 3.0, height() - 14.0), 1.5, 1.5);
        } else if (hovered_) {
            p.setBrush(t.surface_hover);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
        }
    }

    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        if (close_btn_) close_btn_->setVisible(true);
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        if (close_btn_) close_btn_->setVisible(active_);
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit activated(tab_.id);
            return;
        }
        if (event->button() == Qt::MiddleButton) {
            emit closeClicked(tab_.id);
            return;
        }
        if (event->button() == Qt::RightButton) {
            emit contextActionRequested(event->globalPosition().toPoint(), tab_.id);
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    BrowserTab tab_;
    bool active_ = false;
    bool in_split_ = false;
    QColor accent_;
    bool hovered_ = false;
    finn::ui::FinnIconButton* close_btn_ = nullptr;
};

class SpaceChip final : public QAbstractButton {
    Q_OBJECT

public:
    SpaceChip(const BrowserSpace& space, bool active, QWidget* parent = nullptr)
        : QAbstractButton(parent), space_(space), active_(active) {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(32, 28);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(space.name);
        setAccessibleName(space.name);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (active_) {
            p.setBrush(t.nav_sel);
            p.setPen(QPen(space_.accent, 1.2));
            p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
        } else if (hovered_) {
            p.setBrush(t.surface_hover);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);
        }

        const QString icon_name =
            space_.icon_name.isEmpty() ? QStringLiteral("home-09") : space_.icon_name;
        const QColor tint = active_ ? space_.accent : t.dim;
        const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            icon_name, QSize(16, 16), tint, devicePixelRatioF());
        p.drawPixmap((width() - 16) / 2, (height() - 16) / 2, pm);
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
    bool active_ = false;
    bool hovered_ = false;
};

void clear_layout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

}  // namespace

SidebarPanel::SidebarPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(256);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // Top Arc-style toolbar: sidebar toggle, back, forward, reload, downloads
    auto* top_row = new QHBoxLayout();
    top_row->setSpacing(4);
    collapse_btn_ = new finn::ui::FinnIconButton(QStringLiteral("sidebar-toggle"), 28, 15, this);
    collapse_btn_->setToolTip(QStringLiteral("Toggle Sidebar (Ctrl+S)"));
    collapse_btn_->setAccessibleName(QStringLiteral("Toggle Sidebar"));
    back_btn_ = new finn::ui::FinnIconButton(QStringLiteral("backward"), 28, 15, this);
    back_btn_->setToolTip(QStringLiteral("Back (Alt+Left)"));
    back_btn_->setAccessibleName(QStringLiteral("Back"));
    forward_btn_ = new finn::ui::FinnIconButton(QStringLiteral("forward"), 28, 15, this);
    forward_btn_->setToolTip(QStringLiteral("Forward (Alt+Right)"));
    forward_btn_->setAccessibleName(QStringLiteral("Forward"));
    reload_btn_ = new finn::ui::FinnIconButton(QStringLiteral("refresh"), 28, 15, this);
    reload_btn_->setToolTip(QStringLiteral("Reload (Ctrl+R)"));
    reload_btn_->setAccessibleName(QStringLiteral("Reload or Stop"));
    downloads_btn_ = new finn::ui::FinnIconButton(QStringLiteral("download"), 28, 15, this);
    downloads_btn_->setToolTip(QStringLiteral("Downloads"));
    downloads_btn_->setAccessibleName(QStringLiteral("Downloads"));

    top_row->addWidget(collapse_btn_);
    top_row->addWidget(back_btn_);
    top_row->addWidget(forward_btn_);
    top_row->addWidget(reload_btn_);
    top_row->addStretch(1);
    top_row->addWidget(downloads_btn_);
    root->addLayout(top_row);

    connect(collapse_btn_, &QAbstractButton::clicked, this, &SidebarPanel::collapseRequested);
    connect(back_btn_, &QAbstractButton::clicked, this, &SidebarPanel::backRequested);
    connect(forward_btn_, &QAbstractButton::clicked, this, &SidebarPanel::forwardRequested);
    connect(reload_btn_, &QAbstractButton::clicked, this, &SidebarPanel::reloadOrStopRequested);
    connect(downloads_btn_, &QAbstractButton::clicked, this, &SidebarPanel::downloadsRequested);

    // Arc-style URL pill
    auto* pill_btn = new UrlPillButton(this);
    url_pill_ = pill_btn;
    auto* pill_lay = new QHBoxLayout(pill_btn);
    pill_lay->setContentsMargins(10, 0, 10, 0);
    pill_lay->setSpacing(7);

    url_sec_icon_ = new QLabel(pill_btn);
    url_sec_icon_->setFixedSize(14, 14);
    url_sec_icon_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    url_label_ = new finn::ui::FinnLabel(
        QStringLiteral("Search or enter address"),
        finn::ui::FinnLabel::Role::Body, finn::ui::FinnLabel::Tone::Dim, pill_btn);
    url_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto* kbd_hint = new finn::ui::FinnLabel(
        QStringLiteral("Ctrl+L"),
        finn::ui::FinnLabel::Role::Caption, finn::ui::FinnLabel::Tone::Mute, pill_btn);
    kbd_hint->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    pill_lay->addWidget(url_sec_icon_, 0, Qt::AlignVCenter);
    pill_lay->addWidget(url_label_, 1);
    pill_lay->addWidget(kbd_hint, 0, Qt::AlignVCenter);
    root->addWidget(pill_btn);
    connect(pill_btn, &QAbstractButton::clicked, this, &SidebarPanel::commandBarRequested);

    // Favorites grid
    favorites_container_ = new QWidget(this);
    favorites_grid_ = new QGridLayout(favorites_container_);
    favorites_grid_->setContentsMargins(0, 0, 0, 0);
    favorites_grid_->setSpacing(6);
    root->addWidget(favorites_container_);

    // Scrollable Pinned + Today tabs — keep viewport transparent so sidebar surface stays uniform.
    scroll_ = new QScrollArea(this);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setAutoFillBackground(false);
    scroll_->viewport()->setAutoFillBackground(false);
    QPalette trans_pal = scroll_->palette();
    trans_pal.setColor(QPalette::Base, Qt::transparent);
    trans_pal.setColor(QPalette::Window, Qt::transparent);
    scroll_->setPalette(trans_pal);
    scroll_->viewport()->setPalette(trans_pal);

    scroll_body_ = new QWidget(scroll_);
    scroll_body_->setAutoFillBackground(false);
    scroll_body_->setPalette(trans_pal);
    auto* body_lay = new QVBoxLayout(scroll_body_);
    body_lay->setContentsMargins(0, 2, 0, 2);
    body_lay->setSpacing(6);

    pinned_header_ = new finn::ui::FinnLabel(
        QStringLiteral("Pinned"), finn::ui::FinnLabel::Role::Caption,
        finn::ui::FinnLabel::Tone::Mute, scroll_body_);
    body_lay->addWidget(pinned_header_);

    pinned_layout_ = new QVBoxLayout();
    pinned_layout_->setContentsMargins(0, 0, 0, 4);
    pinned_layout_->setSpacing(4);
    body_lay->addLayout(pinned_layout_);

    auto* today_hdr_row = new QHBoxLayout();
    today_hdr_row->setContentsMargins(0, 8, 0, 0);
    auto* today_header = new finn::ui::FinnLabel(
        QStringLiteral("Today"), finn::ui::FinnLabel::Role::Caption,
        finn::ui::FinnLabel::Tone::Mute, scroll_body_);
    auto* new_tab_btn = new finn::ui::FinnButton(
        QStringLiteral("+ New Tab"), finn::ui::FinnButton::Variant::Ghost, {}, scroll_body_);
    connect(new_tab_btn, &QAbstractButton::clicked, this, &SidebarPanel::newTabRequested);
    today_hdr_row->addWidget(today_header);
    today_hdr_row->addStretch(1);
    today_hdr_row->addWidget(new_tab_btn);
    body_lay->addLayout(today_hdr_row);

    today_layout_ = new QVBoxLayout();
    today_layout_->setContentsMargins(0, 0, 0, 0);
    today_layout_->setSpacing(2);
    body_lay->addLayout(today_layout_);
    body_lay->addStretch(1);

    scroll_->setWidget(scroll_body_);
    root->addWidget(scroll_, 1);

    root->addWidget(new finn::ui::FinnDivider(Qt::Horizontal, this));

    // Spaces bar at bottom
    auto* spaces_row = new QHBoxLayout();
    spaces_row->setContentsMargins(0, 2, 0, 0);
    spaces_row->setSpacing(4);
    spaces_layout_ = new QHBoxLayout();
    spaces_layout_->setContentsMargins(0, 0, 0, 0);
    spaces_layout_->setSpacing(4);
    auto* add_space_btn = new finn::ui::FinnButton(
        QStringLiteral("+"), finn::ui::FinnButton::Variant::Ghost, {}, this);
    add_space_btn->setToolTip(QStringLiteral("New Space"));
    add_space_btn->setFixedWidth(28);
    connect(add_space_btn, &QAbstractButton::clicked, this, &SidebarPanel::newSpaceRequested);

    spaces_row->addLayout(spaces_layout_, 1);
    spaces_row->addWidget(add_space_btn);
    root->addLayout(spaces_row);
}

void SidebarPanel::setNavigationState(const QUrl& current_url,
                                      bool can_back,
                                      bool can_forward,
                                      bool loading,
                                      bool has_active_downloads) {
    back_btn_->setEnabled(can_back);
    forward_btn_->setEnabled(can_forward);
    reload_btn_->setIconName(loading ? QStringLiteral("x") : QStringLiteral("refresh"));
    reload_btn_->setToolTip(loading ? QStringLiteral("Stop Loading")
                                    : QStringLiteral("Reload (Ctrl+R)"));
    downloads_btn_->setBadgeDot(has_active_downloads);

    const QString host = display_host_for_url(current_url);
    url_label_->setText(host);
    const bool is_home = !current_url.isValid() ||
                         current_url.scheme() == QStringLiteral("flourish");
    url_label_->setTone(is_home ? finn::ui::FinnLabel::Tone::Dim
                                : finn::ui::FinnLabel::Tone::Ink);

    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const bool is_https = current_url.scheme() == QStringLiteral("https");
    const QString sec_icon = is_https ? QStringLiteral("shield") : QStringLiteral("search-01");
    const QColor sec_tint = is_https ? t.success : t.dim;
    url_sec_icon_->setPixmap(finn::ui::FinnThemeManager::instance().iconPixmap(
        sec_icon, QSize(13, 13), sec_tint, devicePixelRatioF()));
}

void SidebarPanel::setState(const QVector<BrowserSpace>& spaces,
                            const QString& active_space_id,
                            const QVector<BrowserTab>& tabs,
                            const QString& active_tab_id,
                            const QString& split_tab_id) {
    QColor space_accent = finn::ui::FinnThemeManager::instance().tokens().accent;
    for (const auto& s : spaces) {
        if (s.id == active_space_id) {
            space_accent = s.accent;
            break;
        }
    }

    QVector<BrowserTab> favorites;
    QVector<BrowserTab> space_tabs;
    for (const auto& tab : tabs) {
        if (tab.space_id != active_space_id) continue;
        if (tab.favorite) {
            favorites.push_back(tab);
        } else {
            space_tabs.push_back(tab);
        }
    }

    rebuildFavorites(favorites, active_tab_id, space_accent);
    rebuildTabLists(space_tabs, active_tab_id, split_tab_id, space_accent);
    rebuildSpaces(spaces, active_space_id);
}

void SidebarPanel::rebuildFavorites(const QVector<BrowserTab>& favorites,
                                    const QString& active_tab_id,
                                    const QColor& space_accent) {
    clear_layout(favorites_grid_);
    favorites_container_->setVisible(!favorites.isEmpty());
    for (int i = 0; i < favorites.size(); ++i) {
        const auto& tab = favorites[i];
        auto* tile = new FavoriteTile(tab, tab.id == active_tab_id, space_accent,
                                      favorites_container_);
        const QString tid = tab.id;
        connect(tile, &QAbstractButton::clicked, this, [this, tid]() {
            emit tabSelected(tid);
        });
        favorites_grid_->addWidget(tile, i / 3, i % 3);
    }
}

void SidebarPanel::rebuildTabLists(const QVector<BrowserTab>& space_tabs,
                                   const QString& active_tab_id,
                                   const QString& split_tab_id,
                                   const QColor& space_accent) {
    clear_layout(pinned_layout_);
    clear_layout(today_layout_);

    bool has_pinned = false;
    for (const auto& tab : space_tabs) {
        auto* row = new TabRowWidget(
            tab, tab.id == active_tab_id, tab.id == split_tab_id, space_accent, scroll_body_);
        connect(row, &TabRowWidget::activated, this, &SidebarPanel::tabSelected);
        connect(row, &TabRowWidget::closeClicked, this, &SidebarPanel::tabClosed);
        connect(row, &TabRowWidget::contextActionRequested, this,
                [this, tab](const QPoint& global_pos, const QString& tid) {
                    QMenu menu(this);
                    menu.addAction(tab.pinned ? QStringLiteral("Unpin Tab")
                                              : QStringLiteral("Pin Tab"),
                                   [this, tid]() { emit tabPinToggled(tid); });
                    menu.addAction(tab.favorite ? QStringLiteral("Remove from Favorites")
                                                : QStringLiteral("Add to Favorites"),
                                   [this, tid]() { emit tabFavoriteToggled(tid); });
                    menu.addAction(QStringLiteral("Open in Split View"),
                                   [this, tid]() { emit tabSplitRequested(tid); });
                    menu.addAction(QStringLiteral("Duplicate Tab"),
                                   [this, tid]() { emit tabDuplicateRequested(tid); });
                    menu.addAction(QStringLiteral("Reload"),
                                   [this, tid]() { emit tabReloadRequested(tid); });
                    menu.addSeparator();
                    menu.addAction(QStringLiteral("Close Tab"),
                                   [this, tid]() { emit tabClosed(tid); });
                    menu.exec(global_pos);
                });

        if (tab.pinned) {
            has_pinned = true;
            pinned_layout_->addWidget(row);
        } else {
            today_layout_->addWidget(row);
        }
    }
    pinned_header_->setVisible(has_pinned);
}

void SidebarPanel::rebuildSpaces(const QVector<BrowserSpace>& spaces,
                                 const QString& active_space_id) {
    clear_layout(spaces_layout_);
    for (const auto& space : spaces) {
        auto* chip = new SpaceChip(space, space.id == active_space_id, this);
        const QString sid = space.id;
        connect(chip, &QAbstractButton::clicked, this, [this, sid]() {
            emit spaceSelected(sid);
        });
        spaces_layout_->addWidget(chip);
    }
    spaces_layout_->addStretch(1);
}

}  // namespace flourish

#include "sidebar_panel.moc"
