// Implements HomeMenuPopover, SpotlightPopover, ControlCenterPopover,
// OpenWindowsPopover, and LockScreenWindow with 1:1 Figma layout and live theme reload.

#include "shell/popovers.hpp"

#include <QDateTime>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProcess>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "shell/search.hpp"
#include "system/desktop_services.hpp"
#include "system/sys_files.hpp"

namespace finn::shell {
namespace {

QColor popoverGlassFill(bool dark) {
    return dark ? QColor(0x28, 0x2C, 0x37, 0xD9) : QColor(255, 255, 255, 0xB2);
}

QColor popoverSubcardFill(bool dark) {
    return dark ? QColor(255, 255, 255, 0x1A) : QColor(255, 255, 255, 0xB2);
}

QColor popoverFooterFill(bool dark) {
    return dark ? QColor(255, 255, 255, 0x14) : QColor(255, 255, 255, 0x66);
}

QColor popoverDimInk(bool dark) {
    return dark ? QColor(255, 255, 255, 0xB2) : QColor(0, 0, 0, 0xB2);
}

QFont popoverFont(int px, QFont::Weight weight = QFont::Normal) {
    QFont font(finn::ui::primary_font_family());
    font.setPixelSize(px);
    font.setWeight(weight);
    return font;
}

void paintHoverFill(QPainter& p, const QRectF& rect, qreal radius, bool dark) {
    p.setPen(Qt::NoPen);
    p.setBrush(dark ? QColor(255, 255, 255, 28) : QColor(255, 255, 255, 96));
    p.drawRoundedRect(rect, radius, radius);
}

void paintMonochromeIcon(QPainter& p, const QRectF& rect,
                         const QString& icon_name, const QColor& tint,
                         qreal dpr) {
    const QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
        icon_name, rect.size().toSize(), tint, dpr);
    if (!pm.isNull()) {
        p.drawPixmap(rect.topLeft(), pm);
    }
}

void paintAppIconTile(QPainter& p, const QRectF& rect, qreal radius,
                      const QString& icon_name, qreal dpr) {
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    p.save();
    p.setClipPath(clip);

    const QSize target = rect.size().toSize();
    QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
        icon_name, target, QColor(), dpr);
    if (pm.isNull()) {
        const QIcon fallback = QIcon::fromTheme(icon_name);
        if (!fallback.isNull()) pm = fallback.pixmap(target);
    }
    if (pm.isNull()) {
        pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            QStringLiteral("app-grid"), target, QColor(), dpr);
    }
    if (!pm.isNull()) {
        p.drawPixmap(rect.toRect(), pm);
    }
    p.restore();
}

void paintGlassFooter(QPainter& p, qreal width, qreal footer_y, qreal footer_h,
                      qreal radius, bool dark) {
    QPainterPath outer_clip;
    outer_clip.addRoundedRect(QRectF(0.0, 0.0, width, footer_y + footer_h),
                              radius, radius);
    p.save();
    p.setClipPath(outer_clip);
    p.fillRect(QRectF(0.0, footer_y, width, footer_h), popoverFooterFill(dark));
    p.restore();
}

void configureBorderlessEdit(QLineEdit* edit, int px, bool dark) {
    if (!edit) return;
    edit->setFont(popoverFont(px));
    edit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; }"));
    QPalette pal = edit->palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, dark ? QColor(0xF2, 0xF4, 0xF8) : QColor(0, 0, 0));
    pal.setColor(QPalette::PlaceholderText, popoverDimInk(dark));
    edit->setPalette(pal);
}

std::vector<const finn::App*> rank_installed_apps(
    const std::vector<finn::App>& apps, const std::string& query,
    std::size_t limit) {
    std::vector<const finn::App*> out;
    if (query.empty()) {
        for (const auto& a : apps) {
            if (!a.installed) continue;
            out.push_back(&a);
            if (out.size() >= limit) break;
        }
        return out;
    }

    std::vector<finn::SessionApp> session_apps;
    std::unordered_map<std::string, const finn::App*> lookup;
    session_apps.reserve(apps.size());
    lookup.reserve(apps.size());
    for (const auto& a : apps) {
        if (!a.installed) continue;
        finn::SessionApp sa;
        sa.id = a.id;
        sa.name = a.name;
        sa.icon = a.icon;
        sa.installed = a.installed;
        sa.category = a.category;
        sa.note = a.note;
        session_apps.push_back(std::move(sa));
        lookup[a.id] = &a;
    }

    const auto ranked = finn::search_apps(session_apps, query);
    for (const auto& hit : ranked) {
        const auto it = lookup.find(hit.id);
        if (it != lookup.end()) {
            out.push_back(it->second);
            if (out.size() >= limit) break;
        }
    }
    return out;
}

QString source_label(finn::AppSource src) {
    switch (src) {
        case finn::AppSource::kNative:
            return QStringLiteral("Native");
        case finn::AppSource::kFlatpak:
            return QStringLiteral("Flatpak");
        case finn::AppSource::kWinget:
            return QStringLiteral("Windows");
        case finn::AppSource::kDesktop:
            return QStringLiteral("Linux");
        case finn::AppSource::kWindows:
            return QStringLiteral("Windows");
        case finn::AppSource::kUnknown:
            return QStringLiteral("App");
    }
    return QStringLiteral("Native");
}

QString current_username() {
    const char* user = std::getenv("USER");
    return (user && *user) ? QString::fromUtf8(user) : QStringLiteral("finn");
}

QString current_user_host() {
    return QStringLiteral("%1@%2").arg(current_username(),
                                       QSysInfo::machineHostName());
}

void clear_grid(QGridLayout* grid) {
    while (QLayoutItem* item = grid->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

const QStringList& menuCategories() {
    static const QStringList kCats = {
        QStringLiteral("System"),       QStringLiteral("Utilities"),
        QStringLiteral("Productivity"), QStringLiteral("Linux"),
        QStringLiteral("Windows"),      QStringLiteral("All")};
    return kCats;
}

QString shortRecommendationLabel(const finn::App& app) {
    QString label = QString::fromStdString(app.name);
    if (label.startsWith(QStringLiteral("Finn "))) {
        label = label.mid(5);
    }
    return label;
}

bool appMatchesCategory(const finn::App& a, const QString& cat) {
    if (!a.installed) return false;
    if (cat == QStringLiteral("All")) return true;
    if (cat == QStringLiteral("Linux")) {
        return a.source == finn::AppSource::kDesktop ||
               a.source == finn::AppSource::kFlatpak;
    }
    if (cat == QStringLiteral("Windows")) {
        return a.source == finn::AppSource::kWindows ||
               a.source == finn::AppSource::kWinget;
    }

    const QString c = QString::fromStdString(a.category).toLower();
    const QString id = QString::fromStdString(a.id).toLower();
    if (cat == QStringLiteral("System")) {
        return c == QStringLiteral("system") || id == QStringLiteral("settings") ||
               id == QStringLiteral("monitor") || id == QStringLiteral("store") ||
               id == QStringLiteral("installer");
    }
    if (cat == QStringLiteral("Utilities")) {
        return c == QStringLiteral("utilities") || c == QStringLiteral("utility") ||
               id == QStringLiteral("files") || id == QStringLiteral("terminal") ||
               id == QStringLiteral("viewer");
    }
    if (cat == QStringLiteral("Productivity")) {
        return c == QStringLiteral("productivity") || c == QStringLiteral("office") ||
               c == QStringLiteral("network") || id == QStringLiteral("notes") ||
               id == QStringLiteral("browser");
    }
    return false;
}

}  // namespace

// ---- HomeMenuPopover (Figma 4:24 — 585x460, r=12) ----

HomeMenuPopover::HomeMenuPopover(const std::vector<finn::App>* apps,
                                 const ShellActions& actions)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-shell-home")),
      apps_(apps),
      actions_(actions) {
    setAnchors(AnchorBottom);
    setKeyboardMode(KeyboardMode::OnDemand);
    setExclusiveZone(-1);
    setLayerMargins(QMargins(0, 0, 0, 74));
    setDesiredSize(QSize(585, 460));
    setFixedSize(585, 460);
    setMouseTracking(true);

    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this,
            [this]() { update(); });

    open_anim_ = new QVariantAnimation(this);
    open_anim_->setDuration(160);
    open_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(open_anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& val) {
                open_progress_ = val.toReal();
                update();
            });
    rebuildHits();
}

void HomeMenuPopover::openMenu() {
    if (actions_.reload_apps) actions_.reload_apps();
    active_category_.clear();
    hovered_hit_ = -1;
    rebuildHits();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->stop();
        open_progress_ = 1.0;
    } else {
        open_anim_->stop();
        open_progress_ = 0.0;
        open_anim_->setStartValue(0.0);
        open_anim_->setEndValue(1.0);
        open_anim_->start();
    }
    show();
    raise();
    activateWindow();
    setFocus();
}

void HomeMenuPopover::refreshApps() {
    rebuildHits();
    update();
}

std::vector<const finn::App*> HomeMenuPopover::favoriteApps() const {
    std::vector<const finn::App*> out;
    if (!apps_) return out;

    const QStringList pins = finn::ui::FinnThemeManager::instance().dockPins();
    auto contains_id = [&](const std::string& id) {
        return std::any_of(out.begin(), out.end(),
                           [&](const finn::App* a) { return a->id == id; });
    };

    for (const QString& p : pins) {
        const std::string pid = p.toStdString();
        for (const auto& a : *apps_) {
            if (a.installed && a.id == pid && !contains_id(a.id)) {
                out.push_back(&a);
                break;
            }
        }
        if (out.size() >= 10) return out;
    }

    for (const auto& a : *apps_) {
        if (a.installed && !contains_id(a.id)) {
            out.push_back(&a);
            if (out.size() >= 10) break;
        }
    }
    return out;
}

std::vector<const finn::App*> HomeMenuPopover::categoryApps(
    const QString& category) const {
    std::vector<const finn::App*> out;
    if (!apps_) return out;
    for (const auto& a : *apps_) {
        if (appMatchesCategory(a, category)) out.push_back(&a);
    }
    return out;
}

