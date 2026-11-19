// Finn Store: native software hub for FinnOS Native, Linux (pacman/Flatpak/AppImage),
// Windows (Wine/Proton), and Android (Waydroid) applications.
// Redesigned to match Figma Settings (20:64) 1:1 with ChromeMode::FigmaSplit,
// 250px FinnSidebar with 26x26 circular colored icon badges, FinnHeroCard spotlight,
// and 2-column Settings-style r=6 app cards.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "compat/android_runner.hpp"
#include "compat/desktop_entry.hpp"
#include "compat/wine_runner.hpp"
#include "session/app_registry.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

using namespace finn::ui;

QString rgba_css(const QColor& c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

void apply_header_16px_regular(FinnLabel* lbl) {
    if (!lbl) return;
    auto sync = [lbl]() {
        QFont f = finn_font(12.0, QFont::Normal);
        f.setPixelSize(16);
        lbl->setFont(f);
    };
    sync();
    QObject::connect(&FinnThemeManager::instance(),
                     &FinnThemeManager::themeChanged,
                     lbl, sync);
}

void apply_log_edit_style(QPlainTextEdit* edit, const FinnTokens& t) {
    if (!edit) return;
    edit->setFont(finn_mono_font(9.6, QFont::Normal));
    edit->setStyleSheet(QStringLiteral(
                            "QPlainTextEdit { background: %1; color: %2; "
                            "border: 1px solid %3; border-radius: 6px; padding: 10px; "
                            "selection-background-color: %4; selection-color: %2; }%5")
                            .arg(t.tile.name(), t.ink.name(),
                                 rgba_css(t.hair), rgba_css(t.tile_on),
                                 finn::ui::scrollbar_qss(t)));
}

class StoreIconWidget final : public QWidget {
public:
    StoreIconWidget(int box_size, int icon_size, bool framed = false,
                    bool dim_tint = false, QWidget* parent = nullptr)
        : QWidget(parent),
          icon_size_(icon_size),
          framed_(framed),
          dim_tint_(dim_tint) {
        setFixedSize(box_size, box_size);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setIconName(const QString& name) {
        if (icon_name_ == name) return;
        icon_name_ = name;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        if (framed_) {
            const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            p.setBrush(t.surface_canvas);
            p.setPen(QPen(t.hair, 1.0));
            p.drawRoundedRect(r, kRadiusMd, kRadiusMd);
        }

        if (icon_name_.isEmpty()) return;

        const QRect target((width() - icon_size_) / 2,
                           (height() - icon_size_) / 2,
                           icon_size_, icon_size_);
        const QColor tint = dim_tint_ ? t.dim : QColor();
        const qreal dpr = devicePixelRatioF();

        QPixmap pm = FinnThemeManager::instance().iconPixmap(
            icon_name_, QSize(icon_size_, icon_size_), tint, dpr);
        if (pm.isNull()) {
            const QIcon fb = QIcon::fromTheme(icon_name_);
            if (!fb.isNull()) {
                fb.paint(&p, target);
                return;
            }
            pm = FinnThemeManager::instance().iconPixmap(
                QStringLiteral("app-grid"), QSize(icon_size_, icon_size_),
                dim_tint_ ? t.dim : t.ink, dpr);
        }
        if (!pm.isNull()) {
            p.drawPixmap(target, pm);
        }
    }

private:
    QString icon_name_;
    int icon_size_ = 36;
    bool framed_ = false;
    bool dim_tint_ = false;
};

class ElidedFinnLabel final : public FinnLabel {
public:
    ElidedFinnLabel(const QString& text, Role role, Tone tone,
                    QWidget* parent = nullptr,
                    Qt::TextElideMode mode = Qt::ElideRight)
        : FinnLabel(text, role, tone, parent),
          full_text_(text),
          mode_(mode) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        if (!text.isEmpty()) setToolTip(text);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, [this](const FinnTokens&) { updateElided(); });
    }

    void setFullText(const QString& text) {
        full_text_ = text;
        setToolTip(text);
        updateElided();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        FinnLabel::resizeEvent(event);
        updateElided();
    }

private:
    void updateElided() {
        const QMargins m = contentsMargins();
        const int avail = width() - m.left() - m.right();
        if (avail <= 0) {
            setText(full_text_);
            return;
        }
        setText(fontMetrics().elidedText(full_text_, mode_, avail));
    }

    QString full_text_;
    Qt::TextElideMode mode_;
};

void clear_layout(QLayout* layout) {
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->deleteLater();
        } else if (auto* child = item->layout()) {
            clear_layout(child);
        }
        delete item;
    }
}

QString source_label(finn::AppSource src) {
    switch (src) {
        case finn::AppSource::kNative:  return QStringLiteral("FinnSDK");
        case finn::AppSource::kFlatpak: return QStringLiteral("Flatpak");
        case finn::AppSource::kDesktop: return QStringLiteral("Linux");
        case finn::AppSource::kWindows: return QStringLiteral("Wine");
        case finn::AppSource::kWinget:  return QStringLiteral("Windows");
        case finn::AppSource::kUnknown: return QStringLiteral("App");
    }
    return QStringLiteral("App");
}

QString slugify(const QString& text) {
    QString out = text.toLower().trimmed();
    out.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    out.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return out.isEmpty() ? QStringLiteral("my-app") : out;
}

