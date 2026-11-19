// Implements HomeMenuPopover, SpotlightPopover, ControlCenterPopover,
// OpenWindowsPopover, and LockScreenWindow with 1:1 Figma layout and live theme reload.

#include "shell/popovers.hpp"

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#include "common/paths.hpp"
#include "shell/search.hpp"
#include "system/desktop_services.hpp"
#include "system/sys_files.hpp"

namespace finn::shell {
namespace {

QFont popoverFont(int px, QFont::Weight weight = QFont::Normal) {
    QFont font(finn::ui::primary_font_family());
    font.setPixelSize(px);
    font.setWeight(weight);
    return font;
}

void paintHoverFill(QPainter& p, const QRectF& rect, qreal radius,
                    const finn::ui::FinnTokens& t) {
    p.setPen(Qt::NoPen);
    p.setBrush(shell_hover_fill(t));
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
                      qreal radius, const finn::ui::FinnTokens& t) {
    QPainterPath outer_clip;
    outer_clip.addRoundedRect(QRectF(0.0, 0.0, width, footer_y + footer_h),
                              radius, radius);
    p.save();
    p.setClipPath(outer_clip);
    p.fillRect(QRectF(0.0, footer_y, width, footer_h), shell_footer_fill(t));
    p.restore();
}

void configureBorderlessEdit(QLineEdit* edit, int px,
                             const finn::ui::FinnTokens& t) {
    if (!edit) return;
    edit->setFont(popoverFont(px));
    edit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; }"));
    QPalette pal = edit->palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, t.ink);
    pal.setColor(QPalette::PlaceholderText, t.dim);
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
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Home Menu"));

    connect(&finn::ui::FinnThemeManager::instance(),
            &finn::ui::FinnThemeManager::themeChanged, this,
            [this]() { update(); });

    open_anim_ = std::make_unique<ShellSpringDriver>(
        this, finn::ui::springs::kSnappy, 1.0, [this](double val) {
            open_progress_ = std::clamp(val, 0.0, 1.0);
            update();
        });
    applyResponsiveLayout();
    rebuildHits();
}

void HomeMenuPopover::applyResponsiveLayout() {
    const FormFactor ff = current_form_factor();
    const QSize screen = primary_screen_size();
    int target_w = 585;
    int target_h = 460;
    int bottom_margin = 74;
    if (ff == FormFactor::Phone) {
        target_w = std::clamp(screen.width() - 16, 320, 420);
        target_h = std::min(460, std::max(340, screen.height() - 108));
        bottom_margin = 62;
    } else if (ff == FormFactor::Tablet) {
        target_w = std::min(585, std::max(480, screen.width() - 24));
        target_h = std::min(460, std::max(360, screen.height() - 88));
        bottom_margin = 62;
    }
    setLayerMargins(QMargins(0, 0, 0, bottom_margin));
    setDesiredSize(QSize(target_w, target_h));
    setFixedSize(target_w, target_h);
}

void HomeMenuPopover::openMenu() {
    if (actions_.reload_apps) actions_.reload_apps();
    applyResponsiveLayout();
    active_category_.clear();
    hovered_hit_ = -1;
    focused_hit_ = -1;
    rebuildHits();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->set(1.0);
    } else {
        open_anim_->set(0.0);
        open_anim_->retarget(1.0);
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
    focused_hit_ = -1;

    const int w = std::max(320, width());
    const int h = std::max(340, height());
    const bool compact = w < 520;
    const int footer_y = h - 50;

    const auto favs = favoriteApps();
    const std::size_t max_favs = compact ? 5 : 10;
    for (std::size_t i = 0; i < favs.size() && i < max_favs; ++i) {
        const int col = compact ? 0 : static_cast<int>(i / 5);
        const int row = compact ? static_cast<int>(i) : static_cast<int>(i % 5);
        const QRect r(15 + col * 140, 46 + row * 42, compact ? 148 : 130, 32);
        hits_.push_back({r, HitItem::Kind::LaunchApp,
                         QString::fromStdString(favs[i]->id),
                         QString::fromStdString(favs[i]->name)});
    }

    const int right_x = compact ? 182 : 310;
    if (active_category_.isEmpty()) {
        const auto& cats = menuCategories();
        const int cols = compact ? 2 : 3;
        const int cell_w = compact ? 74 : 80;
        const int step_x = compact ? 82 : 90;
        const int step_y = compact ? 84 : 106;
        for (int i = 0; i < cats.size(); ++i) {
            const int col = i % cols;
            const int row = i / cols;
            const QRect r(right_x + col * step_x, 46 + row * step_y, cell_w,
                          compact ? 78 : 96);
            hits_.push_back({r, HitItem::Kind::OpenCategory, cats[i], cats[i]});
        }
    } else {
        hits_.push_back({QRect(right_x, 14, w - right_x - 12, 24),
                         HitItem::Kind::BackToAll, {},
                         QStringLiteral("Back to All Apps")});
        const auto cat_apps = categoryApps(active_category_);
        const std::size_t max_cat = compact ? 5 : 10;
        for (std::size_t i = 0; i < cat_apps.size() && i < max_cat; ++i) {
            const int col = compact ? 0 : static_cast<int>(i / 5);
            const int row = compact ? static_cast<int>(i) : static_cast<int>(i % 5);
            const QRect r(right_x + col * 135, 46 + row * 42,
                          compact ? (w - right_x - 16) : 128, 32);
            hits_.push_back({r, HitItem::Kind::LaunchApp,
                             QString::fromStdString(cat_apps[i]->id),
                             QString::fromStdString(cat_apps[i]->name)});
        }
    }

    if (footer_y >= 340) {
        const auto recents = recentApps();
        const std::size_t max_rec = compact ? 4 : 8;
        const int rec_y0 = compact ? 302 : 297;
        for (std::size_t i = 0; i < recents.size() && i < max_rec; ++i) {
            const int col = static_cast<int>(i % (compact ? 2 : 4));
            const int row = static_cast<int>(i / (compact ? 2 : 4));
            const int rx =
                compact ? (15 + col * ((w - 30) / 2))
                        : (15 + (col == 3 ? 424 : col * 141));
            const int ry = rec_y0 + row * 42;
            if (ry + 32 >= footer_y) break;
            hits_.push_back({QRect(rx, ry, compact ? ((w - 42) / 2) : 131, 32),
                             HitItem::Kind::LaunchApp,
                             QString::fromStdString(recents[i]->id),
                             QString::fromStdString(recents[i]->name)});
        }
    }

    hits_.push_back({QRect(8, footer_y + 5, compact ? (w - 96) : 240, 40),
                     HitItem::Kind::Lock, {}, QStringLiteral("Lock Screen")});
    hits_.push_back({QRect(w - 80, footer_y + 8, 34, 34),
                     HitItem::Kind::Reboot, {}, QStringLiteral("Restart")});
    hits_.push_back({QRect(w - 43, footer_y + 8, 34, 34),
                     HitItem::Kind::PowerOff, {}, QStringLiteral("Shut Down")});
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

void HomeMenuPopover::activateHit(const HitItem& hit) {
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

void HomeMenuPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pt = event->position().toPoint();
        for (int i = 0; i < hits_.size(); ++i) {
            if (!hits_[i].rect.contains(pt)) continue;
            focused_hit_ = i;
            activateHit(hits_[i]);
            return;
        }
    }
    FinnLayerWindow::mousePressEvent(event);
}