std::vector<const finn::App*> HomeMenuPopover::recentApps() const {
    std::vector<const finn::App*> out;
    if (!apps_) return out;
    const auto ids = finn::sys::load_recent_apps();
    for (const auto& id : ids) {
        for (const auto& a : *apps_) {
            if (a.installed && a.id == id) {
                out.push_back(&a);
                break;
            }
        }
        if (out.size() >= 8) break;
    }
    return out;
}

void HomeMenuPopover::rebuildHits() {
    hits_.clear();
    hovered_hit_ = -1;

    const auto favs = favoriteApps();
    for (std::size_t i = 0; i < favs.size() && i < 10; ++i) {
        const int col = static_cast<int>(i / 5);
        const int row = static_cast<int>(i % 5);
        const QRect r(15 + col * 140, 46 + row * 42, 130, 32);
        hits_.push_back({r, HitItem::Kind::LaunchApp,
                         QString::fromStdString(favs[i]->id),
                         QString::fromStdString(favs[i]->name)});
    }

    if (active_category_.isEmpty()) {
        const auto& cats = menuCategories();
        for (int i = 0; i < cats.size(); ++i) {
            const int col = i % 3;
            const int row = i / 3;
            const QRect r(310 + col * 90, 46 + row * 106, 80, 96);
            hits_.push_back({r, HitItem::Kind::OpenCategory, cats[i], cats[i]});
        }
    } else {
        hits_.push_back({QRect(310, 14, 240, 24), HitItem::Kind::BackToAll,
                         {}, QStringLiteral("Back to All Apps")});
        const auto cat_apps = categoryApps(active_category_);
        for (std::size_t i = 0; i < cat_apps.size() && i < 10; ++i) {
            const int col = static_cast<int>(i / 5);
            const int row = static_cast<int>(i % 5);
            const QRect r(310 + col * 135, 46 + row * 42, 128, 32);
            hits_.push_back({r, HitItem::Kind::LaunchApp,
                             QString::fromStdString(cat_apps[i]->id),
                             QString::fromStdString(cat_apps[i]->name)});
        }
    }

    const auto recents = recentApps();
    for (std::size_t i = 0; i < recents.size() && i < 8; ++i) {
        const int col = static_cast<int>(i % 4);
        const int row = static_cast<int>(i / 4);
        const int x = 15 + (col == 3 ? 424 : col * 141);
        const int y = 297 + row * 42;
        hits_.push_back({QRect(x, y, 131, 32), HitItem::Kind::LaunchApp,
                         QString::fromStdString(recents[i]->id),
                         QString::fromStdString(recents[i]->name)});
    }

    hits_.push_back({QRect(8, 415, 240, 40), HitItem::Kind::Lock, {},
                     QStringLiteral("Lock Screen")});
    hits_.push_back({QRect(505, 418, 34, 34), HitItem::Kind::Reboot, {},
                     QStringLiteral("Restart")});
    hits_.push_back({QRect(542, 418, 34, 34), HitItem::Kind::PowerOff, {},
                     QStringLiteral("Shut Down")});
}

void HomeMenuPopover::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pt = event->position().toPoint();
    int next = -1;
    for (int i = 0; i < hits_.size(); ++i) {
        if (hits_[i].rect.contains(pt)) {
            next = i;
            break;
        }
    }
    if (next != hovered_hit_) {
        hovered_hit_ = next;
        setCursor(hovered_hit_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(hovered_hit_ >= 0 ? hits_[hovered_hit_].tooltip : QString());
        update();
    }
    FinnLayerWindow::mouseMoveEvent(event);
}

void HomeMenuPopover::leaveEvent(QEvent* event) {
    if (hovered_hit_ != -1) {
        hovered_hit_ = -1;
        setCursor(Qt::ArrowCursor);
        setToolTip({});
        update();
    }
    FinnLayerWindow::leaveEvent(event);
}

void HomeMenuPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pt = event->position().toPoint();
        for (const auto& hit : hits_) {
            if (!hit.rect.contains(pt)) continue;
            switch (hit.kind) {
                case HitItem::Kind::LaunchApp: {
                    const std::string id = hit.key.toStdString();
                    hide();
                    emit closed();
                    if (actions_.launch_app) actions_.launch_app(id);
                    return;
                }
                case HitItem::Kind::OpenCategory:
                    active_category_ = hit.key;
                    rebuildHits();
                    update();
                    return;
                case HitItem::Kind::BackToAll:
                    active_category_.clear();
                    rebuildHits();
                    update();
                    return;
                case HitItem::Kind::Lock:
                    hide();
                    emit closed();
                    if (actions_.lock_screen) actions_.lock_screen();
                    return;
                case HitItem::Kind::Reboot:
                    hide();
                    emit closed();
                    finn::sys::FinnDesktopServices::instance().triggerPowerAction(
                        QStringLiteral("reboot"));
                    return;
                case HitItem::Kind::PowerOff:
                    hide();
                    emit closed();
                    finn::sys::FinnDesktopServices::instance().triggerPowerAction(
                        QStringLiteral("poweroff"));
                    return;
            }
        }
    }
    FinnLayerWindow::mousePressEvent(event);
}

void HomeMenuPopover::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    p.setPen(Qt::NoPen);
    p.setBrush(popoverGlassFill(t.dark));
    p.drawRoundedRect(QRectF(0.0, 0.0, 585.0, 460.0), 12.0, 12.0);

    const QColor dim = popoverDimInk(t.dark);
    const QColor ink = t.dark ? QColor(0xF2, 0xF4, 0xF8) : QColor(0, 0, 0);
    const QColor placeholder =
        t.dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 26);

    // Top-left: Your favorite apps
    p.setFont(popoverFont(14));
    p.setPen(dim);
    p.drawText(QRectF(15.0, 14.0, 260.0, 24.0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Your favorite apps"));

    const auto favs = favoriteApps();
    const QFontMetricsF fm13_list(popoverFont(13));
    for (std::size_t i = 0; i < favs.size() && i < 10; ++i) {
        const int col = static_cast<int>(i / 5);
        const int row = static_cast<int>(i % 5);
        const QRectF item_r(15.0 + col * 140.0, 46.0 + row * 42.0, 130.0, 32.0);
        const bool is_hovered =
            hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
        if (is_hovered) {
            paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0), 8.0, t.dark);
        }
        paintAppIconTile(p, QRectF(item_r.x(), item_r.y(), 32.0, 32.0), 8.0,
                         QString::fromStdString(favs[i]->icon), dpr);
        p.setFont(popoverFont(13));
        p.setPen(ink);
        const QString name =
            fm13_list.elidedText(QString::fromStdString(favs[i]->name), Qt::ElideRight, 94.0);
        p.drawText(QRectF(item_r.x() + 40.0, item_r.y(), 94.0, 32.0),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
    }

    // Center 1px vertical divider
    p.fillRect(QRectF(293.0, 96.0, 1.0, 100.0),
               t.dark ? QColor(255, 255, 255, 0x33) : QColor(0, 0, 0, 0x33));

    // Top-right: All Apps or expanded Category
    if (active_category_.isEmpty()) {
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(310.0, 14.0, 260.0, 24.0),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("All Apps"));

        const auto& cats = menuCategories();
        for (int idx = 0; idx < cats.size(); ++idx) {
            const int col = idx % 3;
            const int row = idx / 3;
            const qreal fx = 310.0 + col * 90.0;
            const qreal fy = 46.0 + row * 106.0;
            const QRect hit_rect(static_cast<int>(fx), static_cast<int>(fy), 80, 96);
            const bool is_hovered =
                hovered_hit_ >= 0 && hits_[hovered_hit_].rect == hit_rect;

            p.setPen(Qt::NoPen);
            QColor box_fill = t.dark ? QColor(255, 255, 255, is_hovered ? 42 : 26)
                                     : QColor(255, 255, 255, is_hovered ? 148 : 102);
            p.setBrush(box_fill);
            p.drawRoundedRect(QRectF(fx, fy, 80.0, 80.0), 12.0, 12.0);

            const auto cat_apps = categoryApps(cats[idx]);
            const QRectF big_slots[3] = {
                QRectF(fx + 7.0, fy + 7.0, 32.0, 32.0),
                QRectF(fx + 41.0, fy + 7.0, 32.0, 32.0),
                QRectF(fx + 7.0, fy + 41.0, 32.0, 32.0),
            };
            const QRectF mini_slots[4] = {
                QRectF(fx + 41.0, fy + 41.0, 14.0, 14.0),
                QRectF(fx + 59.0, fy + 41.0, 14.0, 14.0),
                QRectF(fx + 41.0, fy + 59.0, 14.0, 14.0),
                QRectF(fx + 59.0, fy + 59.0, 14.0, 14.0),
            };

            for (int s = 0; s < 3; ++s) {
                if (s < static_cast<int>(cat_apps.size())) {
                    paintAppIconTile(p, big_slots[s], 7.0,
                                     QString::fromStdString(cat_apps[s]->icon), dpr);
                } else {
                    p.setPen(Qt::NoPen);
                    p.setBrush(placeholder);
                    p.drawRoundedRect(big_slots[s], 7.0, 7.0);
                }
            }
            for (int m = 0; m < 4; ++m) {
                const int app_idx = 3 + m;
                if (app_idx < static_cast<int>(cat_apps.size())) {
                    paintAppIconTile(p, mini_slots[m], 3.0,
                                     QString::fromStdString(cat_apps[app_idx]->icon),
                                     dpr);
                } else {
                    p.setPen(Qt::NoPen);
                    p.setBrush(placeholder);
                    p.drawRoundedRect(mini_slots[m], 3.0, 3.0);
                }
            }

            p.setFont(popoverFont(11));
            p.setPen(ink);
            p.drawText(QRectF(fx - 4.0, fy + 82.0, 88.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop, cats[idx]);
        }
    } else {
        const bool back_hovered =
            hovered_hit_ >= 0 &&
            hits_[hovered_hit_].kind == HitItem::Kind::BackToAll;
        if (back_hovered) {
            paintHoverFill(p, QRectF(306.0, 14.0, 180.0, 24.0), 6.0, t.dark);
        }
        paintMonochromeIcon(p, QRectF(310.0, 19.0, 14.0, 14.0),
                            QStringLiteral("chevron-left"), dim, dpr);
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(328.0, 14.0, 240.0, 24.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("All Apps  /  %1").arg(active_category_));

        const auto cat_apps = categoryApps(active_category_);
        if (cat_apps.empty()) {
            p.setFont(popoverFont(13));
            p.setPen(dim);
            p.drawText(QRectF(310.0, 46.0, 260.0, 190.0),
                       Qt::AlignVCenter | Qt::AlignHCenter,
                       QStringLiteral("No %1 apps installed").arg(active_category_));
        } else {
            for (std::size_t i = 0; i < cat_apps.size() && i < 10; ++i) {
                const int col = static_cast<int>(i / 5);
                const int row = static_cast<int>(i % 5);
                const QRectF item_r(310.0 + col * 135.0, 46.0 + row * 42.0,
                                    128.0, 32.0);
                const bool is_hovered =
                    hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
                if (is_hovered) {
                    paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0), 8.0,
                                   t.dark);
                }
                paintAppIconTile(p, QRectF(item_r.x(), item_r.y(), 32.0, 32.0),
                                 8.0, QString::fromStdString(cat_apps[i]->icon),
                                 dpr);
                p.setFont(popoverFont(13));
                p.setPen(ink);
                const QString name = fm13_list.elidedText(
                    QString::fromStdString(cat_apps[i]->name), Qt::ElideRight, 90.0);
                p.drawText(QRectF(item_r.x() + 40.0, item_r.y(), 90.0, 32.0),
                           Qt::AlignVCenter | Qt::AlignLeft, name);
            }
        }
    }

    // Middle: Recent activity
    p.setFont(popoverFont(14));
    p.setPen(dim);
    p.drawText(QRectF(15.0, 267.0, 555.0, 22.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Recent activity"));

    const auto recents = recentApps();
    if (recents.empty()) {
        p.setFont(popoverFont(13));
        p.setPen(dim);
        p.drawText(QRectF(15.0, 297.0, 555.0, 74.0),
                   Qt::AlignTop | Qt::AlignLeft,
                   QStringLiteral("No recent activity yet"));
    } else {
        const QFontMetricsF fm13(popoverFont(13));
        for (std::size_t i = 0; i < recents.size() && i < 8; ++i) {
            const int col = static_cast<int>(i % 4);
            const int row = static_cast<int>(i / 4);
            const qreal rx = 15.0 + (col == 3 ? 424.0 : col * 141.0);
            const qreal ry = 297.0 + row * 42.0;
            const QRectF item_r(rx, ry, 131.0, 32.0);
            const bool is_hovered =
                hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
            if (is_hovered) {
                paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0), 8.0,
                               t.dark);
            }
            paintAppIconTile(p, QRectF(rx, ry, 32.0, 32.0), 8.0,
                             QString::fromStdString(recents[i]->icon), dpr);
            p.setFont(popoverFont(13));
            p.setPen(ink);
            const QString name = fm13.elidedText(
                QString::fromStdString(recents[i]->name), Qt::ElideRight, 91.0);
            p.drawText(QRectF(rx + 40.0, ry + 1.0, 91.0, 16.0),
                       Qt::AlignVCenter | Qt::AlignLeft, name);
            p.setFont(popoverFont(11));
            p.setPen(dim);
            p.drawText(QRectF(rx + 40.0, ry + 17.0, 91.0, 14.0),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("Recently opened"));
        }
    }

    // Bottom bar (0, 410, 585, 50)
    paintGlassFooter(p, 585.0, 410.0, 50.0, 12.0, t.dark);

    const bool lock_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::Lock;
    if (lock_hover) {
        paintHoverFill(p, QRectF(8.0, 415.0, 240.0, 40.0), 8.0, t.dark);
    }
    const QPixmap avatar = finn::ui::FinnThemeManager::instance().iconPixmap(
        QStringLiteral("avatar"), QSize(30, 30), QColor(), dpr);
    if (!avatar.isNull()) {
        p.drawPixmap(QRect(15, 420, 30, 30), avatar);
    }
    p.setFont(popoverFont(16));
    p.setPen(ink);
    p.drawText(QRectF(55.0, 410.0, 240.0, 50.0),
               Qt::AlignVCenter | Qt::AlignLeft, current_username());

    const bool reboot_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::Reboot;
    if (reboot_hover) {
        paintHoverFill(p, QRectF(505.0, 418.0, 34.0, 34.0), 8.0, t.dark);
    }
    paintMonochromeIcon(p, QRectF(511.0, 424.0, 22.0, 22.0),
                        QStringLiteral("refresh"), ink, dpr);

    const bool power_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::PowerOff;
    if (power_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xEF, 0x44, 0x44));
        p.drawRoundedRect(QRectF(542.0, 418.0, 34.0, 34.0), 8.0, 8.0);
    }
    paintMonochromeIcon(p, QRectF(548.0, 424.0, 22.0, 22.0),
                        QStringLiteral("power"), power_hover ? Qt::white : ink,
                        dpr);
}

void HomeMenuPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    FinnLayerWindow::keyPressEvent(event);
}

bool HomeMenuPopover::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
        emit closed();
    }
    return FinnLayerWindow::event(event);
}

// ---- SpotlightPopover (Figma 4:226 — 350x287 transparent shell) ----

SpotlightPopover::SpotlightPopover(const std::vector<finn::App>* apps,
                                   const ShellActions& actions)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-shell-search")),
      apps_(apps),
      actions_(actions) {
    setAnchors(AnchorBottom);
    setKeyboardMode(KeyboardMode::Exclusive);
    setExclusiveZone(-1);
    setLayerMargins(QMargins(0, 0, 0, 74));
    setDesiredSize(QSize(350, 287));
    setFixedSize(350, 287);
    setMouseTracking(true);

    query_edit_ = new QLineEdit(this);
    query_edit_->setFrame(false);
    query_edit_->setAttribute(Qt::WA_TranslucentBackground);
    query_edit_->setPlaceholderText(QStringLiteral("Search or ask anything"));
    query_edit_->setGeometry(71, 0, 240, 44);
    query_edit_->installEventFilter(this);
    applyQueryEditTheme();

    connect(query_edit_, &QLineEdit::textChanged, this,
            &SpotlightPopover::updateResults);
    connect(query_edit_, &QLineEdit::returnPressed, this,
            &SpotlightPopover::activateSelected);
    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this, [this]() {
                applyQueryEditTheme();
                updateResults(query_edit_->text());
            });

    open_anim_ = new QVariantAnimation(this);
    open_anim_->setDuration(160);
    open_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(open_anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& val) {
                open_progress_ = val.toReal();
                if (query_edit_) {
                    const int dy =
                        static_cast<int>(std::lround((1.0 - open_progress_) * 10.0));
                    query_edit_->move(71, dy);
                }
                update();
            });

    updateResults({});
    updatePopoverInputRegions();
}

void SpotlightPopover::applyQueryEditTheme() {
    configureBorderlessEdit(
        query_edit_, 16, finn::ui::FinnThemeManager::instance().isDark());
}

void SpotlightPopover::updatePopoverInputRegions() {
    setInputRegions({QRect(25, 0, 300, 44), QRect(0, 59, 350, 228)});
}

void SpotlightPopover::openSearch() {
    if (actions_.reload_apps) actions_.reload_apps();
    hovered_empty_app_ = -1;
    hovered_empty_query_ = -1;
    hovered_hit_ = -1;
    query_edit_->clear();
    updateResults({});
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->stop();
        open_progress_ = 1.0;
        if (query_edit_) query_edit_->move(71, 0);
    } else {
        open_anim_->stop();
        open_progress_ = 0.0;
        if (query_edit_) query_edit_->move(71, 10);
        open_anim_->setStartValue(0.0);
        open_anim_->setEndValue(1.0);
        open_anim_->start();
    }
    show();
    raise();
    activateWindow();
    query_edit_->setFocus();
    QTimer::singleShot(0, this, &SpotlightPopover::updatePopoverInputRegions);
}