class StoreWindow final : public FinnWindow {
    Q_OBJECT

public:
    StoreWindow()
        : FinnWindow(QStringLiteral("Finn Store"), QStringLiteral("store")) {
        setMinimumSize(800, 500);
        resize(940, 600);
        setChromeMode(ChromeMode::FigmaSplit);

        // Full 250px Figma Settings-style sidebar with 26x26 circular colored icon badges
        sidebar_ = new FinnSidebar(250, this);
        sidebar_->setHeaderTitle(QStringLiteral("Finn Store"));
        apply_header_16px_regular(sidebar_->headerLabel());
        sidebar_->setSearchEnabled(true, QStringLiteral("Search"));

        if (sidebar_->searchField()) {
            sidebar_->searchField()->disconnect(sidebar_);
            connect(sidebar_->searchField(), &FinnSearchField::textChanged,
                    this, [this](const QString& q) {
                        populateAppsGrid(q);
                        if (!q.trimmed().isEmpty() && stack_->currentIndex() != 1) {
                            last_list_page_ = QStringLiteral("all");
                            sidebar_->setCurrentId(QStringLiteral("all"));
                            page_title_->setText(QStringLiteral("All Apps"));
                            stack_->setCurrentIndex(1);
                        }
                    });
        }

        // The glyph says what the source is; colour is not a category here.
        sidebar_->addItem(QStringLiteral("discover"), QStringLiteral("compass"),
                          QStringLiteral("Discover"), nav_accent_light(), nav_accent_dark());
        sidebar_->addItem(QStringLiteral("all"), QStringLiteral("layers"),
                          QStringLiteral("All Apps"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addSectionSpacer(12);
        sidebar_->addItem(QStringLiteral("linux"), QStringLiteral("package"),
                          QStringLiteral("Linux & Flatpak"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addItem(QStringLiteral("windows"), QStringLiteral("window"),
                          QStringLiteral("Windows (Wine)"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addItem(QStringLiteral("android"), QStringLiteral("smartphone"),
                          QStringLiteral("Android"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addSectionSpacer(12);
        sidebar_->addItem(QStringLiteral("updates"), QStringLiteral("download"),
                          QStringLiteral("Updates"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addItem(QStringLiteral("sdk"), QStringLiteral("terminal-square"),
                          QStringLiteral("FinnSDK"), nav_neutral_light(), nav_neutral_dark());
        sidebar_->addStretch();
        setSidebarWidget(sidebar_);

        // Right content pane (12, 24, 12, 14 margins, with 96px top-right reserve for floating controls)
        right_host_ = new QWidget(this);
        auto* right_col = new QVBoxLayout(right_host_);
        right_col->setContentsMargins(12, 24, 12, 14);
        right_col->setSpacing(12);

        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(8);

        page_title_ = new FinnLabel(
            QStringLiteral("Discover"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, right_host_);
        apply_header_16px_regular(page_title_);
        header_row->addWidget(page_title_);
        header_row->addStretch(1);

        auto* refresh_btn = new FinnButton(
            QStringLiteral("Refresh"), FinnButton::Variant::Secondary,
            QStringLiteral("refresh"), right_host_);
        refresh_btn->setFixedHeight(28);
        refresh_btn->setAccessibleName(QStringLiteral("Refresh catalogue"));
        connect(refresh_btn, &QAbstractButton::clicked, this, [this]() {
            reloadCatalogue();
            showToast(QStringLiteral("Catalogue refreshed"));
        });
        header_row->addWidget(refresh_btn);
        right_col->addLayout(header_row);

        stack_ = new QStackedWidget(right_host_);
        finn::ui::install_stack_animator(stack_);
        stack_->addWidget(wrapScroll(buildDiscoverPage()));  // 0
        stack_->addWidget(wrapScroll(buildAppsPage()));      // 1
        stack_->addWidget(wrapScroll(buildLinuxPage()));     // 2
        stack_->addWidget(wrapScroll(buildWindowsPage()));   // 3
        stack_->addWidget(wrapScroll(buildAndroidPage()));   // 4
        stack_->addWidget(wrapScroll(buildUpdatesPage()));   // 5
        stack_->addWidget(wrapScroll(buildSdkPage()));       // 6
        stack_->addWidget(wrapScroll(buildDetailPage()));    // 7
        right_col->addWidget(stack_, 1);

        setContentWidget(right_host_);

        connect(sidebar_, &FinnSidebar::selected, this,
                [this](const QString& id) { switchPage(id); });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, [this](const FinnTokens&) { applyTheme(); });

        sidebar_->setCurrentId(QStringLiteral("discover"));
        applyTheme();
        reloadCatalogue();
    }

private:
    QString currentSearchQuery() const {
        return (sidebar_ && sidebar_->searchField())
                   ? sidebar_->searchField()->text()
                   : QString();
    }

    void applyTheme() {
        const auto& t = FinnThemeManager::instance().tokens();
        apply_log_edit_style(linux_log_, t);
        apply_log_edit_style(android_log_, t);
        apply_log_edit_style(updates_log_, t);
        apply_log_edit_style(finnapp_log_, t);
    }

    QWidget* wrapScroll(QWidget* inner) {
        auto* sc = new QScrollArea(stack_);
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sc->setAutoFillBackground(false);
        sc->viewport()->setAutoFillBackground(false);
        inner->setAutoFillBackground(false);
        sc->setWidget(inner);
        return sc;
    }

    void switchPage(const QString& id) {
        if (id == QLatin1String("discover")) {
            last_list_page_ = QStringLiteral("discover");
            page_title_->setText(QStringLiteral("Discover"));
            stack_->setCurrentIndex(0);
        } else if (id == QLatin1String("all")) {
            last_list_page_ = QStringLiteral("all");
            page_title_->setText(QStringLiteral("All Apps"));
            populateAppsGrid(currentSearchQuery());
            stack_->setCurrentIndex(1);
        } else if (id == QLatin1String("linux")) {
            page_title_->setText(QStringLiteral("Linux & Flatpak"));
            stack_->setCurrentIndex(2);
        } else if (id == QLatin1String("windows")) {
            page_title_->setText(QStringLiteral("Windows (Wine)"));
            refreshWindowsPage();
            stack_->setCurrentIndex(3);
        } else if (id == QLatin1String("android")) {
            page_title_->setText(QStringLiteral("Android"));
            refreshAndroidPage();
            stack_->setCurrentIndex(4);
        } else if (id == QLatin1String("updates")) {
            page_title_->setText(QStringLiteral("Installed & Updates"));
            refreshUpdatesList();
            stack_->setCurrentIndex(5);
        } else if (id == QLatin1String("sdk")) {
            page_title_->setText(QStringLiteral("FinnSDK Studio"));
            stack_->setCurrentIndex(6);
        }
    }

    void reloadCatalogue() {
        apps_ = finn::AppRegistry::load_all(finn::kCataloguePath, finn::kInstalledPath).apps();
        int installed_count = 0;
        for (const auto& a : apps_) {
            if (a.installed) ++installed_count;
        }
        if (installed_stat_card_) {
            installed_stat_card_->setValueParts(
                QStringLiteral("%1 Apps").arg(installed_count),
                QStringLiteral("Installed"));
        }
        populateSpotlight();
        populateAppsGrid(currentSearchQuery());
        refreshWindowsPage();
        refreshAndroidPage();
        refreshUpdatesList();
    }

    void launchOrInstallApp(const finn::App& app) {
        if (app.installed) {
            finn::SessionClient client;
            std::string err;
            client.launch(app.id, &err);
            showToast(err.empty()
                          ? QStringLiteral("Launching %1").arg(QString::fromStdString(app.name))
                          : QString::fromStdString(err));
        } else {
            openDetail(app);
        }
    }

    void openDetail(const finn::App& app) {
        selected_app_ = app;
        page_title_->setText(QString::fromStdString(app.name));
        if (back_btn_) {
            back_btn_->setText(last_list_page_ == QLatin1String("discover")
                                   ? QStringLiteral("Back to Discover")
                                   : QStringLiteral("Back to All Apps"));
        }
        detail_title_->setFullText(QString::fromStdString(app.name));
        detail_sub_->setText(
            QStringLiteral("%1  ·  %2")
                .arg(source_label(app.source),
                     QString::fromStdString(app.note)));
        detail_id_card_->setValue(QString::fromStdString(app.id));
        detail_src_card_->setValue(source_label(app.source));
        detail_exec_card_->setValue(
            app.command.empty() ? QStringLiteral("Built-in") : QString::fromStdString(app.command));
        detail_status_card_->setValueParts(
            app.installed ? QStringLiteral("Installed") : QStringLiteral("Available"),
            source_label(app.source));

        detail_icon_->setIconName(app.icon.empty()
                                      ? QStringLiteral("app-grid")
                                      : QString::fromStdString(app.icon));
        if (app.installed) {
            detail_launch_btn_->setText(QStringLiteral("Open"));
        } else if (app.source == finn::AppSource::kFlatpak) {
            detail_launch_btn_->setText(QStringLiteral("Install Flatpak"));
        } else {
            detail_launch_btn_->setText(QStringLiteral("Install Guide"));
        }
        detail_launch_btn_->setAccessibleName(
            QStringLiteral("%1 %2").arg(detail_launch_btn_->text(),
                                        QString::fromStdString(app.name)));
        detail_launch_btn_->setEnabled(true);
        stack_->setCurrentIndex(7);
    }

    QWidget* buildDiscoverPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(12);

        // Featured spotlight grid styled after Figma Settings About (20:64):
        // Left FinnHeroCard (#FFFFFF -> #EBFFDF light / #1E2923 -> #15221B dark gradient, r=6)
        // paired with 2 right-column FinnStatCards!
        auto* top_grid = new QGridLayout();
        top_grid->setContentsMargins(0, 0, 0, 0);
        top_grid->setHorizontalSpacing(10);
        top_grid->setVerticalSpacing(10);
        top_grid->setColumnStretch(0, 1);
        top_grid->setColumnStretch(1, 1);

        auto* hero = new FinnHeroCard(
            QStringLiteral("Finn Store "),
            QStringLiteral("Native · Flatpak · Wine · Android"),
            QStringLiteral("boot-mark"), page);
        hero->setInlineSuffix(QStringLiteral("Universal"));
        hero->setFixedHeight(150);
        top_grid->addWidget(hero, 0, 0, 2, 1);

        installed_stat_card_ = new FinnStatCard(
            QStringLiteral("Catalogue"), QStringLiteral("Ready"),
            QStringLiteral("Installed"), page);
        installed_stat_card_->setFixedHeight(70);
        top_grid->addWidget(installed_stat_card_, 0, 1);

        const bool has_flatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
        const auto wine_st = finn::compat::probe_wine_runtime();
        auto* runtime_stat = new FinnStatCard(
            QStringLiteral("Runtimes"),
            QStringLiteral("FinnSDK · Wayland"),
            (has_flatpak && wine_st.ready)
                ? QStringLiteral("Flatpak · Wine")
                : (wine_st.ready ? QStringLiteral("Wine ready") : QStringLiteral("Native")),
            page);
        runtime_stat->setFixedHeight(70);
        top_grid->addWidget(runtime_stat, 1, 1);

        root->addLayout(top_grid);

        auto* feat_lbl = new FinnLabel(
            QStringLiteral("Featured Applications"),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, page);
        root->addWidget(feat_lbl);

        // 2-column Settings-style r=6 (t.tile) app cards
        spotlight_grid_ = new QGridLayout();
        spotlight_grid_->setContentsMargins(0, 0, 0, 0);
        spotlight_grid_->setHorizontalSpacing(10);
        spotlight_grid_->setVerticalSpacing(10);
        spotlight_grid_->setColumnStretch(0, 1);
        spotlight_grid_->setColumnStretch(1, 1);
        root->addLayout(spotlight_grid_);
        root->addStretch(1);
        return page;
    }

    void populateSpotlight() {
        clear_layout(spotlight_grid_);
        int row = 0;
        int col = 0;
        int shown = 0;
        for (const finn::App& app : apps_) {
            if (!app.installed) continue;
            auto* card = makeAppTile(app, spotlight_grid_->parentWidget());
            spotlight_grid_->addWidget(card, row, col);
            ++shown;
            if (++col >= 2) {
                col = 0;
                ++row;
            }
            if (shown >= 8) break;
        }
    }

    FinnCard* makeAppTile(const finn::App& app, QWidget* parent) {
        // Settings-style r=6 (kRadiusMd) card on t.tile with 36x36 app icon,
        // clean category badge, and compact Open / Install pill button.
        auto* card = new FinnCard(kRadiusMd, true, parent);
        card->setFixedHeight(74);
        auto* lay = new QHBoxLayout(card);
        lay->setContentsMargins(12, 10, 12, 10);
        lay->setSpacing(10);

        auto* ico = new StoreIconWidget(36, 36, false, false, card);
        ico->setIconName(app.icon.empty()
                             ? QStringLiteral("app-grid")
                             : QString::fromStdString(app.icon));
        lay->addWidget(ico, 0, Qt::AlignVCenter);

        auto* col = new QVBoxLayout();
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        const QString app_name = QString::fromStdString(app.name);
        QString app_note = QString::fromStdString(app.note).trimmed();
        if (app_note.isEmpty() || app_note.compare(app_name, Qt::CaseInsensitive) == 0) {
            app_note = source_label(app.source);
        }
        auto* name = new ElidedFinnLabel(
            app_name, FinnLabel::Role::Body, FinnLabel::Tone::Ink, card);
        name->setFont(finn_font_px(13, QFont::Medium));
        auto* note = new ElidedFinnLabel(
            app_note, FinnLabel::Role::Caption, FinnLabel::Tone::Dim, card);
        col->addWidget(name);
        col->addWidget(note);
        lay->addLayout(col, 1);

        auto* action_btn = new FinnButton(
            app.installed ? QStringLiteral("Open") : QStringLiteral("Install"),
            app.installed ? FinnButton::Variant::Secondary
                          : FinnButton::Variant::Primary,
            {}, card);
        action_btn->setFixedHeight(26);
        action_btn->setMinimumWidth(56);
        action_btn->setAccessibleName(
            QStringLiteral("%1 %2").arg(action_btn->text(),
                                        QString::fromStdString(app.name)));
        card->setAccessibleName(QString::fromStdString(app.name));
        lay->addWidget(action_btn, 0, Qt::AlignVCenter);

        const finn::App copy = app;
        connect(action_btn, &QAbstractButton::clicked, this,
                [this, copy]() { launchOrInstallApp(copy); });
        connect(card, &FinnCard::clicked, this,
                [this, copy]() { openDetail(copy); });
        return card;
    }

    QWidget* buildAppsPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        apps_grid_ = new QGridLayout();
        apps_grid_->setContentsMargins(0, 0, 0, 0);
        apps_grid_->setHorizontalSpacing(10);
        apps_grid_->setVerticalSpacing(10);
        apps_grid_->setColumnStretch(0, 1);
        apps_grid_->setColumnStretch(1, 1);
        root->addLayout(apps_grid_);
        root->addStretch(1);
        return page;
    }

    void populateAppsGrid(const QString& query) {
        clear_layout(apps_grid_);
        const QString q = query.trimmed();
        int row = 0;
        int col = 0;
        int shown = 0;
        for (const finn::App& app : apps_) {
            if (!q.isEmpty() &&
                !QString::fromStdString(app.name).contains(q, Qt::CaseInsensitive) &&
                !QString::fromStdString(app.note).contains(q, Qt::CaseInsensitive) &&
                !QString::fromStdString(app.id).contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            apps_grid_->addWidget(makeAppTile(app, apps_grid_->parentWidget()), row, col);
            ++shown;
            if (++col >= 2) {
                col = 0;
                ++row;
            }
        }
        if (shown == 0) {
            apps_grid_->addWidget(
                new FinnEmptyState(
                    QStringLiteral("store"),
                    QStringLiteral("No matching applications"),
                    QStringLiteral("Try a different search query in the sidebar."),
                    {}, apps_grid_->parentWidget()),
                0, 0, 1, 2);
        }
    }

    QWidget* buildLinuxPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        const bool has_flatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
        root->addWidget(new FinnSettingRow(
            QStringLiteral("Flatpak Runtime"),
            has_flatpak ? QStringLiteral("Flatpak CLI ready — install Flathub applications or .flatpakref bundles")
                        : QStringLiteral("flatpak binary not installed (pacman -S flatpak)"),
            QStringLiteral("linux"),
            new FinnBadge(has_flatpak ? QStringLiteral("Ready") : QStringLiteral("Unavailable"),
                          has_flatpak ? FinnBadge::Tone::Success : FinnBadge::Tone::Neutral, page),
            page));

        auto* bar = new QHBoxLayout();
        bar->setSpacing(8);
        flatpak_input_ = new FinnTextField(
            QStringLiteral("Flatpak App ID (e.g. org.mozilla.firefox)"),
            page);
        auto* install_btn = new FinnButton(
            QStringLiteral("Install Flatpak"), FinnButton::Variant::Primary,
            QStringLiteral("download"), page);
        install_btn->setEnabled(has_flatpak);
        connect(install_btn, &QAbstractButton::clicked, this, [this]() {
            const QString id = flatpak_input_->text().trimmed();
            if (id.isEmpty()) return;
            runCommandIntoLog(QStringLiteral("flatpak"),
                              {QStringLiteral("install"), QStringLiteral("-y"),
                               QStringLiteral("flathub"), id},
                              linux_log_);
        });

        auto* appimg_btn = new FinnButton(
            QStringLiteral("Run AppImage"), FinnButton::Variant::Secondary,
            QStringLiteral("folder"), page);
        appimg_btn->setAccessibleName(QStringLiteral("Run AppImage"));
        install_btn->setAccessibleName(QStringLiteral("Install Flatpak"));
        flatpak_input_->setAccessibleName(QStringLiteral("Flatpak App ID"));
        connect(appimg_btn, &QAbstractButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select AppImage"), QDir::homePath(),
                QStringLiteral("AppImage (*.AppImage *.appimage);;All Files (*)"));
            if (path.isEmpty()) return;
            const QString fname = QFileInfo(path).fileName();
            if (!FinnDialog::confirm(
                    this, QStringLiteral("Run AppImage?"),
                    QStringLiteral("Grant executable permission (+x) and launch '%1'?\n\n"
                                   "Only run AppImages from sources you trust.\nPath: %2")
                        .arg(fname, path),
                    QStringLiteral("Run AppImage"), false)) {
                return;
            }
            if (!QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeUser)) {
                showToast(QStringLiteral("Failed to mark AppImage executable"),
                          FinnToast::Severity::Error);
                return;
            }
            if (!QProcess::startDetached(path, {})) {
                showToast(QStringLiteral("Failed to launch AppImage"),
                          FinnToast::Severity::Error);
                return;
            }
            showToast(QStringLiteral("Launched AppImage"), FinnToast::Severity::Success);
        });

        bar->addWidget(flatpak_input_, 1);
        bar->addWidget(install_btn);
        bar->addWidget(appimg_btn);
        root->addLayout(bar);

        linux_log_ = new QPlainTextEdit(page);
        linux_log_->setReadOnly(true);
        linux_log_->setMinimumHeight(200);
        linux_log_->setPlaceholderText(QStringLiteral("Package manager output appears here."));
        root->addWidget(linux_log_, 1);
        return page;
    }

    QWidget* buildWindowsPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        win_status_row_ = new FinnSettingRow(
            QStringLiteral("Wine Compatibility Layer"),
            QStringLiteral("Checking..."),
            QStringLiteral("view-open-windows"), nullptr, page);
        root->addWidget(win_status_row_);

        auto* pick_exe = new FinnButton(
            QStringLiteral("Run Windows .exe"), FinnButton::Variant::Primary,
            QStringLiteral("view-open-windows"), page);
        connect(pick_exe, &QAbstractButton::clicked, this, [this]() {
            const QString exe = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select Windows Executable"), QDir::homePath(),
                QStringLiteral("Windows Executables (*.exe *.msi);;All Files (*)"));
            if (exe.isEmpty()) return;
            const auto plan = finn::compat::build_wine_launch(exe.toStdString());
            if (!plan.ok || plan.argv.empty()) {
                showToast(QString::fromStdString(plan.error), FinnToast::Severity::Error);
                return;
            }
            QStringList args;
            for (size_t i = 1; i < plan.argv.size(); ++i) {
                args << QString::fromStdString(plan.argv[i]);
            }
            QProcess::startDetached(QString::fromStdString(plan.argv[0]), args);
            showToast(QStringLiteral("Launched via Wine"));
        });
        win_status_row_->setTrailingWidget(pick_exe);

        win_list_layout_ = new QVBoxLayout();
        win_list_layout_->setSpacing(8);
        root->addLayout(win_list_layout_);
        root->addStretch(1);
        return page;
    }

    void refreshWindowsPage() {
        if (!win_status_row_) return;
        const auto st = finn::compat::probe_wine_runtime();
        win_status_row_->setSubtitle(QString::fromStdString(st.summary));
        clear_layout(win_list_layout_);
        const std::string conf_path =
            (QDir::homePath() + QLatin1Char('/') +
             QString::fromLatin1(finn::kUserWindowsConfRel))
                .toStdString();
        const auto entries = finn::compat::load_windows_apps(conf_path);
        if (entries.empty()) {
            win_list_layout_->addWidget(new FinnEmptyState(
                QStringLiteral("view-open-windows"),
                QStringLiteral("No registered Windows applications"),
                QStringLiteral("Use 'Run Windows .exe' above to launch installers or portable apps."),
                {}, win_list_layout_->parentWidget()));
            return;
        }
        for (const auto& e : entries) {
            auto* launch = new FinnButton(
                QStringLiteral("Open"), FinnButton::Variant::Secondary, {},
                win_list_layout_->parentWidget());
            const std::string id = e.id;
            connect(launch, &QAbstractButton::clicked, this, [this, id]() {
                finn::SessionClient client;
                std::string err;
                client.launch(id, &err);
                showToast(err.empty() ? QStringLiteral("Launching Windows app...") : QString::fromStdString(err));
            });
            win_list_layout_->addWidget(new FinnSettingRow(
                QString::fromStdString(e.name), QString::fromStdString(e.exe_path),
                QStringLiteral("view-open-windows"), launch, win_list_layout_->parentWidget()));
        }
    }

    QWidget* buildAndroidPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        android_status_row_ = new FinnSettingRow(
            QStringLiteral("Waydroid Runtime"),
            QStringLiteral("Probing..."),
            QStringLiteral("android"), nullptr, page);
        root->addWidget(android_status_row_);

        android_install_btn_ = new FinnButton(
            QStringLiteral("Install APK"), FinnButton::Variant::Primary,
            QStringLiteral("android"), page);
        connect(android_install_btn_, &QAbstractButton::clicked, this, [this]() {
            const QString apk = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select Android APK"), QDir::homePath(),
                QStringLiteral("Android Package (*.apk);;All Files (*)"));
            if (apk.isEmpty()) return;
            runCommandIntoLog(QStringLiteral("waydroid"),
                              {QStringLiteral("app"), QStringLiteral("install"), apk},
                              android_log_);
        });
        android_status_row_->setTrailingWidget(android_install_btn_);

        android_list_layout_ = new QVBoxLayout();
        android_list_layout_->setSpacing(8);
        root->addLayout(android_list_layout_);

        android_log_ = new QPlainTextEdit(page);
        android_log_->setReadOnly(true);
        android_log_->setMinimumHeight(130);
        root->addWidget(android_log_);
        return page;
    }

    void refreshAndroidPage() {
        if (!android_status_row_) return;
        const auto st = finn::compat::probe_android_runtime();
        android_status_row_->setSubtitle(QString::fromStdString(st.summary));
        android_install_btn_->setEnabled(st.ready);
        clear_layout(android_list_layout_);

        const auto apps = finn::compat::list_android_apps();
        if (apps.empty()) {
            android_list_layout_->addWidget(new FinnEmptyState(
                QStringLiteral("android"),
                st.ready ? QStringLiteral("No Android apps installed")
                         : QStringLiteral("Waydroid container inactive"),
                QString::fromStdString(st.summary),
                {}, android_list_layout_->parentWidget()));
            return;
        }
        for (const auto& a : apps) {
            auto* launch = new FinnButton(
                QStringLiteral("Open"), FinnButton::Variant::Secondary, {},
                android_list_layout_->parentWidget());
            const QString pkg = QString::fromStdString(a.package_name);
            connect(launch, &QAbstractButton::clicked, this, [pkg]() {
                QProcess::startDetached(QStringLiteral("waydroid"),
                                        {QStringLiteral("app"), QStringLiteral("launch"), pkg});
            });
            android_list_layout_->addWidget(new FinnSettingRow(
                QString::fromStdString(a.name), pkg,
                QStringLiteral("android"), launch, android_list_layout_->parentWidget()));
        }
    }

    QWidget* buildUpdatesPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        updates_check_btn_ = new FinnButton(
            QStringLiteral("Check Now"), FinnButton::Variant::Primary,
            QStringLiteral("update"), page);
        updates_check_btn_->setAccessibleName(QStringLiteral("Check for updates"));
        connect(updates_check_btn_, &QAbstractButton::clicked, this, [this]() {
            refreshUpdatesList();
            showToast(QStringLiteral("Checking pacman and Flatpak updates..."));
        });
        root->addWidget(new FinnSettingRow(
            QStringLiteral("System & Application Updates"),
            QStringLiteral("Query pacman repositories and Flathub remotes"),
            QStringLiteral("download"), updates_check_btn_, page));

        updates_log_ = new QPlainTextEdit(page);
        updates_log_->setReadOnly(true);
        updates_log_->setMinimumHeight(240);
        updates_log_->setAccessibleName(QStringLiteral("Updates log"));
        root->addWidget(updates_log_, 1);
        return page;
    }