void HomeMenuPopover::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const int w = std::max(320, width());
    const int h = std::max(340, height());
    const bool compact = w < 520;
    const int footer_y = h - 50;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    p.setPen(QPen(t.hair, 1.0));
    p.setBrush(shell_glass_fill(t));
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), finn::ui::radii::r_lg,
                      finn::ui::radii::r_lg);

    const QColor dim = t.dim;
    const QColor ink = t.ink;
    const QColor placeholder = shell_hover_fill(t);

    // Top-left: Your favorite apps
    p.setFont(popoverFont(14));
    p.setPen(dim);
    p.drawText(QRectF(15.0, 14.0, compact ? 155.0 : 260.0, 24.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               compact ? QStringLiteral("Favorites")
                       : QStringLiteral("Your favorite apps"));

    const auto favs = favoriteApps();
    const QFontMetricsF fm13_list(popoverFont(13));
    const std::size_t max_favs = compact ? 5 : 10;
    for (std::size_t i = 0; i < favs.size() && i < max_favs; ++i) {
        const int col = compact ? 0 : static_cast<int>(i / 5);
        const int row = compact ? static_cast<int>(i) : static_cast<int>(i % 5);
        const QRectF item_r(15.0 + col * 140.0, 46.0 + row * 42.0,
                            compact ? 148.0 : 130.0, 32.0);
        const bool is_hovered =
            hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
        if (is_hovered) {
            paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0),
                           finn::ui::radii::r_md, t);
        }
        paintAppIconTile(p, QRectF(item_r.x(), item_r.y(), 32.0, 32.0),
                         finn::ui::radii::r_md,
                         QString::fromStdString(favs[i]->icon), dpr);
        p.setFont(popoverFont(13));
        p.setPen(ink);
        const qreal label_w = item_r.width() - 36.0;
        const QString name = fm13_list.elidedText(
            QString::fromStdString(favs[i]->name), Qt::ElideRight, label_w);
        p.drawText(QRectF(item_r.x() + 40.0, item_r.y(), label_w, 32.0),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
    }

    // Center 1px vertical divider
    p.fillRect(QRectF(compact ? 171.0 : 293.0, 96.0, 1.0, 100.0), t.hair);

    // Top-right: All Apps or expanded Category
    const qreal right_x = compact ? 182.0 : 310.0;
    if (active_category_.isEmpty()) {
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(right_x, 14.0, w - right_x - 12.0, 24.0),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("All Apps"));

        const auto& cats = menuCategories();
        const int cols = compact ? 2 : 3;
        const qreal tile_sz = compact ? 64.0 : 80.0;
        const qreal step_x = compact ? 82.0 : 90.0;
        const qreal step_y = compact ? 84.0 : 106.0;
        for (int idx = 0; idx < cats.size(); ++idx) {
            const int col = idx % cols;
            const int row = idx / cols;
            const qreal fx = right_x + col * step_x;
            const qreal fy = 46.0 + row * step_y;
            const QRect hit_rect(static_cast<int>(fx), static_cast<int>(fy),
                                 compact ? 74 : 80, compact ? 78 : 96);
            const bool is_hovered =
                hovered_hit_ >= 0 && hits_[hovered_hit_].rect == hit_rect;

            p.setPen(Qt::NoPen);
            p.setBrush(is_hovered ? shell_pressed_fill(t)
                                  : shell_subcard_fill(t));
            p.drawRoundedRect(QRectF(fx, fy, tile_sz, tile_sz),
                              finn::ui::radii::r_lg, finn::ui::radii::r_lg);

            const auto cat_apps = categoryApps(cats[idx]);
            const qreal big_sz = compact ? 25.0 : 32.0;
            const qreal big_off = compact ? 35.0 : 41.0;
            const qreal pad = compact ? 5.0 : 7.0;
            const QRectF big_slots[3] = {
                QRectF(fx + pad, fy + pad, big_sz, big_sz),
                QRectF(fx + big_off, fy + pad, big_sz, big_sz),
                QRectF(fx + pad, fy + big_off, big_sz, big_sz),
            };
            const qreal mini_sz = compact ? 11.0 : 14.0;
            const qreal mini_step = compact ? 14.0 : 18.0;
            const QRectF mini_slots[4] = {
                QRectF(fx + big_off, fy + big_off, mini_sz, mini_sz),
                QRectF(fx + big_off + mini_step, fy + big_off, mini_sz, mini_sz),
                QRectF(fx + big_off, fy + big_off + mini_step, mini_sz, mini_sz),
                QRectF(fx + big_off + mini_step, fy + big_off + mini_step, mini_sz,
                       mini_sz),
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
            p.drawText(QRectF(fx - 4.0, fy + tile_sz + 2.0, tile_sz + 8.0, 14.0),
                       Qt::AlignHCenter | Qt::AlignTop, cats[idx]);
        }
    } else {
        const bool back_hovered =
            hovered_hit_ >= 0 &&
            hits_[hovered_hit_].kind == HitItem::Kind::BackToAll;
        if (back_hovered) {
            paintHoverFill(p, QRectF(right_x - 4.0, 14.0, 180.0, 24.0),
                           finn::ui::radii::r_sm, t);
        }
        paintMonochromeIcon(p, QRectF(right_x, 19.0, 14.0, 14.0),
                            QStringLiteral("chevron-left"), dim, dpr);
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(right_x + 18.0, 14.0, w - right_x - 24.0, 24.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("All Apps  /  %1").arg(active_category_));

        const auto cat_apps = categoryApps(active_category_);
        if (cat_apps.empty()) {
            p.setFont(popoverFont(13));
            p.setPen(dim);
            p.drawText(QRectF(right_x, 46.0, w - right_x - 12.0, 190.0),
                       Qt::AlignVCenter | Qt::AlignHCenter,
                       QStringLiteral("No %1 apps installed").arg(active_category_));
        } else {
            const std::size_t max_cat = compact ? 5 : 10;
            for (std::size_t i = 0; i < cat_apps.size() && i < max_cat; ++i) {
                const int col = compact ? 0 : static_cast<int>(i / 5);
                const int row = compact ? static_cast<int>(i) : static_cast<int>(i % 5);
                const QRectF item_r(right_x + col * 135.0, 46.0 + row * 42.0,
                                    compact ? (w - right_x - 16.0) : 128.0, 32.0);
                const bool is_hovered =
                    hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
                if (is_hovered) {
                    paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0),
                                   finn::ui::radii::r_md, t);
                }
                paintAppIconTile(p, QRectF(item_r.x(), item_r.y(), 32.0, 32.0),
                                 finn::ui::radii::r_md,
                                 QString::fromStdString(cat_apps[i]->icon), dpr);
                p.setFont(popoverFont(13));
                p.setPen(ink);
                const qreal label_w = item_r.width() - 38.0;
                const QString name = fm13_list.elidedText(
                    QString::fromStdString(cat_apps[i]->name), Qt::ElideRight,
                    label_w);
                p.drawText(QRectF(item_r.x() + 40.0, item_r.y(), label_w, 32.0),
                           Qt::AlignVCenter | Qt::AlignLeft, name);
            }
        }
    }

    // Middle: Recent activity
    if (footer_y >= 340) {
        const qreal rec_hdr_y = compact ? 274.0 : 267.0;
        const qreal rec_y0 = compact ? 302.0 : 297.0;
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(15.0, rec_hdr_y, w - 30.0, 22.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Recent activity"));

        const auto recents = recentApps();
        if (recents.empty()) {
            p.setFont(popoverFont(13));
            p.setPen(dim);
            p.drawText(QRectF(15.0, rec_y0, w - 30.0, footer_y - rec_y0 - 8.0),
                       Qt::AlignTop | Qt::AlignLeft,
                       QStringLiteral("No recent activity yet"));
        } else {
            const QFontMetricsF fm13(popoverFont(13));
            const std::size_t max_rec = compact ? 4 : 8;
            for (std::size_t i = 0; i < recents.size() && i < max_rec; ++i) {
                const int col = static_cast<int>(i % (compact ? 2 : 4));
                const int row = static_cast<int>(i / (compact ? 2 : 4));
                const qreal rx = compact
                                     ? (15.0 + col * ((w - 30.0) * 0.5))
                                     : (15.0 + (col == 3 ? 424.0 : col * 141.0));
                const qreal ry = rec_y0 + row * 42.0;
                if (ry + 32.0 >= footer_y) break;
                const qreal rw = compact ? ((w - 42.0) * 0.5) : 131.0;
                const QRectF item_r(rx, ry, rw, 32.0);
                const bool is_hovered =
                    hovered_hit_ >= 0 && hits_[hovered_hit_].rect == item_r.toRect();
                if (is_hovered) {
                    paintHoverFill(p, item_r.adjusted(-4.0, -3.0, 4.0, 3.0),
                                   finn::ui::radii::r_md, t);
                }
                paintAppIconTile(p, QRectF(rx, ry, 32.0, 32.0),
                                 finn::ui::radii::r_md,
                                 QString::fromStdString(recents[i]->icon), dpr);
                p.setFont(popoverFont(13));
                p.setPen(ink);
                const qreal label_w = rw - 40.0;
                const QString name = fm13.elidedText(
                    QString::fromStdString(recents[i]->name), Qt::ElideRight,
                    label_w);
                p.drawText(QRectF(rx + 40.0, ry + 1.0, label_w, 16.0),
                           Qt::AlignVCenter | Qt::AlignLeft, name);
                p.setFont(popoverFont(11));
                p.setPen(dim);
                p.drawText(QRectF(rx + 40.0, ry + 17.0, label_w, 14.0),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           QStringLiteral("Recently opened"));
            }
        }
    }

    // Bottom bar
    paintGlassFooter(p, w, footer_y, 50.0, finn::ui::radii::r_lg, t);

    const bool lock_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::Lock;
    if (lock_hover) {
        paintHoverFill(p, QRectF(8.0, footer_y + 5.0, compact ? (w - 96.0) : 240.0, 40.0),
                       finn::ui::radii::r_md, t);
    }
    const QPixmap avatar = finn::ui::FinnThemeManager::instance().avatarPixmap(
        QSize(30, 30), dpr);
    if (!avatar.isNull()) {
        p.drawPixmap(QRect(15, footer_y + 10, 30, 30), avatar);
    }
    p.setFont(popoverFont(16));
    p.setPen(ink);
    p.drawText(QRectF(55.0, footer_y, 200.0, 50.0),
               Qt::AlignVCenter | Qt::AlignLeft, current_username());

    const bool reboot_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::Reboot;
    if (reboot_hover) {
        paintHoverFill(p, QRectF(w - 80.0, footer_y + 8.0, 34.0, 34.0),
                       finn::ui::radii::r_md, t);
    }
    paintMonochromeIcon(p, QRectF(w - 74.0, footer_y + 14.0, 22.0, 22.0),
                        QStringLiteral("refresh"), ink, dpr);

    const bool power_hover =
        hovered_hit_ >= 0 && hits_[hovered_hit_].kind == HitItem::Kind::PowerOff;
    if (power_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(shell_danger(t));
        p.drawRoundedRect(QRectF(w - 43.0, footer_y + 8.0, 34.0, 34.0),
                          finn::ui::radii::r_md, finn::ui::radii::r_md);
    }
    paintMonochromeIcon(p, QRectF(w - 37.0, footer_y + 14.0, 22.0, 22.0),
                        QStringLiteral("power"),
                        power_hover ? shell_on_accent(t) : ink, dpr);

    if (focused_hit_ >= 0 && focused_hit_ < hits_.size()) {
        draw_focus_ring(p, QRectF(hits_[focused_hit_].rect),
                        finn::ui::radii::r_md, t);
    }
}

void HomeMenuPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    if (!hits_.isEmpty()) {
        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Down ||
            event->key() == Qt::Key_Right) {
            focused_hit_ = (focused_hit_ + 1) % hits_.size();
            update();
            return;
        }
        if (event->key() == Qt::Key_Backtab || event->key() == Qt::Key_Up ||
            event->key() == Qt::Key_Left) {
            focused_hit_ =
                (focused_hit_ <= 0) ? (hits_.size() - 1) : (focused_hit_ - 1);
            update();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            focused_hit_ >= 0 && focused_hit_ < hits_.size()) {
            activateHit(hits_[focused_hit_]);
            return;
        }
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
    setKeyboardMode(KeyboardMode::Exclusive);
    setExclusiveZone(-1);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Spotlight Search"));

    query_edit_ = new QLineEdit(this);
    query_edit_->setFrame(false);
    query_edit_->setAttribute(Qt::WA_TranslucentBackground);
    query_edit_->setPlaceholderText(QStringLiteral("Search or ask anything"));
    query_edit_->setAccessibleName(QStringLiteral("Search query"));
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

    open_anim_ = std::make_unique<ShellSpringDriver>(
        this, finn::ui::springs::kSnappy, 1.0, [this](double val) {
            open_progress_ = std::clamp(val, 0.0, 1.0);
            if (query_edit_) {
                const int pill_x = std::max(12, (width() - 300) / 2);
                const int dy =
                    static_cast<int>(std::lround((1.0 - open_progress_) * 10.0));
                query_edit_->move(pill_x + 46, dy);
            }
            update();
        });

    applyResponsiveLayout();
    updateResults({});
    updatePopoverInputRegions();
}

void SpotlightPopover::applyResponsiveLayout() {
    const FormFactor ff = current_form_factor();
    const QSize screen = primary_screen_size();
    int w = 350;
    if (ff == FormFactor::Phone) {
        w = std::clamp(screen.width() - 16, 320, 400);
        setAnchors(AnchorTop);
        setLayerMargins(QMargins(8, 44, 8, 0));
    } else {
        setAnchors(AnchorBottom);
        setLayerMargins(
            QMargins(0, 0, 0, ff == FormFactor::Tablet ? 62 : 74));
    }
    setDesiredSize(QSize(w, 287));
    setFixedSize(w, 287);
    if (query_edit_) {
        const int pill_w = std::min(300, w - 24);
        const int pill_x = (w - pill_w) / 2;
        query_edit_->setGeometry(pill_x + 46, 0, pill_w - 60, 44);
    }
}

void SpotlightPopover::applyQueryEditTheme() {
    configureBorderlessEdit(query_edit_, 16,
                            finn::ui::FinnThemeManager::instance().tokens());
}

void SpotlightPopover::updatePopoverInputRegions() {
    const int w = std::max(320, width());
    const int pill_w = std::min(300, w - 24);
    const int pill_x = (w - pill_w) / 2;
    setInputRegions({QRect(pill_x, 0, pill_w, 44), QRect(0, 59, w, 228)});
}