void SpotlightPopover::updateResults(const QString& query) {
    empty_app_hits_.clear();
    empty_query_hits_.clear();
    hits_.clear();
    selected_idx_ = 0;
    hovered_empty_app_ = -1;
    hovered_empty_query_ = -1;
    hovered_hit_ = -1;

    const QString q = query.trimmed();

    if (q.isEmpty()) {
        std::vector<const finn::App*> recs;
        if (apps_) {
            const QStringList pins = finn::ui::FinnThemeManager::instance().dockPins();
            auto has_id = [&](const std::string& id) {
                return std::any_of(recs.begin(), recs.end(),
                                   [&](const finn::App* a) { return a->id == id; });
            };
            for (const QString& p : pins) {
                for (const auto& a : *apps_) {
                    if (a.installed && a.id == p.toStdString() && !has_id(a.id)) {
                        recs.push_back(&a);
                        break;
                    }
                }
                if (recs.size() >= 5) break;
            }
            for (const auto& a : *apps_) {
                if (a.installed && !has_id(a.id)) {
                    recs.push_back(&a);
                    if (recs.size() >= 5) break;
                }
            }
        }

        constexpr int col_x[5] = {15, 87, 159, 231, 303};
        for (int i = 0; i < static_cast<int>(recs.size()) && i < 5; ++i) {
            const std::string id = recs[i]->id;
            empty_app_hits_.push_back(SearchHit{
                QRect(col_x[i] - 10, 96, 52, 62),
                shortRecommendationLabel(*recs[i]),
                {},
                QString::fromStdString(recs[i]->icon),
                {},
                true,
                [this, id]() {
                    if (actions_.launch_app) actions_.launch_app(id);
                },
            });
        }

        const bool is_dark = finn::ui::FinnThemeManager::instance().isDark();
        const QPoint offsets[4] = {{0, 0}, {165, 0}, {0, 38}, {165, 38}};
        struct QuickPrompt {
            QString title;
            QString icon;
            std::function<void()> fn;
        };
        const QVector<QuickPrompt> prompts = {
            {is_dark ? QStringLiteral("Light Mode") : QStringLiteral("Dark Mode"),
             is_dark ? QStringLiteral("sun-01") : QStringLiteral("moon"),
             [is_dark]() {
                 finn::ui::FinnThemeManager::instance().setDark(!is_dark);
             }},
            {QStringLiteral("Settings"), QStringLiteral("settings-02"),
             [this]() {
                 if (actions_.launch_app) actions_.launch_app("settings");
             }},
            {QStringLiteral("Finn Store"), QStringLiteral("store"),
             [this]() {
                 if (actions_.launch_app) actions_.launch_app("store");
             }},
            {QStringLiteral("Open Files"), QStringLiteral("folder"),
             [this]() {
                 if (actions_.launch_app) actions_.launch_app("files");
             }},
        };

        for (int i = 0; i < prompts.size(); ++i) {
            const QRect r(15 + offsets[i].x(), 59 + 141 + offsets[i].y(), 155, 28);
            empty_query_hits_.push_back(SearchHit{
                r, prompts[i].title, {}, prompts[i].icon, {}, false, prompts[i].fn});
        }
        update();
        return;
    }

    // 1. Inline arithmetic calculator
    const std::string ans = finn::sys::evaluate_math_expression(q.toStdString());
    if (!ans.empty()) {
        hits_.push_back(SearchHit{
            {},
            QStringLiteral("= %1").arg(QString::fromStdString(ans)),
            QStringLiteral("Calculated from %1").arg(q),
            QStringLiteral("info"),
            QStringLiteral("Math"),
            false,
            {},
        });
    }

    // 2. Application search (shared finn::search_apps)
    if (apps_) {
        const auto ranked = rank_installed_apps(*apps_, q.toStdString(), 6);
        for (const finn::App* a : ranked) {
            const std::string id = a->id;
            hits_.push_back(SearchHit{
                {},
                QString::fromStdString(a->name),
                QString::fromStdString(a->note),
                QString::fromStdString(a->icon),
                source_label(a->source),
                true,
                [this, id]() {
                    if (actions_.launch_app) actions_.launch_app(id);
                },
            });
        }
    }

    // 3. Quick theme command
    if (q.contains(QStringLiteral("dark"), Qt::CaseInsensitive) ||
        q.contains(QStringLiteral("light"), Qt::CaseInsensitive) ||
        q.contains(QStringLiteral("theme"), Qt::CaseInsensitive)) {
        const bool is_dark = finn::ui::FinnThemeManager::instance().isDark();
        hits_.push_back(SearchHit{
            {},
            is_dark ? QStringLiteral("Switch to Light Mode")
                    : QStringLiteral("Switch to Dark Mode"),
            QStringLiteral("Toggle system appearance"),
            is_dark ? QStringLiteral("sun-01") : QStringLiteral("moon"),
            QStringLiteral("Theme"),
            false,
            [is_dark]() {
                finn::ui::FinnThemeManager::instance().setDark(!is_dark);
            },
        });
    }

    // 4. Home directory file matches when query >= 2 chars
    if (q.size() >= 2) {
        const QDir home = QDir::home();
        const auto entries = home.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
        int file_hits = 0;
        for (const QFileInfo& fi : entries) {
            if (!fi.fileName().contains(q, Qt::CaseInsensitive)) continue;
            const std::string path = fi.absoluteFilePath().toStdString();
            hits_.push_back(SearchHit{
                {},
                fi.fileName(),
                fi.isDir() ? QStringLiteral("Folder") : QStringLiteral("File"),
                fi.isDir() ? QStringLiteral("folder") : QStringLiteral("files"),
                QStringLiteral("Home"),
                false,
                [this, path]() {
                    if (actions_.open_target) actions_.open_target(path);
                },
            });
            if (++file_hits >= 3) break;
        }
    }

    if (hits_.size() > 5) hits_.resize(5);
    for (int i = 0; i < hits_.size(); ++i) {
        hits_[i].rect = QRect(12, 59 + 12 + i * 41, 326, 38);
    }
    update();
}

void SpotlightPopover::activateSelected() {
    if (selected_idx_ < 0 || selected_idx_ >= hits_.size()) return;
    const auto fn = hits_[selected_idx_].activate;
    if (!fn) return;
    hide();
    emit closed();
    fn();
}

void SpotlightPopover::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pt = event->position().toPoint();
    int app_idx = -1;
    int query_idx = -1;
    int hit_idx = -1;

    if (query_edit_->text().trimmed().isEmpty()) {
        for (int i = 0; i < empty_app_hits_.size(); ++i) {
            if (empty_app_hits_[i].rect.contains(pt)) {
                app_idx = i;
                break;
            }
        }
        for (int i = 0; i < empty_query_hits_.size(); ++i) {
            if (empty_query_hits_[i].rect.contains(pt)) {
                query_idx = i;
                break;
            }
        }
    } else {
        for (int i = 0; i < hits_.size(); ++i) {
            if (hits_[i].rect.contains(pt)) {
                hit_idx = i;
                break;
            }
        }
    }

    if (app_idx != hovered_empty_app_ || query_idx != hovered_empty_query_ ||
        hit_idx != hovered_hit_) {
        hovered_empty_app_ = app_idx;
        hovered_empty_query_ = query_idx;
        hovered_hit_ = hit_idx;
        const bool hand =
            hovered_empty_app_ >= 0 || hovered_empty_query_ >= 0 ||
            (hovered_hit_ >= 0 && static_cast<bool>(hits_[hovered_hit_].activate));
        setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    FinnLayerWindow::mouseMoveEvent(event);
}

void SpotlightPopover::leaveEvent(QEvent* event) {
    if (hovered_empty_app_ != -1 || hovered_empty_query_ != -1 ||
        hovered_hit_ != -1) {
        hovered_empty_app_ = -1;
        hovered_empty_query_ = -1;
        hovered_hit_ = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
    FinnLayerWindow::leaveEvent(event);
}

void SpotlightPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pt = event->position().toPoint();
        if (query_edit_->text().trimmed().isEmpty()) {
            for (const auto& item : empty_app_hits_) {
                if (item.rect.contains(pt) && item.activate) {
                    const auto fn = item.activate;
                    hide();
                    emit closed();
                    fn();
                    return;
                }
            }
            for (const auto& item : empty_query_hits_) {
                if (item.rect.contains(pt) && item.activate) {
                    const auto fn = item.activate;
                    hide();
                    emit closed();
                    fn();
                    return;
                }
            }
        } else {
            for (int i = 0; i < hits_.size(); ++i) {
                if (hits_[i].rect.contains(pt)) {
                    selected_idx_ = i;
                    activateSelected();
                    return;
                }
            }
        }
    }
    FinnLayerWindow::mousePressEvent(event);
}

void SpotlightPopover::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    const QColor glass = popoverGlassFill(t.dark);
    const QColor dim = popoverDimInk(t.dark);
    const QColor ink = t.dark ? QColor(0xF2, 0xF4, 0xF8) : QColor(0, 0, 0);

    // Top search pill: x=25, y=0, w=300, h=44, r=22
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawRoundedRect(QRectF(25.0, 0.0, 300.0, 44.0), 22.0, 22.0);
    paintMonochromeIcon(p, QRectF(33.0, 8.0, 28.0, 28.0),
                        QStringLiteral("search-01"), ink, dpr);

    // Bottom recommendations / results card: x=0, y=59, w=350, h=228, r=12
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawRoundedRect(QRectF(0.0, 59.0, 350.0, 228.0), 12.0, 12.0);

    if (query_edit_->text().trimmed().isEmpty()) {
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(15.0, 59.0 + 12.0, 320.0, 18.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Recommendations"));

        constexpr qreal col_x[5] = {15.0, 87.0, 159.0, 231.0, 303.0};
        const QFontMetricsF fm14(popoverFont(14));
        for (int i = 0; i < empty_app_hits_.size() && i < 5; ++i) {
            const auto& item = empty_app_hits_[i];
            if (hovered_empty_app_ == i) {
                paintHoverFill(p, QRectF(col_x[i] - 10.0, 96.0, 52.0, 62.0), 8.0,
                               t.dark);
            }
            paintAppIconTile(p, QRectF(col_x[i], 59.0 + 41.0, 32.0, 32.0), 8.0,
                             item.icon, dpr);
            p.setFont(popoverFont(14));
            p.setPen(ink);
            const qreal center_x = col_x[i] + 16.0;
            const qreal box_w =
                std::min<qreal>(66.0, 2.0 * std::min(center_x, 350.0 - center_x));
            const QString label = fm14.elidedText(item.title, Qt::ElideRight, box_w);
            p.drawText(QRectF(center_x - box_w * 0.5, 59.0 + 41.0 + 37.0, box_w, 18.0),
                       Qt::AlignHCenter | Qt::AlignTop, label);
        }

        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(15.0, 59.0 + 112.0, 320.0, 18.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Recently asked"));

        const QColor badge_bg =
            t.dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 24);
        for (int i = 0; i < empty_query_hits_.size(); ++i) {
            const auto& qh = empty_query_hits_[i];
            const QRectF r(qh.rect);
            if (hovered_empty_query_ == i) {
                paintHoverFill(p, r.adjusted(-4.0, -3.0, 4.0, 3.0), 7.0, t.dark);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(badge_bg);
            p.drawRoundedRect(QRectF(r.x(), r.y(), 28.0, 28.0), 7.0, 7.0);
            paintMonochromeIcon(p, QRectF(r.x() + 6.0, r.y() + 6.0, 16.0, 16.0),
                                qh.icon, ink, dpr);

            p.setFont(popoverFont(14));
            p.setPen(ink);
            const QString qtxt = fm14.elidedText(qh.title, Qt::ElideRight, 117.0);
            p.drawText(QRectF(r.x() + 38.0, r.y(), 117.0, 28.0),
                       Qt::AlignVCenter | Qt::AlignLeft, qtxt);
        }
        return;
    }

    if (hits_.isEmpty()) {
        p.setFont(popoverFont(14));
        p.setPen(ink);
        p.drawText(QRectF(20.0, 132.0, 310.0, 22.0), Qt::AlignCenter,
                   QStringLiteral("No matches found"));
        p.setFont(popoverFont(12));
        p.setPen(dim);
        p.drawText(QRectF(20.0, 158.0, 310.0, 20.0), Qt::AlignCenter,
                   QStringLiteral("Try an app name, file, or math like 128*4."));
        return;
    }

    const QFontMetricsF fm13(popoverFont(13));
    const QFontMetricsF fm11(popoverFont(11));
    for (int i = 0; i < hits_.size(); ++i) {
        const auto& hit = hits_[i];
        const QRectF r(hit.rect);
        if (i == selected_idx_ || i == hovered_hit_) {
            paintHoverFill(p, r, 8.0, t.dark);
        }
        if (hit.is_app_icon) {
            paintAppIconTile(p, QRectF(r.x() + 8.0, r.y() + 7.0, 24.0, 24.0),
                             6.0, hit.icon, dpr);
        } else {
            paintMonochromeIcon(p, QRectF(r.x() + 10.0, r.y() + 9.0, 20.0, 20.0),
                                hit.icon, ink, dpr);
        }

        const qreal text_w = r.width() - 96.0;
        p.setFont(popoverFont(13));
        p.setPen(ink);
        p.drawText(QRectF(r.x() + 40.0, r.y() + 3.0, text_w, 17.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   fm13.elidedText(hit.title, Qt::ElideRight, text_w));

        p.setFont(popoverFont(11));
        p.setPen(dim);
        p.drawText(QRectF(r.x() + 40.0, r.y() + 20.0, text_w, 15.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   fm11.elidedText(hit.subtitle, Qt::ElideRight, text_w));

        p.drawText(QRectF(r.right() - 54.0, r.y(), 46.0, r.height()),
                   Qt::AlignVCenter | Qt::AlignRight, hit.badge);
    }
}

void SpotlightPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    if (event->key() == Qt::Key_Down && !hits_.isEmpty()) {
        selected_idx_ = (selected_idx_ + 1) % hits_.size();
        update();
        return;
    }
    if (event->key() == Qt::Key_Up && !hits_.isEmpty()) {
        selected_idx_ = (selected_idx_ + hits_.size() - 1) % hits_.size();
        update();
        return;
    }
    FinnLayerWindow::keyPressEvent(event);
}

void SpotlightPopover::resizeEvent(QResizeEvent* event) {
    FinnLayerWindow::resizeEvent(event);
    updatePopoverInputRegions();
}

void SpotlightPopover::showEvent(QShowEvent* event) {
    FinnLayerWindow::showEvent(event);
    QTimer::singleShot(0, this, &SpotlightPopover::updatePopoverInputRegions);
}

bool SpotlightPopover::eventFilter(QObject* watched, QEvent* event) {
    if (watched == query_edit_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            hide();
            emit closed();
            return true;
        }
        if (ke->key() == Qt::Key_Down && !hits_.isEmpty()) {
            selected_idx_ = (selected_idx_ + 1) % hits_.size();
            update();
            return true;
        }
        if (ke->key() == Qt::Key_Up && !hits_.isEmpty()) {
            selected_idx_ = (selected_idx_ + hits_.size() - 1) % hits_.size();
            update();
            return true;
        }
    }
    return FinnLayerWindow::eventFilter(watched, event);
}

bool SpotlightPopover::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
        emit closed();
    }
    return FinnLayerWindow::event(event);
}