    void refreshUpdatesList() {
        if (!updates_log_) return;
        const quint64 gen = ++updates_gen_;
        if (updates_pac_proc_) {
            updates_pac_proc_->disconnect(this);
            updates_pac_proc_->kill();
            updates_pac_proc_->deleteLater();
        }
        if (updates_fp_proc_) {
            updates_fp_proc_->disconnect(this);
            updates_fp_proc_->kill();
            updates_fp_proc_->deleteLater();
        }

        if (updates_check_btn_) updates_check_btn_->setEnabled(false);
        updates_log_->setPlainText(QStringLiteral("Checking pacman and Flatpak updates..."));

        auto run_flatpak_check = [this, gen](QString acc_out) {
            if (gen != updates_gen_ || !updates_log_) return;
            if (QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty()) {
                updates_log_->setPlainText(acc_out);
                if (updates_check_btn_) updates_check_btn_->setEnabled(true);
                return;
            }
            auto* fp = new QProcess(this);
            updates_fp_proc_ = fp;
            connect(fp, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, fp, gen, acc_out](int code, QProcess::ExitStatus status) mutable {
                        if (gen == updates_gen_ && updates_log_) {
                            const QString f = QString::fromUtf8(fp->readAllStandardOutput()).trimmed();
                            const QString ferr = QString::fromUtf8(fp->readAllStandardError()).trimmed();
                            acc_out += QStringLiteral("\n=== Flatpak Updates ===\n");
                            if (status == QProcess::NormalExit && code == 0) {
                                acc_out += f.isEmpty()
                                               ? QStringLiteral("No Flatpak updates pending.\n")
                                               : f + QLatin1Char('\n');
                            } else {
                                acc_out += ferr.isEmpty()
                                               ? QStringLiteral("Flatpak update check failed (exit %1).\n").arg(code)
                                               : ferr + QLatin1Char('\n');
                            }
                            updates_log_->setPlainText(acc_out);
                            if (updates_check_btn_) updates_check_btn_->setEnabled(true);
                        }
                        fp->deleteLater();
                    });
            connect(fp, &QProcess::errorOccurred, this,
                    [this, fp, gen, acc_out](QProcess::ProcessError) mutable {
                        if (gen == updates_gen_ && updates_log_) {
                            acc_out += QStringLiteral("\n=== Flatpak Updates ===\nFailed to invoke flatpak.\n");
                            updates_log_->setPlainText(acc_out);
                            if (updates_check_btn_) updates_check_btn_->setEnabled(true);
                        }
                        fp->deleteLater();
                    });
            fp->start(QStringLiteral("flatpak"),
                      {QStringLiteral("remote-ls"), QStringLiteral("--updates")});
        };