void SpotlightPopover::openSearch() {
    if (actions_.reload_apps) actions_.reload_apps();
    applyResponsiveLayout();
    hovered_empty_app_ = -1;
    hovered_empty_query_ = -1;
    hovered_hit_ = -1;
    query_edit_->clear();
    updateResults({});
    const int pill_w = std::min(300, width() - 24);
    const int pill_x = (width() - pill_w) / 2;
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->set(1.0);
        if (query_edit_) query_edit_->move(pill_x + 46, 0);
    } else {
        open_anim_->set(0.0);
        if (query_edit_) query_edit_->move(pill_x + 46, 10);
        open_anim_->retarget(1.0);
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
    const int hit_w = std::max(280, width() - 24);
    for (int i = 0; i < hits_.size(); ++i) {
        hits_[i].rect = QRect(12, 59 + 12 + i * 41, hit_w, 38);
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
    const qreal w = std::max<qreal>(320.0, width());
    const qreal pill_w = std::min<qreal>(300.0, w - 24.0);
    const qreal pill_x = (w - pill_w) * 0.5;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    const QColor glass = shell_glass_fill(t);
    const QColor dim = t.dim;
    const QColor ink = t.ink;

    // Top search pill
    p.setPen(QPen(t.hair, 1.0));
    p.setBrush(glass);
    p.drawRoundedRect(QRectF(pill_x + 0.5, 0.5, pill_w - 1.0, 43.0), 21.5,
                      21.5);
    paintMonochromeIcon(p, QRectF(pill_x + 8.0, 8.0, 28.0, 28.0),
                        QStringLiteral("search-01"), ink, dpr);

    // Bottom recommendations / results card
    p.setPen(QPen(t.hair, 1.0));
    p.setBrush(glass);
    p.drawRoundedRect(QRectF(0.5, 59.5, w - 1.0, 227.0), finn::ui::radii::r_lg,
                      finn::ui::radii::r_lg);

    if (query_edit_->text().trimmed().isEmpty()) {
        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(15.0, 59.0 + 12.0, w - 30.0, 18.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Recommendations"));

        constexpr qreal col_x[5] = {15.0, 87.0, 159.0, 231.0, 303.0};
        const QFontMetricsF fm14(popoverFont(14));
        for (int i = 0; i < empty_app_hits_.size() && i < 5; ++i) {
            const auto& item = empty_app_hits_[i];
            if (col_x[i] + 32.0 > w - 8.0) continue;
            if (hovered_empty_app_ == i) {
                paintHoverFill(p, QRectF(col_x[i] - 10.0, 96.0, 52.0, 62.0),
                               finn::ui::radii::r_md, t);
            }
            paintAppIconTile(p, QRectF(col_x[i], 59.0 + 41.0, 32.0, 32.0),
                             finn::ui::radii::r_md, item.icon, dpr);
            p.setFont(popoverFont(14));
            p.setPen(ink);
            const qreal center_x = col_x[i] + 16.0;
            const qreal box_w =
                std::min<qreal>(66.0, 2.0 * std::min(center_x, w - center_x));
            const QString label = fm14.elidedText(item.title, Qt::ElideRight, box_w);
            p.drawText(QRectF(center_x - box_w * 0.5, 59.0 + 41.0 + 37.0, box_w, 18.0),
                       Qt::AlignHCenter | Qt::AlignTop, label);
        }

        p.setFont(popoverFont(14));
        p.setPen(dim);
        p.drawText(QRectF(15.0, 59.0 + 112.0, w - 30.0, 18.0),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Recently asked"));

        const QColor badge_bg = shell_hover_fill(t);
        for (int i = 0; i < empty_query_hits_.size(); ++i) {
            const auto& qh = empty_query_hits_[i];
            const QRectF r(qh.rect);
            if (r.right() > w - 4.0) continue;
            if (hovered_empty_query_ == i) {
                paintHoverFill(p, r.adjusted(-4.0, -3.0, 4.0, 3.0), 7.0, t);
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
        p.drawText(QRectF(20.0, 132.0, w - 40.0, 22.0), Qt::AlignCenter,
                   QStringLiteral("No matches found"));
        p.setFont(popoverFont(12));
        p.setPen(dim);
        p.drawText(QRectF(20.0, 158.0, w - 40.0, 20.0), Qt::AlignCenter,
                   QStringLiteral("Try an app name, file, or math like 128*4."));
        return;
    }

    const QFontMetricsF fm13(popoverFont(13));
    const QFontMetricsF fm11(popoverFont(11));
    for (int i = 0; i < hits_.size(); ++i) {
        const auto& hit = hits_[i];
        const QRectF r(hit.rect);
        if (i == selected_idx_ || i == hovered_hit_) {
            paintHoverFill(p, r, finn::ui::radii::r_md, t);
        }
        if (i == selected_idx_) {
            draw_focus_ring(p, r, finn::ui::radii::r_md, t);
        }
        if (hit.is_app_icon) {
            paintAppIconTile(p, QRectF(r.x() + 8.0, r.y() + 7.0, 24.0, 24.0),
                             finn::ui::radii::r_sm, hit.icon, dpr);
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
    setKeyboardMode(KeyboardMode::OnDemand);
    setExclusiveZone(-1);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Control Center"));

    constexpr int kMinBrightness = 5;
    constexpr int kMinVolume = 0;

    brightness_slider_ = new finn::ui::FinnSlider(kMinBrightness, 100, kMinBrightness, this);
    brightness_slider_->setPillStyle(true, QStringLiteral("sun-01"), 6, 24);
    brightness_slider_->setAccessibleName(QStringLiteral("Display Brightness"));

    volume_slider_ = new finn::ui::FinnSlider(kMinVolume, 100, kMinVolume, this);
    volume_slider_->setPillStyle(true, QStringLiteral("volume-02"), 9, 24);
    volume_slider_->setAccessibleName(QStringLiteral("Audio Volume"));

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

    open_anim_ = std::make_unique<ShellSpringDriver>(
        this, finn::ui::springs::kSnappy, 1.0, [this](double val) {
            open_progress_ = std::clamp(val, 0.0, 1.0);
            const int dy =
                static_cast<int>(std::lround((1.0 - open_progress_) * 10.0));
            if (brightness_slider_) brightness_slider_->move(6, 110 + dy);
            if (volume_slider_) volume_slider_->move(6, 156 + dy);
            update();
        });

    applyResponsiveLayout();
    syncSystemState();
}

void ControlCenterPopover::applyResponsiveLayout() {
    const FormFactor ff = current_form_factor();
    const QSize screen = primary_screen_size();
    int w = 444;
    if (ff == FormFactor::Phone) {
        w = std::clamp(screen.width() - 16, 320, 420);
        setAnchors(AnchorTop);
        setLayerMargins(QMargins(8, 44, 8, 0));
    } else if (ff == FormFactor::Tablet) {
        w = std::min(444, std::max(340, screen.width() - 24));
        setAnchors(AnchorBottom | AnchorRight);
        setLayerMargins(QMargins(0, 0, 12, 62));
    } else {
        // Figma 11:19: the panel's right edge lines up with the dock group's
        // right edge (254 + 130 + 130 + 154 + 3x15 = 713), 74px above bottom.
        constexpr int kDockGroupWidth = 254 + 130 + 130 + 154 + 3 * 15;
        const int right_margin = std::max(0, (screen.width() - kDockGroupWidth) / 2);
        setAnchors(AnchorBottom | AnchorRight);
        setLayerMargins(QMargins(0, 0, right_margin, 74));
    }
    setDesiredSize(QSize(w, 254));
    setFixedSize(w, 254);
    if (brightness_slider_) brightness_slider_->setGeometry(6, 110, w - 12, 36);
    if (volume_slider_) volume_slider_->setGeometry(6, 156, w - 12, 36);
}

void ControlCenterPopover::openDrawer() {
    finn::sys::FinnDesktopServices::instance().refreshAll();
    applyResponsiveLayout();
    hovered_ = ControlTarget::None;
    focused_target_idx_ = -1;
    syncSystemState();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->set(1.0);
        if (brightness_slider_) brightness_slider_->move(6, 110);
        if (volume_slider_) volume_slider_->move(6, 156);
    } else {
        open_anim_->set(0.0);
        if (brightness_slider_) brightness_slider_->move(6, 120);
        if (volume_slider_) volume_slider_->move(6, 166);
        open_anim_->retarget(1.0);
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
    const int w = std::max(320, width());
    const int half_w = (w - 22) / 2;
    const int col2_x = 6 + half_w + 10;
    const int media_btn_x = col2_x + half_w - 104;
    if (QRect(6, 6, half_w, 42).contains(pt)) return ControlTarget::Wifi;
    if (QRect(6, 58, half_w, 42).contains(pt)) return ControlTarget::Bluetooth;
    if (QRect(media_btn_x, 70, 26, 26).contains(pt)) return ControlTarget::MediaPrev;
    if (QRect(media_btn_x + 33, 70, 26, 26).contains(pt)) {
        return ControlTarget::MediaPlayPause;
    }
    if (QRect(media_btn_x + 66, 70, 26, 26).contains(pt)) {
        return ControlTarget::MediaNext;
    }
    if (QRect(w - 80, 212, 34, 34).contains(pt)) return ControlTarget::ThemeToggle;
    if (QRect(w - 43, 212, 34, 34).contains(pt)) return ControlTarget::OpenSettings;
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

void ControlCenterPopover::activateTarget(ControlTarget target) {
    auto& svc = finn::sys::FinnDesktopServices::instance();
    switch (target) {
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
            return;
    }
}

void ControlCenterPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const ControlTarget target = targetAt(event->position());
        if (target != ControlTarget::None) {
            activateTarget(target);
            return;
        }
    }
    FinnLayerWindow::mousePressEvent(event);
}

void ControlCenterPopover::paintEvent(QPaintEvent*) {
    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    const auto& svc = finn::sys::FinnDesktopServices::instance();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const qreal w = std::max<qreal>(320.0, width());
    const qreal half_w = (w - 22.0) * 0.5;
    const qreal col2_x = 6.0 + half_w + 10.0;
    const qreal media_btn_x = col2_x + half_w - 104.0;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(std::clamp(open_progress_, 0.0, 1.0));
    p.translate(0.0, (1.0 - open_progress_) * 10.0);

    p.setPen(QPen(t.hair, 1.0));
    p.setBrush(shell_glass_fill(t));
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, 299.0), finn::ui::radii::r_lg,
                      finn::ui::radii::r_lg);

    const QColor ink = t.ink;
    const QColor dim = t.dim;
    const QColor sub_fill = shell_subcard_fill(t);
    const QColor active_blue = t.accent;
    const QColor inactive_circle = t.track;
    const QColor white_fg = token_color(finn::ui::colors::tile_light);

    // Wi-Fi card
    const auto& net = svc.network();
    const bool wifi_on = net.wifi_hardware && net.wifi_enabled;
    const QRectF wifi_card(6.0, 6.0, half_w, 42.0);
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(wifi_card, finn::ui::radii::r_sm, finn::ui::radii::r_sm);
    if (hovered_ == ControlTarget::Wifi && net.wifi_hardware) {
        paintHoverFill(p, wifi_card, finn::ui::radii::r_sm, t);
    }
    p.setBrush(wifi_on ? active_blue : inactive_circle);
    p.drawEllipse(QRectF(11.0, 11.0, 32.0, 32.0));
    paintMonochromeIcon(p, QRectF(18.0, 18.0, 18.0, 18.0),
                        QStringLiteral("wifi-02"), white_fg, dpr);

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
    const qreal card_text_w = std::max<qreal>(64.0, half_w - 53.0);
    p.setFont(popoverFont(14));
    p.setPen(ink);
    p.drawText(QRectF(51.0, 8.0, card_text_w, 18.0),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Wi-Fi"));
    p.setFont(popoverFont(12));
    p.setPen(dim);
    p.drawText(QRectF(51.0, 27.0, card_text_w, 15.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(wifi_sub, Qt::ElideRight, card_text_w));

    // Bluetooth card
    const auto& bt = svc.bluetooth();
    const bool bt_on = bt.available && bt.powered;
    const QRectF bt_card(6.0, 58.0, half_w, 42.0);
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(bt_card, finn::ui::radii::r_sm, finn::ui::radii::r_sm);
    if (hovered_ == ControlTarget::Bluetooth && bt.available) {
        paintHoverFill(p, bt_card, finn::ui::radii::r_sm, t);
    }
    p.setBrush(bt_on ? active_blue : inactive_circle);
    p.drawEllipse(QRectF(11.0, 63.0, 32.0, 32.0));
    paintMonochromeIcon(p, QRectF(18.0, 70.0, 18.0, 18.0),
                        bt_on ? QStringLiteral("bluetooth")
                              : QStringLiteral("bluetooth-off"),
                        white_fg, dpr);

    const QString bt_sub =
        bt.available ? (bt.powered ? QStringLiteral("On") : QStringLiteral("Off"))
                     : QStringLiteral("Unavailable");
    p.setFont(popoverFont(14));
    p.setPen(ink);
    p.drawText(QRectF(51.0, 60.0, card_text_w, 18.0),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Bluetooth"));
    p.setFont(popoverFont(12));
    p.setPen(dim);
    p.drawText(QRectF(51.0, 79.0, card_text_w, 15.0),
               Qt::AlignVCenter | Qt::AlignLeft, bt_sub);

    // Media Control card
    const auto& media = svc.media();
    p.setPen(Qt::NoPen);
    p.setBrush(sub_fill);
    p.drawRoundedRect(QRectF(col2_x, 6.0, half_w, 94.0), finn::ui::radii::r_sm,
                      finn::ui::radii::r_sm);

    // Album art square (shrunk slightly on narrow phone widths to preserve text space)
    const qreal art_sz = (half_w < 190.0) ? 56.0 : 82.0;
    const qreal art_y = (half_w < 190.0) ? 14.0 : 12.0;
    p.setBrush(shell_hover_fill(t));
    p.drawRoundedRect(QRectF(col2_x + 6.0, art_y, art_sz, art_sz),
                      finn::ui::radii::r_xs, finn::ui::radii::r_xs);

    const qreal meta_x = col2_x + 6.0 + art_sz + 10.0;
    const qreal meta_w = std::max<qreal>(56.0, col2_x + half_w - meta_x - 8.0);
    const QString song = media.active ? media.title : QStringLiteral("No media");
    const QString artist =
        media.active
            ? (media.artist.isEmpty() ? media.player_identity : media.artist)
            : QStringLiteral("Idle");

    p.setFont(popoverFont(12));
    p.setPen(media.active ? ink : dim);
    p.drawText(QRectF(meta_x, 14.0, meta_w, 15.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(song, Qt::ElideRight, meta_w));
    p.setPen(dim);
    p.drawText(QRectF(meta_x, 30.0, meta_w, 15.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               fm12.elidedText(artist, Qt::ElideRight, meta_w));

    // Progress bar + status
    p.setPen(Qt::NoPen);
    p.setBrush(with_alpha(t.ink, 40));
    p.drawRoundedRect(QRectF(meta_x, 50.0, meta_w, 3.0), 1.5, 1.5);
    if (media.active && media.playing) {
        p.setBrush(with_alpha(t.ink, 175));
        p.drawRoundedRect(QRectF(meta_x, 50.0, meta_w * 0.36, 3.0), 1.5, 1.5);
    }

    p.setFont(popoverFont(10));
    p.setPen(dim);
    p.drawText(QRectF(meta_x, 55.0, meta_w, 13.0),
               Qt::AlignVCenter | Qt::AlignLeft,
               media.active ? (media.playing ? QStringLiteral("Playing")
                                             : QStringLiteral("Paused"))
                            : QStringLiteral("--:--"));

    const QColor media_tint = media.active ? ink : with_alpha(ink, 90);
    if (media.active && hovered_ == ControlTarget::MediaPrev) {
        paintHoverFill(p, QRectF(media_btn_x, 70.0, 26.0, 26.0),
                       finn::ui::radii::r_sm, t);
    }
    if (media.active && hovered_ == ControlTarget::MediaPlayPause) {
        paintHoverFill(p, QRectF(media_btn_x + 33.0, 70.0, 26.0, 26.0),
                       finn::ui::radii::r_sm, t);
    }
    if (media.active && hovered_ == ControlTarget::MediaNext) {
        paintHoverFill(p, QRectF(media_btn_x + 66.0, 70.0, 26.0, 26.0),
                       finn::ui::radii::r_sm, t);
    }

    paintMonochromeIcon(p, QRectF(media_btn_x + 5.0, 75.0, 16.0, 16.0),
                        QStringLiteral("backward"), media_tint, dpr);
    if (!media.active || media.playing) {
        paintMonochromeIcon(p, QRectF(media_btn_x + 38.0, 75.0, 16.0, 16.0),
                            QStringLiteral("pause"), media_tint, dpr);
    } else {
        QPainterPath play_tri;
        play_tri.moveTo(media_btn_x + 42.0, 76.5);
        play_tri.lineTo(media_btn_x + 52.0, 83.0);
        play_tri.lineTo(media_btn_x + 42.0, 89.5);
        play_tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(media_tint);
        p.drawPath(play_tri);
    }
    paintMonochromeIcon(p, QRectF(media_btn_x + 71.0, 75.0, 16.0, 16.0),
                        QStringLiteral("forward"), media_tint, dpr);

    // Bottom bar
    paintGlassFooter(p, w, 204.0, 50.0, finn::ui::radii::r_lg, t);

    if (hovered_ == ControlTarget::ThemeToggle) {
        paintHoverFill(p, QRectF(w - 80.0, 212.0, 34.0, 34.0),
                       finn::ui::radii::r_md, t);
    }
    paintMonochromeIcon(p, QRectF(w - 74.0, 218.0, 22.0, 22.0),
                        QStringLiteral("edit-01"), ink, dpr);

    if (hovered_ == ControlTarget::OpenSettings) {
        paintHoverFill(p, QRectF(w - 43.0, 212.0, 34.0, 34.0),
                       finn::ui::radii::r_md, t);
    }
    paintMonochromeIcon(p, QRectF(w - 37.0, 218.0, 22.0, 22.0),
                        QStringLiteral("settings-02"), ink, dpr);

    const QRectF focus_rects[7] = {
        wifi_card,
        bt_card,
        QRectF(media_btn_x, 70.0, 26.0, 26.0),
        QRectF(media_btn_x + 33.0, 70.0, 26.0, 26.0),
        QRectF(media_btn_x + 66.0, 70.0, 26.0, 26.0),
        QRectF(w - 80.0, 212.0, 34.0, 34.0),
        QRectF(w - 43.0, 212.0, 34.0, 34.0),
    };
    if (focused_target_idx_ >= 0 && focused_target_idx_ < 7) {
        draw_focus_ring(p, focus_rects[focused_target_idx_],
                        finn::ui::radii::r_sm, t);
    }
}

void ControlCenterPopover::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    constexpr ControlTarget kOrder[7] = {
        ControlTarget::Wifi,         ControlTarget::Bluetooth,
        ControlTarget::MediaPrev,    ControlTarget::MediaPlayPause,
        ControlTarget::MediaNext,    ControlTarget::ThemeToggle,
        ControlTarget::OpenSettings,
    };
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Down) {
        focused_target_idx_ = (focused_target_idx_ + 1) % 7;
        updateHoverTooltip(kOrder[focused_target_idx_]);
        update();
        return;
    }
    if (event->key() == Qt::Key_Backtab || event->key() == Qt::Key_Left ||
        event->key() == Qt::Key_Up) {
        focused_target_idx_ =
            (focused_target_idx_ <= 0) ? 6 : (focused_target_idx_ - 1);
        updateHoverTooltip(kOrder[focused_target_idx_]);
        update();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
         event->key() == Qt::Key_Space) &&
        focused_target_idx_ >= 0 && focused_target_idx_ < 7) {
        activateTarget(kOrder[focused_target_idx_]);
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
    setKeyboardMode(KeyboardMode::OnDemand);
    setExclusiveZone(-1);
    setAccessibleName(QStringLiteral("Open Windows"));

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

    open_anim_ = std::make_unique<ShellSpringDriver>(
        this, finn::ui::springs::kSnappy, 1.0, [this](double val) {
            open_progress_ = std::clamp(val, 0.0, 1.0);
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
    applyResponsiveLayout();
}

void OpenWindowsPopover::applyResponsiveLayout() {
    const FormFactor ff = current_form_factor();
    const QSize screen = primary_screen_size();
    if (ff == FormFactor::Phone) {
        const int w = std::clamp(screen.width() - 16, 320, 420);
        const int h = std::min(360, std::max(240, screen.height() - 120));
        setAnchors(AnchorTop);
        setLayerMargins(QMargins(8, 44, 8, 0));
        setDesiredSize(QSize(w, h));
    } else if (ff == FormFactor::Tablet) {
        const int w = std::min(640, std::max(480, screen.width() - 24));
        setAnchors(AnchorBottom);
        setLayerMargins(QMargins(0, 0, 0, 62));
        setDesiredSize(QSize(w, 340));
    } else {
        setAnchors(AnchorBottom);
        setLayerMargins(QMargins(0, 0, 0, 74));
        setDesiredSize(QSize(640, 340));
    }
}

void OpenWindowsPopover::openOverview() {
    applyResponsiveLayout();
    rebuildCards();
    if (finn::ui::FinnThemeManager::instance().reducedMotion()) {
        open_anim_->set(1.0);
        if (outer_layout_) outer_layout_->setContentsMargins(0, 0, 0, 0);
        if (panel_) panel_->setOpacityAlpha(244);
    } else {
        open_anim_->set(0.0);
        if (outer_layout_) outer_layout_->setContentsMargins(0, 10, 0, 0);
        if (panel_) panel_->setOpacityAlpha(0);
        open_anim_->retarget(1.0);
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

    const int max_cols = (current_form_factor() == FormFactor::Phone) ? 1 : 2;
    int row = 0;
    int col = 0;
    for (const auto& w : windows_->windows()) {
        auto* card = new finn::ui::FinnCard(finn::ui::kRadiusLg, true, grid_host_);
        card->setSelected(w.activated);
        card->setAccessibleName(w.title.isEmpty() ? w.app_id : w.title);
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

        const auto& tok = finn::ui::FinnThemeManager::instance().tokens();
        auto* close_btn =
            new finn::ui::FinnIconButton(QStringLiteral("x"), 24, 11, card);
        close_btn->setAccessibleName(
            QStringLiteral("Close %1")
                .arg(w.title.isEmpty() ? w.app_id : w.title));
        close_btn->setCustomHoverColor(
            shell_danger_hover(tok),
            token_color(finn::ui::colors::tile_light));
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
        if (++col >= max_cols) {
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
    setAccessibleName(QStringLiteral("Lock Screen"));

    const FormFactor ff = current_form_factor();
    auto* root = new QVBoxLayout(this);
    if (ff == FormFactor::Phone) {
        root->setContentsMargins(16, 40, 16, 20);
    } else {
        root->setContentsMargins(40, 64, 40, 32);
    }
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
    password_edit_->setAccessibleName(QStringLiteral("Password"));
    connect(password_edit_, &QLineEdit::returnPressed, this,
            &LockScreenWindow::attemptUnlock);

    auto* unlock_btn = new finn::ui::FinnIconButton(
        QStringLiteral("chevron-right"), 26, 14, pw_pill);
    unlock_btn->setCornerRadius(13.0);
    unlock_btn->setAccessibleName(QStringLiteral("Unlock"));
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

    auto* power_pill = new QFrame(this);
    power_pill->setObjectName(QStringLiteral("lockPowerPill"));
    auto* pill_lay = new QHBoxLayout(power_pill);
    pill_lay->setContentsMargins(6, 4, 6, 4);
    pill_lay->setSpacing(4);
    restart_btn_ = new finn::ui::FinnIconButton(
        QStringLiteral("refresh"), 36, 18, power_pill);
    restart_btn_->setCornerRadius(18.0);
    restart_btn_->setAccessibleName(QStringLiteral("Restart"));
    restart_btn_->setToolTip(QStringLiteral("Restart"));
    connect(restart_btn_, &QAbstractButton::clicked, this, []() {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    });
    power_btn_ = new finn::ui::FinnIconButton(
        QStringLiteral("power"), 36, 18, power_pill);
    power_btn_->setCornerRadius(18.0);
    power_btn_->setAccessibleName(QStringLiteral("Shut Down"));
    power_btn_->setToolTip(QStringLiteral("Shut Down"));
    connect(power_btn_, &QAbstractButton::clicked, this, []() {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
    });
    pill_lay->addWidget(restart_btn_);
    pill_lay->addWidget(power_btn_);
    bottom->addWidget(power_pill);
    root->addLayout(bottom);

    shake_anim_ = std::make_unique<ShellSpringDriver>(
        this, finn::ui::springs::kBouncy, 0.0, [this](double val) {
            if (!auth_box_) return;
            const qreal next_dx = std::clamp(val, -14.0, 14.0);
            const int step_dx = static_cast<int>(std::lround(next_dx)) -
                                static_cast<int>(std::lround(shake_offset_));
            shake_offset_ = next_dx;
            if (step_dx != 0) {
                auth_box_->move(auth_box_->x() + step_dx, auth_box_->y());
            }
        });

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

LockScreenWindow::~LockScreenWindow() {
    if (auth_proc_) {
        auth_proc_->kill();
        auth_proc_->deleteLater();
        auth_proc_ = nullptr;
    }
    if (switch_proc_) {
        switch_proc_->kill();
        switch_proc_->deleteLater();
        switch_proc_ = nullptr;
    }
}

void LockScreenWindow::refreshTheme() {
    const FormFactor ff = current_form_factor();
    date_lbl_->setFont(finn::ui::finn_font(15.0, QFont::Medium));
    time_lbl_->setFont(
        finn::ui::finn_font(ff == FormFactor::Phone ? 44.0 : 58.0, QFont::Medium));
    user_name_lbl_->setFont(finn::ui::finn_font(13.5, QFont::DemiBold));

    // Adapt each lock-screen region to the wallpaper luminance behind it.
    auto apply_fg = [](QWidget* w, const QColor& c) {
        if (w) {
            w->setStyleSheet(
                QStringLiteral("color: %1; background: transparent;")
                    .arg(c.name()));
        }
    };
    const QColor clock_fg = token_color(
        clock_dark_bg_ ? finn::ui::colors::ink_dark : finn::ui::colors::ink_light);
    const QColor auth_fg = token_color(
        auth_dark_bg_ ? finn::ui::colors::ink_dark : finn::ui::colors::ink_light);

    apply_fg(date_lbl_, clock_fg);
    apply_fg(time_lbl_, clock_fg);
    apply_fg(user_name_lbl_, auth_fg);

    const auto& t = finn::ui::FinnThemeManager::instance().tokens();
    if (auto* pill = findChild<QFrame*>(QStringLiteral("lockPowerPill"))) {
        QColor bg = t.glass;
        bg.setAlpha(205);
        pill->setStyleSheet(
            QStringLiteral("#lockPowerPill { background: %1; border: 1px solid %2; border-radius: 22px; }")
                .arg(bg.name(QColor::HexArgb), t.hair.name(QColor::HexArgb)));
    }
    if (restart_btn_) {
        restart_btn_->setIconTint(t.ink);
        restart_btn_->setCustomHoverColor(t.surface_hover, t.ink);
    }
    if (power_btn_) {
        power_btn_->setIconTint(t.ink);
        power_btn_->setCustomHoverColor(t.surface_hover, t.ink);
    }
    if (avatar_lbl_) {
        avatar_lbl_->setPixmap(finn::ui::FinnThemeManager::instance().avatarPixmap(
            QSize(64, 64), devicePixelRatioF()));
    }
    configureBorderlessEdit(password_edit_, 14,
                            finn::ui::FinnThemeManager::instance().tokens());
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
            chip->setAccessibleName(
                QStringLiteral("Switch to user %1").arg(u.display_name));
            connect(chip, &QAbstractButton::clicked, this,
                    [this, uname = u.username]() { selectUser(uname); });
            users_row_->addWidget(chip);
        }
    }
    selectUser(cur);
}

void LockScreenWindow::selectUser(const QString& username) {
    if (verifying_) return;
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
    if (!auth_box_ || !shake_anim_ ||
        finn::ui::FinnThemeManager::instance().reducedMotion()) {
        return;
    }
    shake_anim_->impulse(220.0, 0.0);
}

void LockScreenWindow::setVerifying(bool verifying) {
    verifying_ = verifying;
    if (password_edit_) {
        password_edit_->setEnabled(!verifying);
        password_edit_->setPlaceholderText(
            verifying ? QStringLiteral("Verifying...")
                      : QStringLiteral("Enter Password"));
    }
    setCursor(verifying ? Qt::WaitCursor : Qt::ArrowCursor);
}

void LockScreenWindow::finishUnlockSuccess() {
    setVerifying(false);
    if (error_lbl_) error_lbl_->setVisible(false);
    if (password_edit_) password_edit_->clear();
    hide();
}

void LockScreenWindow::finishUnlockFailure(const QString& reason) {
    setVerifying(false);
    if (error_lbl_) {
        error_lbl_->setText(reason);
        error_lbl_->setVisible(true);
    }
    if (password_edit_) {
        password_edit_->clear();
        password_edit_->setFocus();
    }
    shakeInput();
}

void LockScreenWindow::attemptUnlock() {
    if (verifying_) return;
    const QString pw = password_edit_ ? password_edit_->text() : QString();
    const QString current_user = qEnvironmentVariable("USER", QStringLiteral("finn"));
    const QString target_user =
        selected_user_.isEmpty() ? current_user : selected_user_;

    setVerifying(true);
    if (error_lbl_) error_lbl_->setVisible(false);

    if (!pw.isEmpty()) {
        startSuCheck(target_user, pw);
    } else {
        startPasswordlessCheck(target_user);
    }
}

void LockScreenWindow::startSuCheck(const QString& target_user,
                                    const QString& password) {
    if (auth_proc_) {
        auth_proc_->kill();
        auth_proc_->deleteLater();
        auth_proc_ = nullptr;
    }

    auto* proc = new QProcess(this);
    auth_proc_ = proc;
    const QString current_user = qEnvironmentVariable("USER", QStringLiteral("finn"));
    const bool switching = (target_user != current_user);

    connect(proc, &QProcess::finished, this,
            [this, proc, target_user, switching, has_pw = !password.isEmpty()](
                int exit_code, QProcess::ExitStatus exit_status) {
                if (auth_proc_ == proc) auth_proc_ = nullptr;
                proc->deleteLater();
                if (exit_status == QProcess::NormalExit && exit_code == 0) {
                    if (switching) {
                        switchUserSession(target_user);
                    } else {
                        finishUnlockSuccess();
                    }
                    return;
                }
                finishUnlockFailure(has_pw ? QStringLiteral("Incorrect password")
                                           : QStringLiteral("Password required"));
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError) {
                if (auth_proc_ == proc) auth_proc_ = nullptr;
                proc->deleteLater();
                finishUnlockFailure(QStringLiteral("Authentication service unavailable"));
            });

    QPointer<QProcess> guard(proc);
    QTimer::singleShot(5000, this, [this, guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
            guard->kill();
            if (auth_proc_ == guard) auth_proc_ = nullptr;
            guard->deleteLater();
            finishUnlockFailure(QStringLiteral("Authentication timed out"));
        }
    });

    proc->start(QStringLiteral("su"),
                {QStringLiteral("-c"), QStringLiteral("true"), target_user});
    proc->write(password.toUtf8() + '\n');
    proc->closeWriteChannel();
}

void LockScreenWindow::startPasswordlessCheck(const QString& target_user) {
    if (auth_proc_) {
        auth_proc_->kill();
        auth_proc_->deleteLater();
        auth_proc_ = nullptr;
    }

    auto* proc = new QProcess(this);
    auth_proc_ = proc;
    const QString current_user = qEnvironmentVariable("USER", QStringLiteral("finn"));
    const bool switching = (target_user != current_user);

    connect(proc, &QProcess::finished, this,
            [this, proc, target_user, switching](
                int exit_code, QProcess::ExitStatus exit_status) {
                if (auth_proc_ == proc) auth_proc_ = nullptr;
                const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();

                if (exit_status == QProcess::NormalExit && exit_code == 0) {
                    const QStringList fields = out.split(
                        QRegularExpression(QStringLiteral("\\s+")),
                        Qt::SkipEmptyParts);
                    const QString status =
                        (fields.size() >= 2) ? fields[1] : QString();
                    // Only explicit NP (no password) allows empty-password unlock;
                    // locked accounts ("L") must never unlock cleanly.
                    if (status == QStringLiteral("NP")) {
                        if (switching) {
                            switchUserSession(target_user);
                        } else {
                            finishUnlockSuccess();
                        }
                        return;
                    }
                    if (status == QStringLiteral("L")) {
                        finishUnlockFailure(QStringLiteral("Account is locked"));
                        return;
                    }
                    finishUnlockFailure(QStringLiteral("Password required"));
                    return;
                }

                // passwd -S may not be readable by unprivileged users: allow empty
                // unlock for the active user only if PAM itself accepts it via su.
                if (!switching) {
                    startSuCheck(target_user, QString());
                } else {
                    finishUnlockFailure(QStringLiteral("Password required"));
                }
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, target_user, switching](QProcess::ProcessError) {
                if (auth_proc_ == proc) auth_proc_ = nullptr;
                proc->deleteLater();
                if (!switching) {
                    startSuCheck(target_user, QString());
                } else {
                    finishUnlockFailure(QStringLiteral("Password required"));
                }
            });

    QPointer<QProcess> guard(proc);
    QTimer::singleShot(2000, this, [this, guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
            guard->kill();
            if (auth_proc_ == guard) auth_proc_ = nullptr;
            guard->deleteLater();
            finishUnlockFailure(QStringLiteral("Password check timed out"));
        }
    });

    proc->start(QStringLiteral("passwd"),
                {QStringLiteral("-S"), target_user});
}

void LockScreenWindow::switchUserSession(const QString& username) {
    if (switch_proc_) {
        switch_proc_->kill();
        switch_proc_->deleteLater();
        switch_proc_ = nullptr;
    }

    auto* list_proc = new QProcess(this);
    switch_proc_ = list_proc;

    connect(list_proc, &QProcess::finished, this,
            [this, list_proc, username](int exit_code,
                                        QProcess::ExitStatus exit_status) {
                if (switch_proc_ == list_proc) switch_proc_ = nullptr;
                const QString out =
                    QString::fromUtf8(list_proc->readAllStandardOutput());
                list_proc->deleteLater();

                if (exit_status != QProcess::NormalExit || exit_code != 0) {
                    finishUnlockFailure(
                        QStringLiteral("Could not query active sessions"));
                    return;
                }

                QString target_session;
                const QStringList lines =
                    out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& line : lines) {
                    const QStringList cols = line.trimmed().split(
                        QRegularExpression(QStringLiteral("\\s+")),
                        Qt::SkipEmptyParts);
                    if (cols.size() >= 3 && cols[2] == username) {
                        target_session = cols[0];
                        break;
                    }
                }
                if (target_session.isEmpty()) {
                    finishUnlockFailure(
                        QStringLiteral("No active session for %1").arg(username));
                    return;
                }

                auto* act_proc = new QProcess(this);
                switch_proc_ = act_proc;
                connect(act_proc, &QProcess::finished, this,
                        [this, act_proc](int act_code,
                                         QProcess::ExitStatus act_status) {
                            if (switch_proc_ == act_proc) switch_proc_ = nullptr;
                            act_proc->deleteLater();
                            if (act_status == QProcess::NormalExit &&
                                act_code == 0) {
                                finishUnlockSuccess();
                            } else {
                                finishUnlockFailure(
                                    QStringLiteral("Failed to switch user session"));
                            }
                        });
                connect(act_proc, &QProcess::errorOccurred, this,
                        [this, act_proc](QProcess::ProcessError) {
                            if (switch_proc_ == act_proc) switch_proc_ = nullptr;
                            act_proc->deleteLater();
                            finishUnlockFailure(
                                QStringLiteral("Failed to activate session"));
                        });
                act_proc->start(QStringLiteral("loginctl"),
                                {QStringLiteral("activate"), target_session});
            });

    connect(list_proc, &QProcess::errorOccurred, this,
            [this, list_proc](QProcess::ProcessError) {
                if (switch_proc_ == list_proc) switch_proc_ = nullptr;
                list_proc->deleteLater();
                finishUnlockFailure(QStringLiteral("Session manager unavailable"));
            });

    QPointer<QProcess> guard(list_proc);
    QTimer::singleShot(3000, this, [this, guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
            guard->kill();
            if (switch_proc_ == guard) switch_proc_ = nullptr;
            guard->deleteLater();
            finishUnlockFailure(QStringLiteral("Session switch timed out"));
        }
    });

    list_proc->start(QStringLiteral("loginctl"),
                     {QStringLiteral("list-sessions"),
                      QStringLiteral("--no-legend")});
}

void LockScreenWindow::lockNow() {
    // Load the wallpaper on every lock, so a change made in Settings is
    // picked up without restarting the shell. An empty lock_wallpaper means
    // the desktop wallpaper; an empty desktop setting means the shipped one.
    const auto& manager = finn::ui::FinnThemeManager::instance();
    QString path = manager.lockWallpaperPath();
    if (path.isEmpty()) path = manager.wallpaperPath();
    if (path.isEmpty()) {
        path = QString::fromLatin1(finn::kDefaultWallpaperPath);
    }
    lock_wallpaper_.load(path);

    if (lock_wallpaper_.isNull()) {
        bg_is_dark_ = true;
        clock_dark_bg_ = true;
        auth_dark_bg_ = true;
        actions_dark_bg_ = true;
    } else {
        const QImage full = lock_wallpaper_.toImage().convertToFormat(
            QImage::Format_RGBA8888);
        const qreal dst_w = qMax(1, width());
        const qreal dst_h = qMax(1, height());
        const qreal src_ratio = qreal(full.width()) / qreal(full.height());
        const qreal dst_ratio = dst_w / dst_h;
        QRectF crop;
        if (src_ratio > dst_ratio) {
            const qreal w = qreal(full.height()) * dst_ratio;
            crop = QRectF((full.width() - w) / 2.0, 0, w, full.height());
        } else {
            const qreal h = qreal(full.width()) / dst_ratio;
            crop = QRectF(0, (full.height() - h) / 2.0, full.width(), h);
        }
        auto region_is_dark = [&](qreal nx, qreal ny, qreal nw, qreal nh) {
            const QRect r(
                qRound(crop.x() + crop.width() * nx),
                qRound(crop.y() + crop.height() * ny),
                qMax(1, qRound(crop.width() * nw)),
                qMax(1, qRound(crop.height() * nh)));
            const QImage patch = full.copy(r.intersected(full.rect())).scaled(
                12, 12, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            double luma = 0.0;
            const int total = patch.width() * patch.height();
            for (int y = 0; y < patch.height(); ++y) {
                const uchar* row = patch.constScanLine(y);
                for (int x = 0; x < patch.width(); ++x) {
                    luma += 0.299 * (row[x * 4 + 0] / 255.0) +
                            0.587 * (row[x * 4 + 1] / 255.0) +
                            0.114 * (row[x * 4 + 2] / 255.0);
                }
            }
            return total > 0 ? ((luma / total) < 0.55) : true;
        };
        bg_is_dark_ = region_is_dark(0.0, 0.0, 1.0, 1.0);
        clock_dark_bg_ = region_is_dark(0.32, 0.04, 0.36, 0.20);
        auth_dark_bg_ = region_is_dark(0.42, 0.72, 0.16, 0.16);
        actions_dark_bg_ = region_is_dark(0.86, 0.90, 0.12, 0.08);
    }
    refreshTheme();

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
    QPainter p(this);
    // Opaque: a lock screen must not reveal the desktop behind it.
    if (lock_wallpaper_.isNull()) {
        p.fillRect(rect(), token_color(finn::ui::colors::bone_dark));
    } else {
        // Cover-fit, so the image fills the screen without stretching.
        const QRect target = rect();
        const qreal src_ratio = qreal(lock_wallpaper_.width()) /
                                qreal(lock_wallpaper_.height());
        const qreal dst_ratio = qreal(target.width()) / qreal(target.height());
        QRectF src;
        if (src_ratio > dst_ratio) {
            const qreal w = qreal(lock_wallpaper_.height()) * dst_ratio;
            src = QRectF((lock_wallpaper_.width() - w) / 2.0, 0, w,
                         lock_wallpaper_.height());
        } else {
            const qreal h = qreal(lock_wallpaper_.width()) / dst_ratio;
            src = QRectF(0, (lock_wallpaper_.height() - h) / 2.0,
                         lock_wallpaper_.width(), h);
        }
        p.drawPixmap(target, lock_wallpaper_, src);
    }
    // Keep a subtle scrim so foreground text and icons stay legible.
    p.fillRect(rect(),
               with_alpha(token_color(bg_is_dark_ ? finn::ui::colors::bone_dark
                                                  : finn::ui::colors::bone_light),
                          bg_is_dark_ ? 48 : 20));
}

}  // namespace finn::shell