// ---- ControlCenterPopover (Figma 11:19 — 444x300, r=12) ----

ControlCenterPopover::ControlCenterPopover(NotificationServer* notifications,
                                           const ShellActions& actions)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-shell-control")),
      actions_(actions) {
    (void)notifications;
    setAnchors(AnchorBottom | AnchorRight);
    setKeyboardMode(KeyboardMode::OnDemand);
    setExclusiveZone(-1);
    setLayerMargins(QMargins(0, 0, 16, 74));
    setDesiredSize(QSize(444, 300));
    setFixedSize(444, 300);
    setMouseTracking(true);

    constexpr int kMinBrightness = 5;
    constexpr int kMinVolume = 0;

    brightness_slider_ = new finn::ui::FinnSlider(kMinBrightness, 100, kMinBrightness, this);
    brightness_slider_->setPillStyle(true, QStringLiteral("sun-01"), 6, 24);
    brightness_slider_->setGeometry(6, 110, 432, 36);

    volume_slider_ = new finn::ui::FinnSlider(kMinVolume, 100, kMinVolume, this);
    volume_slider_->setPillStyle(true, QStringLiteral("volume-02"), 9, 24);
    volume_slider_->setGeometry(6, 156, 432, 36);

    connect(brightness_slider_, &finn::ui::FinnSlider::valueChanged, this,
            [](int v) {
                finn::sys::FinnDesktopServices::instance().setBrightnessPercent(v);
            });
    connect(volume_slider_, &finn::ui::FinnSlider::valueChanged, this,
            [](int v) {
                finn::sys::FinnDesktopServices::instance().setVolumePercent(v);
            });
    connect(&finn::sys::FinnDesktopServices::instance(),
            &finn::sys::FinnDesktopServices::stateUpdated, this,
            &ControlCenterPopover::syncSystemState);
    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this, [this]() {
                if (brightness_slider_) brightness_slider_->update();
                if (volume_slider_) volume_slider_->update();
                update();
            });

    open_anim_ = new QVariantAnimation(this);
    open_anim_->setDuration(160);
    open_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(open_anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& val) {
                open_progress_ = val.toReal();
                const int dy =
                    static_cast<int>(std::lround((1.0 - open_progress_) * 10.0));
                if (brightness_slider_) brightness_slider_->move(6, 110 + dy);
                if (volume_slider_) volume_slider_->move(6, 156 + dy);
                update();
            });

    syncSystemState();
}

void ControlCenterPopover::openDrawer() {
    finn::sys::FinnDesktopServices::instance().refreshAll();
    hovered_ = ControlTarget::None;
    syncSystemState();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->stop();
        open_progress_ = 1.0;
        if (brightness_slider_) brightness_slider_->move(6, 110);
        if (volume_slider_) volume_slider_->move(6, 156);
    } else {
        open_anim_->stop();
        open_progress_ = 0.0;
        if (brightness_slider_) brightness_slider_->move(6, 120);
        if (volume_slider_) volume_slider_->move(6, 166);
        open_anim_->setStartValue(0.0);
        open_anim_->setEndValue(1.0);
        open_anim_->start();
    }
    show();
    raise();
    activateWindow();
    setFocus();
}

void ControlCenterPopover::syncSystemState() {
    constexpr int kMinBrightness = 5;
    constexpr int kMinVolume = 0;
    const auto& svc = finn::sys::FinnDesktopServices::instance();
    const auto& bl = svc.backlight();
    brightness_slider_->setEnabled(bl.available);
    {
        QSignalBlocker bb(brightness_slider_);
        brightness_slider_->setValue(bl.available ? bl.percent : kMinBrightness);
    }

    const auto& aud = svc.audio();
    volume_slider_->setEnabled(aud.available);
    {
        QSignalBlocker ba(volume_slider_);
        volume_slider_->setValue(
            aud.available ? (aud.muted ? kMinVolume : aud.volume_percent)
                          : kMinVolume);
    }

    update();
}

ControlCenterPopover::ControlTarget ControlCenterPopover::targetAt(
    const QPointF& pos) const {
    const QPoint pt = pos.toPoint();
    if (QRect(6, 6, 211, 42).contains(pt)) return ControlTarget::Wifi;
    if (QRect(6, 58, 211, 42).contains(pt)) return ControlTarget::Bluetooth;
    if (QRect(334, 70, 26, 26).contains(pt)) return ControlTarget::MediaPrev;
    if (QRect(367, 70, 26, 26).contains(pt)) return ControlTarget::MediaPlayPause;
    if (QRect(400, 70, 26, 26).contains(pt)) return ControlTarget::MediaNext;
    if (QRect(364, 258, 34, 34).contains(pt)) return ControlTarget::ThemeToggle;
    if (QRect(401, 258, 34, 34).contains(pt)) return ControlTarget::OpenSettings;
    return ControlTarget::None;
}

void ControlCenterPopover::updateHoverTooltip(ControlTarget target) {
    const auto& svc = finn::sys::FinnDesktopServices::instance();
    const auto& media = svc.media();
    bool interactive = false;
    switch (target) {
        case ControlTarget::Wifi:
            interactive = svc.network().wifi_hardware;
            setToolTip(interactive ? QStringLiteral("Toggle Wi-Fi")
                                   : QStringLiteral("No Wi-Fi hardware"));
            break;
        case ControlTarget::Bluetooth:
            interactive = svc.bluetooth().available;
            setToolTip(interactive ? QStringLiteral("Toggle Bluetooth")
                                   : QStringLiteral("No Bluetooth adapter"));
            break;
        case ControlTarget::MediaPrev:
            interactive = media.active;
            setToolTip(interactive ? QStringLiteral("Previous Track")
                                   : QStringLiteral("No active media"));
            break;
        case ControlTarget::MediaPlayPause:
            interactive = media.active;
            setToolTip(interactive ? QStringLiteral("Play / Pause")
                                   : QStringLiteral("No active media"));
            break;
        case ControlTarget::MediaNext:
            interactive = media.active;
            setToolTip(interactive ? QStringLiteral("Next Track")
                                   : QStringLiteral("No active media"));
            break;
        case ControlTarget::ThemeToggle:
            interactive = true;
            setToolTip(finn::ui::FinnThemeManager::instance().isDark()
                           ? QStringLiteral("Switch to Light Mode")
                           : QStringLiteral("Switch to Dark Mode"));
            break;
        case ControlTarget::OpenSettings:
            interactive = true;
            setToolTip(QStringLiteral("Open Settings"));
            break;
        case ControlTarget::None:
            interactive = false;
            setToolTip({});
            break;
    }
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ControlCenterPopover::mouseMoveEvent(QMouseEvent* event) {
    const ControlTarget next = targetAt(event->position());
    if (next != hovered_) {
        hovered_ = next;
        updateHoverTooltip(hovered_);
        update();
    }
    FinnLayerWindow::mouseMoveEvent(event);
}

void ControlCenterPopover::leaveEvent(QEvent* event) {
    if (hovered_ != ControlTarget::None) {
        hovered_ = ControlTarget::None;
        setCursor(Qt::ArrowCursor);
        setToolTip({});
        update();
    }
    FinnLayerWindow::leaveEvent(event);
}

void ControlCenterPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        auto& svc = finn::sys::FinnDesktopServices::instance();
        switch (targetAt(event->position())) {
            case ControlTarget::Wifi:
                if (svc.network().wifi_hardware) {
                    svc.setWifiEnabled(!svc.network().wifi_enabled);
                }
                return;
            case ControlTarget::Bluetooth:
                if (svc.bluetooth().available) {
                    svc.setBluetoothPowered(!svc.bluetooth().powered);
                }
                return;
            case ControlTarget::MediaPrev:
                if (svc.media().active) svc.mediaPrevious();
                return;
            case ControlTarget::MediaPlayPause:
                if (svc.media().active) svc.mediaPlayPause();
                return;
            case ControlTarget::MediaNext:
                if (svc.media().active) svc.mediaNext();
                return;
            case ControlTarget::ThemeToggle: {
                auto& theme = finn::ui::FinnThemeManager::instance();
                theme.setDark(!theme.isDark());
                return;
            }
            case ControlTarget::OpenSettings:
                hide();
                emit closed();
                if (actions_.launch_app) actions_.launch_app("settings");
                return;
            case ControlTarget::None:
                break;
        }
    }
    FinnLayerWindow::mousePressEvent(event);
}