        if (QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
            run_flatpak_check(QStringLiteral("=== System Packages ===\npacman not installed.\n"));
            return;
        }

        auto* pac = new QProcess(this);
        updates_pac_proc_ = pac;
        connect(pac, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, pac, gen, run_flatpak_check](int code, QProcess::ExitStatus status) {
                    QString out;
                    const QString p = QString::fromUtf8(pac->readAllStandardOutput()).trimmed();
                    const QString perr = QString::fromUtf8(pac->readAllStandardError()).trimmed();
                    out += QStringLiteral("=== System Packages (pacman -Qu) ===\n");
                    if (status == QProcess::NormalExit && (code == 0 || code == 1)) {
                        out += p.isEmpty()
                                   ? QStringLiteral("All system packages are up to date.\n")
                                   : p + QLatin1Char('\n');
                    } else {
                        out += perr.isEmpty()
                                   ? QStringLiteral("pacman -Qu exited with code %1.\n").arg(code)
                                   : perr + QLatin1Char('\n');
                    }
                    pac->deleteLater();
                    if (gen == updates_gen_) run_flatpak_check(out);
                });
        connect(pac, &QProcess::errorOccurred, this,
                [this, pac, gen, run_flatpak_check](QProcess::ProcessError) {
                    pac->deleteLater();
                    if (gen == updates_gen_) {
                        run_flatpak_check(
                            QStringLiteral("=== System Packages (pacman -Qu) ===\nFailed to invoke pacman.\n"));
                    }
                });
        pac->start(QStringLiteral("pacman"), {QStringLiteral("-Qu")});
    }

    QWidget* buildSdkPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        root->addWidget(new FinnSettingRow(
            QStringLiteral("FinnSDK Native App Generator"),
            QStringLiteral("Scaffold a new C++20/Qt6 application backed by FinnUI and finn-sdk"),
            QStringLiteral("terminal"),
            new FinnBadge(QStringLiteral("C++20 / Qt6"), FinnBadge::Tone::Accent, page),
            page));

        sdk_name_edit_ = new FinnTextField(QStringLiteral("Application Name (e.g. Finn Mail)"), page);
        sdk_name_edit_->setAccessibleName(QStringLiteral("New FinnSDK Application Name"));
        auto* create_btn = new FinnButton(
            QStringLiteral("Scaffold in ~/Projects"), FinnButton::Variant::Primary,
            QStringLiteral("app-grid"), page);
        create_btn->setAccessibleName(QStringLiteral("Scaffold FinnSDK project"));
        connect(create_btn, &QAbstractButton::clicked, this, [this, create_btn]() {
            const QString name = sdk_name_edit_->text().trimmed();
            if (name.isEmpty()) return;
            const QString slug = slugify(name);
            const QString target = QDir::homePath() + QStringLiteral("/Projects/") + slug;
            create_btn->setEnabled(false);
            auto* proc = new QProcess(this);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, proc, create_btn, target](int code, QProcess::ExitStatus status) {
                        create_btn->setEnabled(true);
                        const bool ok = (status == QProcess::NormalExit && code == 0);
                        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                        showToast(ok ? QStringLiteral("Created %1").arg(target)
                                     : (err.isEmpty() ? QStringLiteral("finn-sdk failed") : err),
                                  ok ? FinnToast::Severity::Success : FinnToast::Severity::Error);
                        proc->deleteLater();
                    });
            connect(proc, &QProcess::errorOccurred, this,
                    [this, proc, create_btn](QProcess::ProcessError) {
                        create_btn->setEnabled(true);
                        showToast(QStringLiteral("finn-sdk binary not found"),
                                  FinnToast::Severity::Error);
                        proc->deleteLater();
                    });
            proc->start(QStringLiteral("finn-sdk"),
                        {QStringLiteral("new"), target, QStringLiteral("--name"), name});
        });
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        row->addWidget(sdk_name_edit_, 1);
        row->addWidget(create_btn);
        root->addLayout(row);

        auto* pkg_btn = new FinnButton(
            QStringLiteral("Verify .finnapp"), FinnButton::Variant::Secondary,
            QStringLiteral("folder"), page);
        pkg_btn->setAccessibleName(QStringLiteral("Verify .finnapp bundle"));
        auto* pkg_dev_toggle = new FinnToggle(false, page);
        pkg_dev_toggle->setAccessibleName(QStringLiteral("Allow unsigned .finnapp packages"));
        auto* pkg_controls = new QWidget(page);
        auto* pkg_ctrl_lay = new QHBoxLayout(pkg_controls);
        pkg_ctrl_lay->setContentsMargins(0, 0, 0, 0);
        pkg_ctrl_lay->setSpacing(8);
        pkg_ctrl_lay->addWidget(new FinnLabel(
            QStringLiteral("Allow unsigned"), FinnLabel::Role::Caption,
            FinnLabel::Tone::Dim, pkg_controls));
        pkg_ctrl_lay->addWidget(pkg_dev_toggle);
        pkg_ctrl_lay->addWidget(pkg_btn);

        root->addWidget(new FinnSettingRow(
            QStringLiteral("Inspect & Verify .finnapp Package"),
            QStringLiteral("Verify manifest schema, payload SHA-256, and Ed25519 signature via finn-sdk"),
            QStringLiteral("store"), pkg_controls, page));

        finnapp_log_ = new QPlainTextEdit(page);
        finnapp_log_->setReadOnly(true);
        finnapp_log_->setMinimumHeight(180);
        finnapp_log_->setAccessibleName(QStringLiteral("Package verification output"));
        finnapp_log_->setPlaceholderText(
            QStringLiteral("Select a .finnapp bundle to inspect its manifest and signature."));
        root->addWidget(finnapp_log_, 1);

        connect(pkg_btn, &QAbstractButton::clicked, this, [this, pkg_btn, pkg_dev_toggle]() {
            const QString archive = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select .finnapp Package"), QDir::homePath(),
                QStringLiteral("FinnApp Bundles (*.finnapp);;All Files (*)"));
            if (archive.isEmpty()) return;

            QString pubkey;
            const bool allow_unsigned = pkg_dev_toggle->isChecked();
            if (!allow_unsigned) {
                pubkey = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Select Trusted Ed25519 Public Key (.pem)"),
                    QFileInfo(archive).absolutePath(),
                    QStringLiteral("PEM Public Keys (*.pem *.pub);;All Files (*)"));
            }

            QStringList args{QStringLiteral("verify"), archive, QStringLiteral("--json")};
            if (!pubkey.isEmpty()) {
                args << QStringLiteral("--pubkey") << pubkey;
            } else if (allow_unsigned) {
                args << QStringLiteral("--allow-unsigned");
            }

            pkg_btn->setEnabled(false);
            finnapp_log_->setPlainText(QStringLiteral("Verifying %1...").arg(archive));
            auto* proc = new QProcess(this);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, proc, pkg_btn](int, QProcess::ExitStatus) {
                        pkg_btn->setEnabled(true);
                        const QByteArray raw = proc->readAllStandardOutput();
                        const QJsonDocument doc = QJsonDocument::fromJson(raw);
                        if (!doc.isObject()) {
                            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                            finnapp_log_->setPlainText(
                                err.isEmpty() ? QString::fromUtf8(raw) : err);
                            showToast(QStringLiteral("Verification failed"),
                                      FinnToast::Severity::Error);
                            proc->deleteLater();
                            return;
                        }
                        const QJsonObject obj = doc.object();
                        const bool valid = obj.value(QStringLiteral("valid")).toBool();
                        const bool is_signed = obj.value(QStringLiteral("signed")).toBool();
                        QString summary;
                        summary += QStringLiteral("Archive: %1\n").arg(obj.value(QStringLiteral("archive")).toString());
                        summary += QStringLiteral("App ID:  %1\n").arg(obj.value(QStringLiteral("id")).toString(QStringLiteral("-")));
                        summary += QStringLiteral("Version: %1\n").arg(obj.value(QStringLiteral("version")).toString(QStringLiteral("-")));
                        summary += QStringLiteral("Signed:  %1\n").arg(is_signed ? QStringLiteral("yes (Ed25519)") : QStringLiteral("no (unsigned dev)"));
                        summary += QStringLiteral("Valid:   %1\n").arg(valid ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
                        const QJsonArray errs = obj.value(QStringLiteral("errors")).toArray();
                        if (!errs.isEmpty()) {
                            summary += QStringLiteral("\nErrors:\n");
                            for (const auto& e : errs) {
                                summary += QStringLiteral("  - %1\n").arg(e.toString());
                            }
                        } else if (valid) {
                            summary += QStringLiteral("\nNote: v1 native app permissions in finn-app.json are declared-only and not enforced by an OS sandbox.\n");
                        }
                        finnapp_log_->setPlainText(summary);
                        showToast(valid ? QStringLiteral(".finnapp verified")
                                        : QStringLiteral(".finnapp verification failed"),
                                  valid ? FinnToast::Severity::Success : FinnToast::Severity::Error);
                        proc->deleteLater();
                    });
            connect(proc, &QProcess::errorOccurred, this,
                    [this, proc, pkg_btn](QProcess::ProcessError) {
                        pkg_btn->setEnabled(true);
                        finnapp_log_->setPlainText(QStringLiteral("Failed to run finn-sdk verify."));
                        showToast(QStringLiteral("finn-sdk not found"), FinnToast::Severity::Error);
                        proc->deleteLater();
                    });
            proc->start(QStringLiteral("finn-sdk"), args);
        });
        return page;
    }

    QWidget* buildDetailPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 2, 16);
        root->setSpacing(10);

        back_btn_ = new FinnButton(
            QStringLiteral("Back to Discover"), FinnButton::Variant::Ghost,
            QStringLiteral("chevron-left"), page);
        back_btn_->setFixedHeight(28);
        connect(back_btn_, &QAbstractButton::clicked, this, [this]() {
            sidebar_->setCurrentId(last_list_page_);
            switchPage(last_list_page_);
        });
        root->addWidget(back_btn_, 0, Qt::AlignLeft);

        auto* header = new FinnCard(kRadiusMd, false, page);
        auto* hl = new QHBoxLayout(header);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(14);

        detail_icon_ = new StoreIconWidget(48, 44, false, false, header);
        hl->addWidget(detail_icon_, 0, Qt::AlignVCenter);

        auto* hcol = new QVBoxLayout();
        hcol->setSpacing(3);
        detail_title_ = new ElidedFinnLabel({}, FinnLabel::Role::Headline, FinnLabel::Tone::Ink, header);
        detail_sub_ = new FinnLabel({}, FinnLabel::Role::Caption, FinnLabel::Tone::Dim, header);
        detail_sub_->setWordWrap(true);
        hcol->addWidget(detail_title_);
        hcol->addWidget(detail_sub_);
        hl->addLayout(hcol, 1);

        detail_launch_btn_ = new FinnButton(
            QStringLiteral("Open"), FinnButton::Variant::Primary, {}, header);
        detail_launch_btn_->setFixedHeight(30);
        detail_launch_btn_->setAccessibleName(QStringLiteral("Launch or install application"));
        connect(detail_launch_btn_, &QAbstractButton::clicked, this, [this]() {
            if (selected_app_.installed) {
                finn::SessionClient client;
                std::string err;
                client.launch(selected_app_.id, &err);
                showToast(err.empty()
                              ? QStringLiteral("Launching %1").arg(QString::fromStdString(selected_app_.name))
                              : QString::fromStdString(err));
                return;
            }
            if (selected_app_.source == finn::AppSource::kFlatpak) {
                const QString fp_id = QString::fromStdString(selected_app_.id);
                if (QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty()) {
                    showToast(QStringLiteral("flatpak binary not installed"),
                              FinnToast::Severity::Error);
                    return;
                }
                sidebar_->setCurrentId(QStringLiteral("linux"));
                switchPage(QStringLiteral("linux"));
                runCommandIntoLog(QStringLiteral("flatpak"),
                                  {QStringLiteral("install"), QStringLiteral("-y"),
                                   QStringLiteral("flathub"), fp_id},
                                  linux_log_);
                return;
            }
            const QString app_name = QString::fromStdString(selected_app_.name);
            const QString cmd = QString::fromStdString(selected_app_.command);
            FinnDialog::confirm(
                this,
                QStringLiteral("Install %1").arg(app_name),
                QStringLiteral("Finn Store does not elevate privileges via finnd in v1.\n\n"
                               "Build and install '%1' from FinnOS sources (make && sudo make install) "
                               "or verify '%2' is present on disk and listed in /var/lib/finn/installed.")
                    .arg(app_name, cmd.isEmpty() ? app_name : cmd),
                QStringLiteral("OK"), false);
        });
        hl->addWidget(detail_launch_btn_);
        root->addWidget(header);

        auto* grid = new QGridLayout();
        grid->setSpacing(10);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        detail_id_card_ = new FinnStatCard(QStringLiteral("App ID"), QStringLiteral("-"), page);
        detail_src_card_ = new FinnStatCard(QStringLiteral("Runtime"), QStringLiteral("-"), page);
        detail_exec_card_ = new FinnStatCard(QStringLiteral("Executable"), QStringLiteral("-"), page);
        detail_status_card_ = new FinnStatCard(QStringLiteral("Status"), QStringLiteral("-"), page);
        grid->addWidget(detail_id_card_, 0, 0);
        grid->addWidget(detail_src_card_, 0, 1);
        grid->addWidget(detail_exec_card_, 1, 0);
        grid->addWidget(detail_status_card_, 1, 1);
        root->addLayout(grid);
        root->addStretch(1);
        return page;
    }

    void runCommandIntoLog(const QString& bin, const QStringList& args, QPlainTextEdit* log) {
        if (log) log->appendPlainText(QStringLiteral("$ %1 %2").arg(bin, args.join(QLatin1Char(' '))));
        auto* proc = new QProcess(this);
        connect(proc, &QProcess::readyReadStandardOutput, this, [proc, log]() {
            if (log) log->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
        });
        connect(proc, &QProcess::readyReadStandardError, this, [proc, log]() {
            if (log) log->appendPlainText(QString::fromUtf8(proc->readAllStandardError()).trimmed());
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, proc, log](int code, QProcess::ExitStatus) {
                    if (log) log->appendPlainText(QStringLiteral("Exited with code %1").arg(code));
                    reloadCatalogue();
                    proc->deleteLater();
                });
        proc->start(bin, args);
    }

    QWidget* right_host_ = nullptr;
    FinnSidebar* sidebar_ = nullptr;
    FinnLabel* page_title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QString last_list_page_ = QStringLiteral("discover");
    std::vector<finn::App> apps_;
    finn::App selected_app_{};

    FinnStatCard* installed_stat_card_ = nullptr;
    QGridLayout* spotlight_grid_ = nullptr;
    QGridLayout* apps_grid_ = nullptr;
    FinnTextField* flatpak_input_ = nullptr;
    QPlainTextEdit* linux_log_ = nullptr;
    FinnSettingRow* win_status_row_ = nullptr;
    QVBoxLayout* win_list_layout_ = nullptr;
    FinnSettingRow* android_status_row_ = nullptr;
    FinnButton* android_install_btn_ = nullptr;
    QVBoxLayout* android_list_layout_ = nullptr;
    QPlainTextEdit* android_log_ = nullptr;
    QPlainTextEdit* updates_log_ = nullptr;
    FinnButton* updates_check_btn_ = nullptr;
    QPointer<QProcess> updates_pac_proc_;
    QPointer<QProcess> updates_fp_proc_;
    quint64 updates_gen_ = 0;
    FinnTextField* sdk_name_edit_ = nullptr;
    QPlainTextEdit* finnapp_log_ = nullptr;

    FinnButton* back_btn_ = nullptr;
    StoreIconWidget* detail_icon_ = nullptr;
    ElidedFinnLabel* detail_title_ = nullptr;
    FinnLabel* detail_sub_ = nullptr;
    FinnButton* detail_launch_btn_ = nullptr;
    FinnStatCard* detail_id_card_ = nullptr;
    FinnStatCard* detail_src_card_ = nullptr;
    FinnStatCard* detail_exec_card_ = nullptr;
    FinnStatCard* detail_status_card_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-store 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-store");
            return 0;
        }
    }
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("store"),
                                  QStringLiteral("Finn Store"));
    StoreWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"