void ControlCenterPopover::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const auto& svc = finn::sys::FinnDesktopServices::instance();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    p.setPen(Qt::NoPen);
    p.setBrush(popoverGlassFill(t.dark));
    p.drawRoundedRect(QRectF(0.0, 0.0, 444.0, 300.0), 12.0, 12.0);

    const QColor ink = t.dark ? QColor(0xF2, 0xF4, 0xF8) : QColor(0, 0, 0);
    const QColor dim = popoverDimInk(t.dark);
    const QColor sub_fill = popoverSubcardFill(t.dark);
    const QColor active_blue(0x00, 0x7B, 0xFF);
    const QColor inactive_circle(0xAB, 0xAB, 0xAB);

    // Wi-Fi card: (6, 6, 211, 42, r=6)
    const auto& net = svc.network();
    const bool wifi_on = net.wifi_hardware && net.wifi_enabled;
    const QRectF wifi_card(6.0, 6.0, 211.0, 42.0);
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(wifi_card, 6.0, 6.0);
    if (hovered_ == ControlTarget::Wifi && net.wifi_hardware) {
        paintHoverFill(p, wifi_card, 6.0, t.dark);
    }
    p.setBrush(wifi_on ? active_blue : inactive_circle);
    p.drawEllipse(QRectF(11.0, 11.0, 32.0, 32.0));
    paintMonochromeIcon(p, QRectF(18.0, 18.0, 18.0, 18.0),
                        QStringLiteral("wifi-02"), Qt::white, dpr);

    QString wifi_sub;
    if (!net.wifi_hardware) {
        wifi_sub = net.wired_connected ? QStringLiteral("Wired only")
                                       : QStringLiteral("Unavailable");
    } else if (!net.wifi_enabled) {
        wifi_sub = QStringLiteral("Off");
    } else if (!net.active_wifi_ssid.isEmpty()) {
        wifi_sub = net.active_wifi_ssid;
    } else {
        wifi_sub = QStringLiteral("Disconnected");
    }
    const QFontMetricsF fm12(popoverFont(12));
    p.setFont(popoverFont(14));
    p.setPen(ink);
    p.drawText(QRectF(51.0, 8.0, 158.0, 18.0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Wi-Fi"));
    p.setFont(popoverFont(12));
    p.setPen(dim);
    p.drawText(QRectF(51.0, 27.0, 158.0, 15.0), Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(wifi_sub, Qt::ElideRight, 158.0));

    // Bluetooth card: (6, 58, 211, 42, r=6)
    const auto& bt = svc.bluetooth();
    const bool bt_on = bt.available && bt.powered;
    const QRectF bt_card(6.0, 58.0, 211.0, 42.0);
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(bt_card, 6.0, 6.0);
    if (hovered_ == ControlTarget::Bluetooth && bt.available) {
        paintHoverFill(p, bt_card, 6.0, t.dark);
    }
    p.setBrush(bt_on ? active_blue : inactive_circle);
    p.drawEllipse(QRectF(11.0, 63.0, 32.0, 32.0));
    paintMonochromeIcon(p, QRectF(18.0, 70.0, 18.0, 18.0),
                        bt_on ? QStringLiteral("bluetooth")
                              : QStringLiteral("bluetooth-off"),
                        Qt::white, dpr);

    const QString bt_sub =
        bt.available ? (bt.powered ? QStringLiteral("On") : QStringLiteral("Off"))
                     : QStringLiteral("Unavailable");
    p.setFont(popoverFont(14));
    p.setPen(ink);
    p.drawText(QRectF(51.0, 60.0, 158.0, 18.0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Bluetooth"));
    p.setFont(popoverFont(12));
    p.setPen(dim);
    p.drawText(QRectF(51.0, 79.0, 158.0, 15.0), Qt::AlignVCenter | Qt::AlignLeft,
               bt_sub);

    // Media Control card: (227, 6, 211, 94, r=6)
    const auto& media = svc.media();
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(QRectF(227.0, 6.0, 211.0, 94.0), 6.0, 6.0);

    // Album art square: 82x82, r=4 at (227+6, 6+6)
    p.setBrush(t.dark ? QColor(255, 255, 255, 0x1F)
                      : QColor(0xD9, 0xD9, 0xD9));
    p.drawRoundedRect(QRectF(233.0, 12.0, 82.0, 82.0), 4.0, 4.0);

    const QString song = media.active ? media.title : QStringLiteral("No media");
    const QString artist =
        media.active
            ? (media.artist.isEmpty() ? media.player_identity : media.artist)
            : QStringLiteral("Idle");

    p.setFont(popoverFont(12));
    p.setPen(media.active ? ink : dim);
    p.drawText(QRectF(325.0, 14.0, 105.0, 15.0), Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(song, Qt::ElideRight, 105.0));
    p.setPen(dim);
    p.drawText(QRectF(325.0, 30.0, 105.0, 15.0), Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(artist, Qt::ElideRight, 105.0));

    // Progress bar + status
    p.setPen(Qt::NoPen);
    p.setBrush(t.dark ? QColor(255, 255, 255, 44) : QColor(0, 0, 0, 36));
    p.drawRoundedRect(QRectF(325.0, 50.0, 105.0, 3.0), 1.5, 1.5);
    if (media.active && media.playing) {
        p.setBrush(t.dark ? QColor(255, 255, 255, 190) : QColor(0, 0, 0, 160));
        p.drawRoundedRect(QRectF(325.0, 50.0, 38.0, 3.0), 1.5, 1.5);
    }

    p.setFont(popoverFont(10));
    p.setPen(dim);
    p.drawText(QRectF(325.0, 55.0, 105.0, 13.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               media.active ? (media.playing ? QStringLiteral("Playing")
                                             : QStringLiteral("Paused"))
                            : QStringLiteral("--:--"));

    const QColor media_tint =
        media.active ? ink : QColor(ink.red(), ink.green(), ink.blue(), 90);
    if (media.active && hovered_ == ControlTarget::MediaPrev) {
        paintHoverFill(p, QRectF(334.0, 70.0, 26.0, 26.0), 6.0, t.dark);
    }
    if (media.active && hovered_ == ControlTarget::MediaPlayPause) {
        paintHoverFill(p, QRectF(367.0, 70.0, 26.0, 26.0), 6.0, t.dark);
    }
    if (media.active && hovered_ == ControlTarget::MediaNext) {
        paintHoverFill(p, QRectF(400.0, 70.0, 26.0, 26.0), 6.0, t.dark);
    }

    paintMonochromeIcon(p, QRectF(339.0, 75.0, 16.0, 16.0),
                        QStringLiteral("backward"), media_tint, dpr);
    if (!media.active || media.playing) {
        paintMonochromeIcon(p, QRectF(372.0, 75.0, 16.0, 16.0),
                            QStringLiteral("pause"), media_tint, dpr);
    } else {
        QPainterPath play_tri;
        play_tri.moveTo(376.0, 76.5);
        play_tri.lineTo(386.0, 83.0);
        play_tri.lineTo(376.0, 89.5);
        play_tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(media_tint);
        p.drawPath(play_tri);
    }
    paintMonochromeIcon(p, QRectF(405.0, 75.0, 16.0, 16.0),
                        QStringLiteral("forward"), media_tint, dpr);

    // Bottom bar: (0, 250, 444, 50)
    paintGlassFooter(p, 444.0, 250.0, 50.0, 12.0, t.dark);

    if (hovered_ == ControlTarget::ThemeToggle) {
        paintHoverFill(p, QRectF(364.0, 258.0, 34.0, 34.0), 8.0, t.dark);
    }
    paintMonochromeIcon(p, QRectF(370.0, 264.0, 22.0, 22.0),
                        QStringLiteral("edit-01"), ink, dpr);

    if (hovered_ == ControlTarget::OpenSettings) {
        paintHoverFill(p, QRectF(401.0, 258.0, 34.0, 34.0), 8.0, t.dark);
    }
    paintMonochromeIcon(p, QRectF(407.0, 264.0, 22.0, 22.0),
                        QStringLiteral("settings-02"), ink, dpr);
}

void ControlCenterPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    FinnLayerWindow::keyPressEvent(event);
}

bool ControlCenterPopover::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
        emit closed();
    }
    return FinnLayerWindow::event(event);
}

// ---- OpenWindowsPopover ----

OpenWindowsPopover::OpenWindowsPopover(ForeignWindowsManager* windows)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-shell-windows")),
      windows_(windows) {
    setAnchors(AnchorBottom);
    setKeyboardMode(KeyboardMode::OnDemand);
    setExclusiveZone(-1);
    setLayerMargins(QMargins(0, 0, 0, 74));
    setDesiredSize(QSize(640, 340));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer_layout_ = outer;

    auto* panel = new finn::ui::FinnGlassPanel(finn::ui::kRadiusXl, this);
    panel->setOpacityAlpha(244);
    panel_ = panel;
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(16, 16, 16, 14);
    root->setSpacing(10);

    root->addWidget(new finn::ui::FinnLabel(
        QStringLiteral("Open Windows"), finn::ui::FinnLabel::Role::Title,
        finn::ui::FinnLabel::Tone::Ink, panel));

    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    grid_host_ = new QWidget(scroll);
    grid_layout_ = new QGridLayout(grid_host_);
    grid_layout_->setContentsMargins(0, 0, 0, 0);
    grid_layout_->setSpacing(8);
    scroll->setWidget(grid_host_);
    root->addWidget(scroll, 1);

    outer->addWidget(panel);

    open_anim_ = new QVariantAnimation(this);
    open_anim_->setDuration(160);
    open_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(open_anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& val) {
                open_progress_ = val.toReal();
                const int dy =
                    static_cast<int>(std::lround((1.0 - open_progress_) * 10.0));
                if (outer_layout_) outer_layout_->setContentsMargins(0, dy, 0, 0);
                if (panel_) {
                    panel_->setOpacityAlpha(static_cast<int>(
                        std::lround(244.0 * std::clamp(open_progress_, 0.0, 1.0))));
                }
                update();
            });

    if (windows_) {
        connect(windows_, &ForeignWindowsManager::windowsChanged, this, [this]() {
            if (isVisible()) rebuildCards();
        });
    }
    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this, [this]() {
                if (isVisible()) rebuildCards();
            });
}

void OpenWindowsPopover::openOverview() {
    rebuildCards();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->stop();
        open_progress_ = 1.0;
        if (outer_layout_) outer_layout_->setContentsMargins(0, 0, 0, 0);
        if (panel_) panel_->setOpacityAlpha(244);
    } else {
        open_anim_->stop();
        open_progress_ = 0.0;
        if (outer_layout_) outer_layout_->setContentsMargins(0, 10, 0, 0);
        if (panel_) panel_->setOpacityAlpha(0);
        open_anim_->setStartValue(0.0);
        open_anim_->setEndValue(1.0);
        open_anim_->start();
    }
    show();
    raise();
    activateWindow();
}

void OpenWindowsPopover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);
}

void OpenWindowsPopover::rebuildCards() {
    clear_grid(grid_layout_);
    if (!windows_ || windows_->windows().isEmpty()) {
        grid_layout_->addWidget(
            new finn::ui::FinnEmptyState(
                QStringLiteral("view-open-windows"),
                QStringLiteral("No open windows"),
                QStringLiteral("Launch any native, Linux, or Windows application to see it here."),
                {}, grid_host_),
            0, 0, 1, 2);
        return;
    }

    int row = 0;
    int col = 0;
    for (const auto& w : windows_->windows()) {
        auto* card = new finn::ui::FinnCard(finn::ui::kRadiusLg, true, grid_host_);
        card->setSelected(w.activated);
        auto* lay = new QHBoxLayout(card);
        lay->setContentsMargins(12, 10, 10, 10);
        lay->setSpacing(10);

        auto* ico = new QLabel(card);
        ico->setFixedSize(26, 26);
        const QString aid = w.app_id.toLower();
        QString resolved_icon = aid.startsWith(QStringLiteral("finn-"))
                                    ? aid.mid(5)
                                    : aid;
        QPixmap pm = finn::ui::FinnThemeManager::instance().iconPixmap(
            resolved_icon, QSize(24, 24), QColor(), devicePixelRatioF());
        if (pm.isNull()) {
            const QIcon fallback = QIcon::fromTheme(w.app_id);
            if (!fallback.isNull()) pm = fallback.pixmap(24, 24);
        }
        ico->setPixmap(pm);
        lay->addWidget(ico);

        auto* text_col = new QVBoxLayout();
        text_col->setSpacing(2);
        text_col->addWidget(new finn::ui::FinnLabel(
            w.title.isEmpty() ? w.app_id : w.title,
            finn::ui::FinnLabel::Role::Headline,
            finn::ui::FinnLabel::Tone::Ink, card));
        const QString status = w.minimized
                                   ? QStringLiteral("Minimized")
                                   : (w.activated ? QStringLiteral("Active")
                                                  : QStringLiteral("Background"));
        text_col->addWidget(new finn::ui::FinnLabel(
            QStringLiteral("%1 — %2").arg(w.app_id, status),
            finn::ui::FinnLabel::Role::Caption,
            finn::ui::FinnLabel::Tone::Dim, card));
        lay->addLayout(text_col, 1);

        auto* close_btn =
            new finn::ui::FinnIconButton(QStringLiteral("x"), 24, 11, card);
        close_btn->setCustomHoverColor(QColor(0xFF, 0x02, 0x02), Qt::white);
        connect(close_btn, &QAbstractButton::clicked, this, [this, id = w.id]() {
            windows_->closeWindow(id);
        });
        connect(card, &finn::ui::FinnCard::clicked, this, [this, id = w.id]() {
            windows_->activateWindow(id);
            hide();
            emit closed();
        });
        lay->addWidget(close_btn);

        grid_layout_->addWidget(card, row, col);
        if (++col >= 2) {
            col = 0;
            ++row;
        }
    }
    grid_layout_->setRowStretch(row + 1, 1);
}

void OpenWindowsPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    FinnLayerWindow::keyPressEvent(event);
}

bool OpenWindowsPopover::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
        emit closed();
    }
    return FinnLayerWindow::event(event);
}

// ---- LockScreenWindow (macOS-inspired Multi-User Lock Screen) ----

LockScreenWindow::LockScreenWindow(QWidget* parent)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-lock"), parent) {
    setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
    setKeyboardMode(KeyboardMode::Exclusive);
    setExclusiveZone(-1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 64, 40, 32);
    root->setSpacing(12);

    date_lbl_ = new finn::ui::FinnLabel({}, finn::ui::FinnLabel::Role::Title,
                                        finn::ui::FinnLabel::Tone::Ink, this);
    date_lbl_->setAlignment(Qt::AlignCenter);

    time_lbl_ = new finn::ui::FinnLabel({}, finn::ui::FinnLabel::Role::TitleLarge,
                                        finn::ui::FinnLabel::Tone::Ink, this);
    time_lbl_->setAlignment(Qt::AlignCenter);

    root->addWidget(date_lbl_, 0, Qt::AlignHCenter);
    root->addWidget(time_lbl_, 0, Qt::AlignHCenter);
    root->addStretch(1);

    auth_box_ = new QWidget(this);
    auto* alay = new QVBoxLayout(auth_box_);
    alay->setContentsMargins(0, 0, 0, 0);
    alay->setSpacing(10);
    alay->setAlignment(Qt::AlignHCenter);

    avatar_lbl_ = new QLabel(auth_box_);
    avatar_lbl_->setFixedSize(72, 72);
    avatar_lbl_->setAlignment(Qt::AlignCenter);

    user_name_lbl_ = new finn::ui::FinnLabel(
        current_user_host(), finn::ui::FinnLabel::Role::Headline,
        finn::ui::FinnLabel::Tone::Ink, auth_box_);
    user_name_lbl_->setAlignment(Qt::AlignCenter);

    auto* pw_pill = new finn::ui::FinnGlassPanel(17.0, auth_box_);
    pw_pill->setFixedSize(230, 34);
    auto* pw_lay = new QHBoxLayout(pw_pill);
    pw_lay->setContentsMargins(14, 2, 4, 2);
    pw_lay->setSpacing(4);

    password_edit_ = new QLineEdit(pw_pill);
    password_edit_->setEchoMode(QLineEdit::Password);
    password_edit_->setFrame(false);
    password_edit_->setPlaceholderText(QStringLiteral("Enter Password"));
    connect(password_edit_, &QLineEdit::returnPressed, this,
            &LockScreenWindow::attemptUnlock);

    auto* unlock_btn = new finn::ui::FinnIconButton(
        QStringLiteral("chevron-right"), 26, 14, pw_pill);
    unlock_btn->setCornerRadius(13.0);
    unlock_btn->setToolTip(QStringLiteral("Unlock"));
    connect(unlock_btn, &QAbstractButton::clicked, this,
            &LockScreenWindow::attemptUnlock);

    pw_lay->addWidget(password_edit_, 1);
    pw_lay->addWidget(unlock_btn);

    error_lbl_ = new finn::ui::FinnLabel(
        {}, finn::ui::FinnLabel::Role::Caption,
        finn::ui::FinnLabel::Tone::Danger, auth_box_);
    error_lbl_->setAlignment(Qt::AlignCenter);
    error_lbl_->setVisible(false);

    alay->addWidget(avatar_lbl_, 0, Qt::AlignHCenter);
    alay->addWidget(user_name_lbl_, 0, Qt::AlignHCenter);
    alay->addWidget(pw_pill, 0, Qt::AlignHCenter);
    alay->addWidget(error_lbl_, 0, Qt::AlignHCenter);

    root->addWidget(auth_box_, 0, Qt::AlignHCenter);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(10);
    users_row_ = new QHBoxLayout();
    users_row_->setSpacing(8);
    bottom->addLayout(users_row_);
    bottom->addStretch(1);

    auto* restart_btn = new finn::ui::FinnButton(
        QStringLiteral("Restart"), finn::ui::FinnButton::Variant::Secondary,
        QStringLiteral("refresh"), this);
    connect(restart_btn, &QAbstractButton::clicked, this, []() {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    });
    auto* power_btn = new finn::ui::FinnButton(
        QStringLiteral("Shut Down"), finn::ui::FinnButton::Variant::Secondary,
        QStringLiteral("power"), this);
    connect(power_btn, &QAbstractButton::clicked, this, []() {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    });
    bottom->addWidget(restart_btn);
    bottom->addWidget(power_btn);
    root->addLayout(bottom);

    loadUsers();

    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this,
            &LockScreenWindow::refreshTheme);

    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &LockScreenWindow::updateClock);
    timer->start();
    refreshTheme();
    updateClock();
}

void LockScreenWindow::refreshTheme() {
    date_lbl_->setFont(finn::ui::finn_font(15.0, QFont::Medium));
    time_lbl_->setFont(finn::ui::finn_font(58.0, QFont::Medium));
    user_name_lbl_->setFont(finn::ui::finn_font(13.5, QFont::DemiBold));
    if (avatar_lbl_) {
        avatar_lbl_->setPixmap(finn::ui::FinnThemeManager::instance().iconPixmap(
            QStringLiteral("avatar"), QSize(64, 64), QColor(),
            devicePixelRatioF()));
    }
    configureBorderlessEdit(
        password_edit_, 14, finn::ui::FinnThemeManager::instance().isDark());
    update();
}

void LockScreenWindow::loadUsers() {
    users_.clear();
    QFile passwd(QStringLiteral("/etc/passwd"));
    if (passwd.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!passwd.atEnd()) {
            const QString line = QString::fromUtf8(passwd.readLine()).trimmed();
            const QStringList parts = line.split(QLatin1Char(':'));
            if (parts.size() < 7) continue;
            bool ok = false;
            const int uid = parts[2].toInt(&ok);
            const QString shell = parts[6];
            if (ok && uid >= 1000 && uid < 65534 &&
                !shell.endsWith(QStringLiteral("nologin")) &&
                !shell.endsWith(QStringLiteral("false"))) {
                QString gecos = parts[4].section(QLatin1Char(','), 0, 0).trimmed();
                if (gecos.isEmpty()) gecos = parts[0];
                users_.push_back({parts[0], gecos, uid});
            }
        }
    }
    const QString cur = qEnvironmentVariable("USER", QStringLiteral("finn"));
    if (users_.isEmpty()) {
        users_.push_back({cur, cur, 1000});
    }

    while (QLayoutItem* item = users_row_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (users_.size() > 1) {
        for (const auto& u : users_) {
            auto* chip = new finn::ui::FinnButton(
                u.display_name, finn::ui::FinnButton::Variant::Secondary,
                QStringLiteral("avatar"), this);
            connect(chip, &QAbstractButton::clicked, this,
                    [this, uname = u.username]() { selectUser(uname); });
            users_row_->addWidget(chip);
        }
    }
    selectUser(cur);
}

void LockScreenWindow::selectUser(const QString& username) {
    selected_user_ = username;
    QString disp = username;
    for (const auto& u : users_) {
        if (u.username == username) {
            disp = u.display_name;
            break;
        }
    }
    user_name_lbl_->setText(disp);
    if (password_edit_) {
        password_edit_->clear();
        password_edit_->setFocus();
    }
    if (error_lbl_) error_lbl_->setVisible(false);
}

void LockScreenWindow::shakeInput() {
    if (!auth_box_ || finn::ui::FinnThemeManager::instance().reducedMotion()) return;
    const QPoint origin = auth_box_->pos();
    auto* anim = new QPropertyAnimation(auth_box_, "pos", this);
    anim->setDuration(280);
    anim->setKeyValueAt(0.0, origin);
    anim->setKeyValueAt(0.2, origin + QPoint(-10, 0));
    anim->setKeyValueAt(0.4, origin + QPoint(10, 0));
    anim->setKeyValueAt(0.6, origin + QPoint(-7, 0));
    anim->setKeyValueAt(0.8, origin + QPoint(7, 0));
    anim->setKeyValueAt(1.0, origin);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void LockScreenWindow::attemptUnlock() {
    const QString pw = password_edit_ ? password_edit_->text() : QString();
    const QString current_user = qEnvironmentVariable("USER", QStringLiteral("finn"));

    // If switching to another user, ask logind to switch VT/session
    if (!selected_user_.isEmpty() && selected_user_ != current_user) {
        QProcess proc;
        proc.start(QStringLiteral("su"), {QStringLiteral("-c"), QStringLiteral("true"), selected_user_});
        proc.write(pw.toUtf8() + '\n');
        proc.closeWriteChannel();
        if (proc.waitForFinished(2000) && proc.exitCode() == 0) {
            QProcess::startDetached(QStringLiteral("loginctl"),
                                    {QStringLiteral("activate")});
            hide();
            return;
        }
        error_lbl_->setText(QStringLiteral("Incorrect password"));
        error_lbl_->setVisible(true);
        password_edit_->clear();
        shakeInput();
        return;
    }

    // Current user unlock: verify via `su -c true` when password entered, or allow
    // empty password only if account has no password set (dev/live session).
    if (!pw.isEmpty()) {
        QProcess proc;
        proc.start(QStringLiteral("su"), {QStringLiteral("-c"), QStringLiteral("true"), current_user});
        proc.write(pw.toUtf8() + '\n');
        proc.closeWriteChannel();
        if (proc.waitForFinished(2000) && proc.exitCode() == 0) {
            error_lbl_->setVisible(false);
            password_edit_->clear();
            hide();
            return;
        }
        error_lbl_->setText(QStringLiteral("Incorrect password"));
        error_lbl_->setVisible(true);
        password_edit_->clear();
        shakeInput();
        return;
    }

    // Check if passwordless login is permitted (e.g. live session / dev VM)
    QProcess check;
    check.start(QStringLiteral("passwd"), {QStringLiteral("-S"), current_user});
    if (check.waitForFinished(1000)) {
        const QString out = QString::fromUtf8(check.readAllStandardOutput());
        if (out.contains(QStringLiteral(" NP ")) || out.contains(QStringLiteral(" L "))) {
            error_lbl_->setVisible(false);
            hide();
            return;
        }
    }
    // If passwd -S is unavailable or requires password, allow empty only if su succeeds
    QProcess su_empty;
    su_empty.start(QStringLiteral("su"), {QStringLiteral("-c"), QStringLiteral("true"), current_user});
    su_empty.closeWriteChannel();
    if (su_empty.waitForFinished(1000) && su_empty.exitCode() == 0) {
        error_lbl_->setVisible(false);
        hide();
        return;
    }
    error_lbl_->setText(QStringLiteral("Password required"));
    error_lbl_->setVisible(true);
    shakeInput();
}

void LockScreenWindow::lockNow() {
    loadUsers();
    updateClock();
    show();
    raise();
    activateWindow();
    if (password_edit_) password_edit_->setFocus();
}

void LockScreenWindow::updateClock() {
    const QDateTime now = QDateTime::currentDateTime();
    time_lbl_->setText(now.toString(QStringLiteral("HH:mm")));
    date_lbl_->setText(now.toString(QStringLiteral("dddd, MMMM d")));
}

void LockScreenWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        attemptUnlock();
        return;
    }
    FinnLayerWindow::keyPressEvent(event);
}

void LockScreenWindow::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.fillRect(rect(), t.dark ? QColor(12, 14, 18, 225)
                              : QColor(240, 244, 248, 225));
}

// ---- BootSplashWindow (FinnOS Rounding Line -> "hi." / Slinky Boot Animation) ----

BootSplashWindow::BootSplashWindow(QWidget* parent)
    : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-boot"), parent) {
    setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
    setKeyboardMode(KeyboardMode::None);
    setExclusiveZone(-1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    ribbon_ = new finn::ui::FinnRibbonAnimation(
        finn::ui::FinnRibbonAnimation::Mode::HiIntro, this);
    root->addWidget(ribbon_, 1);
    connect(ribbon_, &finn::ui::FinnRibbonAnimation::frameAdvanced, this,
            [this](double) { update(); });

    fade_anim_ = new QVariantAnimation(this);
    fade_anim_->setDuration(320);
    fade_anim_->setStartValue(1.0);
    fade_anim_->setEndValue(0.0);
    fade_anim_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(fade_anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
                opacity_ = v.toReal();
                update();
            });
    connect(fade_anim_, &QVariantAnimation::finished, this, [this]() {
        setInputRegions({});
        hide();
    });
}

void BootSplashWindow::startBootAnimation(bool hold_open) {
    opacity_ = 1.0;
    clearMask();
    ribbon_->restart();
    show();
    raise();
    if (!hold_open) {
        QTimer::singleShot(3100, this, [this]() {
            if (isVisible()) fade_anim_->start();
        });
    }
}

void BootSplashWindow::mousePressEvent(QMouseEvent*) {
    if (fade_anim_->state() != QAbstractAnimation::Running) {
        fade_anim_->start();
    }
}

void BootSplashWindow::keyPressEvent(QKeyEvent*) {
    if (fade_anim_->state() != QAbstractAnimation::Running) {
        fade_anim_->start();
    }
}

void BootSplashWindow::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(opacity_);

    const QColor bg = t.dark ? QColor(0x16, 0x19, 0x22) : QColor(0xF5, 0xF5, 0xF5);
    p.fillRect(rect(), bg);

    // Top Branding lockup matching Figma 36:45 (20x20 mark + 15px gap + "FinnOS" 20px),
    // revealed only AFTER the rounding line finishes drawing "hi."
    const qreal reveal = ribbon_ ? ribbon_->uiRevealProgress() : 1.0;
    if (reveal <= 0.005) return;

    p.setOpacity(opacity_ * reveal);
    const qreal brand_y = std::max(48.0, height() * (180.0 / 832.0));
    const QColor ink = t.dark ? t.ink : QColor(0, 0, 0);
    QFont brand_font(finn::ui::primary_font_family());
    brand_font.setPixelSize(20);
    brand_font.setWeight(QFont::Normal);
    p.setFont(brand_font);
    const QFontMetrics fm(brand_font);
    const QString brand_text = QStringLiteral("FinnOS");
    const int text_w = fm.horizontalAdvance(brand_text);
    const qreal total_w = 20.0 + 15.0 + text_w;
    const qreal start_x = (width() - total_w) * 0.5;

    finn::ui::FinnThemeManager::instance().paintIcon(
        &p, QRectF(start_x, brand_y, 20.0, 20.0), QStringLiteral("boot-mark"), ink);
    p.setPen(ink);
    p.drawText(QRectF(start_x + 35.0, brand_y - 4.0, text_w + 8.0, 28.0),
               Qt::AlignLeft | Qt::AlignVCenter, brand_text);
}

}  // namespace finn::shell
