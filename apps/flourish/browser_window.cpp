#include "browser_window.hpp"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFindTextResult>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <functional>

#include "start_page.hpp"

namespace flourish {

class FlourishWebView final : public QWebEngineView {
    Q_OBJECT

public:
    using CreateWindowFn = std::function<QWebEngineView*(QWebEnginePage::WebWindowType)>;

    explicit FlourishWebView(QWebEngineProfile* profile,
                             CreateWindowFn create_cb,
                             QWidget* parent = nullptr)
        : QWebEngineView(parent), create_cb_(std::move(create_cb)) {
        auto* p = new QWebEnginePage(profile, this);
        setPage(p);
    }

protected:
    QWebEngineView* createWindow(QWebEnginePage::WebWindowType type) override {
        return create_cb_ ? create_cb_(type) : nullptr;
    }

private:
    CreateWindowFn create_cb_;
};

namespace {

QString new_tab_id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString format_bytes(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("0 B");
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
    const double mb = kb / 1024.0;
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

}  // namespace

BrowserWindow::BrowserWindow(const QUrl& initial_url, QWidget* parent)
    : finn::ui::FinnWindow(QStringLiteral("Flourish"),
                           QStringLiteral("browser"),
                           parent) {
    setChromeMode(ChromeMode::FigmaSplit);
    resize(1280, 820);

    const QString data_dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        QStringLiteral("/finn/flourish/profile");
    QDir().mkpath(data_dir);

    profile_ = new QWebEngineProfile(QStringLiteral("flourish"), this);
    profile_->setPersistentStoragePath(data_dir);
    profile_->setCachePath(data_dir + QStringLiteral("/cache"));
    profile_->setHttpUserAgent(
        profile_->httpUserAgent() + QStringLiteral(" Flourish/0.1 FinnOS"));
    profile_->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    profile_->settings()->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled,
                                       !finn::ui::FinnThemeManager::instance().tokens().reduced_motion);
    connect(profile_, &QWebEngineProfile::downloadRequested,
            this, &BrowserWindow::handleDownload);

    const bool first_launch = !QFileInfo::exists(SessionStore::sessionFilePath());
    SessionStore::load(spaces_, tabs_, active_space_id_);

    sidebar_ = new SidebarPanel(this);
    setSidebarWidget(sidebar_);

    setupContentArea();
    setupFindBar();
    setupDownloadsPanel();

    command_bar_ = new CommandBarOverlay(this);

    connect(sidebar_, &SidebarPanel::collapseRequested, this, &BrowserWindow::toggleSidebarAnimated);
    connect(sidebar_, &SidebarPanel::backRequested, this, [this]() {
        if (auto* v = activeWebView()) v->back();
    });
    connect(sidebar_, &SidebarPanel::forwardRequested, this, [this]() {
        if (auto* v = activeWebView()) v->forward();
    });
    connect(sidebar_, &SidebarPanel::reloadOrStopRequested, this, [this]() {
        if (auto* v = activeWebView()) {
            const BrowserTab* tab = findTab(active_tab_id_);
            if (tab && tab->loading) {
                v->stop();
            } else {
                v->reload();
            }
        }
    });
    connect(sidebar_, &SidebarPanel::downloadsRequested, this, [this]() {
        downloads_panel_->setVisible(!downloads_panel_->isVisible());
        if (downloads_panel_->isVisible()) {
            refreshDownloadsList();
            positionFloatingPanels();
            downloads_panel_->raise();
        }
    });
    connect(sidebar_, &SidebarPanel::commandBarRequested, this, [this]() {
        const BrowserTab* tab = findTab(active_tab_id_);
        command_bar_->open(CommandBarOverlay::Mode::EditCurrentUrl,
                           tab ? tab->url : QUrl(), spaces_, tabs_);
    });
    connect(sidebar_, &SidebarPanel::newTabRequested, this, [this]() {
        openUrlInNewTab(QUrl(QStringLiteral("flourish://home")), true);
    });
    connect(sidebar_, &SidebarPanel::tabSelected, this, &BrowserWindow::activateTab);
    connect(sidebar_, &SidebarPanel::tabClosed, this, &BrowserWindow::closeTab);
    connect(sidebar_, &SidebarPanel::tabPinToggled, this, &BrowserWindow::togglePinTab);
    connect(sidebar_, &SidebarPanel::tabFavoriteToggled, this, &BrowserWindow::toggleFavoriteTab);
    connect(sidebar_, &SidebarPanel::tabSplitRequested, this, &BrowserWindow::toggleSplitView);
    connect(sidebar_, &SidebarPanel::tabDuplicateRequested, this, &BrowserWindow::duplicateTab);
    connect(sidebar_, &SidebarPanel::tabReloadRequested, this, [this](const QString& tid) {
        if (auto* v = views_.value(tid, nullptr)) {
            v->reload();
        }
    });
    connect(sidebar_, &SidebarPanel::spaceSelected, this, &BrowserWindow::switchSpace);
    connect(sidebar_, &SidebarPanel::newSpaceRequested, this, &BrowserWindow::createNewSpace);

    connect(command_bar_, &CommandBarOverlay::navigateInCurrentTab, this, [this](const QUrl& url) {
        if (!active_tab_id_.isEmpty()) {
            navigateTab(active_tab_id_, url);
        } else {
            openUrlInNewTab(url, true);
        }
    });
    connect(command_bar_, &CommandBarOverlay::openInNewTab, this, [this](const QUrl& url) {
        openUrlInNewTab(url, true);
    });
    connect(command_bar_, &CommandBarOverlay::switchToTab, this, &BrowserWindow::activateTab);
    connect(command_bar_, &CommandBarOverlay::switchToSpace, this, &BrowserWindow::switchSpace);
    connect(command_bar_, &CommandBarOverlay::actionRequested, this, [this](const QString& action) {
        if (action == QStringLiteral("split")) {
            toggleSplitView();
        } else if (action == QStringLiteral("sidebar")) {
            toggleSidebarAnimated();
        } else if (action == QStringLiteral("dark")) {
            auto& mgr = finn::ui::FinnThemeManager::instance();
            mgr.setDark(!mgr.isDark());
        } else if (action == QStringLiteral("downloads")) {
            downloads_panel_->setVisible(true);
            refreshDownloadsList();
            positionFloatingPanels();
            downloads_panel_->raise();
        } else if (action == QStringLiteral("intro")) {
            showIntroOverlay();
        }
    });

    connect(&finn::ui::FinnThemeManager::instance(), &finn::ui::FinnThemeManager::themeChanged,
            this, [this]() {
                syncWebDarkMode();
                syncUi();
                for (const auto& tab : tabs_) {
                    if (tab.url.scheme() == QStringLiteral("flourish")) {
                        if (auto* v = views_.value(tab.id, nullptr)) {
                            loadStartPageInto(v, tab.space_id);
                        }
                    }
                }
            });

    setupShortcuts();
    syncWebDarkMode();

    if (initial_url.isValid() && !initial_url.isEmpty()) {
        openUrlInNewTab(initial_url, true);
    } else {
        switchSpace(active_space_id_);
    }
    syncTopBarForSidebar();

    if (first_launch && (!initial_url.isValid() || initial_url.isEmpty())) {
        showIntroOverlay();
    }
}

BrowserWindow::~BrowserWindow() {
    SessionStore::save(spaces_, tabs_, active_space_id_);
}

void BrowserWindow::setupContentArea() {
    content_root_ = new QWidget(this);
    auto* col = new QVBoxLayout(content_root_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // 38px top bar: keeps window drag region & FinnWindowControls (top-right 98px) unobstructed.
    auto* top_bar = new QWidget(content_root_);
    top_bar->setFixedHeight(38);
    auto* tb = new QHBoxLayout(top_bar);
    tb->setContentsMargins(10, 4, 102, 4);
    tb->setSpacing(6);

    mini_nav_group_ = new QWidget(top_bar);
    auto* mini_lay = new QHBoxLayout(mini_nav_group_);
    mini_lay->setContentsMargins(0, 0, 0, 0);
    mini_lay->setSpacing(4);

    auto* show_sb_btn = new finn::ui::FinnIconButton(
        QStringLiteral("sidebar-toggle"), 26, 14, mini_nav_group_);
    show_sb_btn->setToolTip(QStringLiteral("Show Sidebar (Ctrl+S)"));
    show_sb_btn->setAccessibleName(QStringLiteral("Show Sidebar"));
    connect(show_sb_btn, &QAbstractButton::clicked, this, &BrowserWindow::toggleSidebarAnimated);
    mini_back_btn_ = new finn::ui::FinnIconButton(
        QStringLiteral("backward"), 26, 14, mini_nav_group_);
    mini_back_btn_->setAccessibleName(QStringLiteral("Back"));
    connect(mini_back_btn_, &QAbstractButton::clicked, this, [this]() {
        if (auto* v = activeWebView()) v->back();
    });
    mini_fwd_btn_ = new finn::ui::FinnIconButton(
        QStringLiteral("forward"), 26, 14, mini_nav_group_);
    mini_fwd_btn_->setAccessibleName(QStringLiteral("Forward"));
    connect(mini_fwd_btn_, &QAbstractButton::clicked, this, [this]() {
        if (auto* v = activeWebView()) v->forward();
    });
    mini_reload_btn_ = new finn::ui::FinnIconButton(
        QStringLiteral("refresh"), 26, 14, mini_nav_group_);
    mini_reload_btn_->setAccessibleName(QStringLiteral("Reload"));
    connect(mini_reload_btn_, &QAbstractButton::clicked, this, [this]() {
        if (auto* v = activeWebView()) v->reload();
    });
    mini_url_btn_ = new finn::ui::FinnButton(
        QStringLiteral("Search or enter URL..."),
        finn::ui::FinnButton::Variant::Secondary, QStringLiteral("search-01"), mini_nav_group_);
    mini_url_btn_->setAccessibleName(QStringLiteral("Address bar"));
    connect(mini_url_btn_, &QAbstractButton::clicked, this, [this]() {
        const BrowserTab* tab = findTab(active_tab_id_);
        command_bar_->open(CommandBarOverlay::Mode::EditCurrentUrl,
                           tab ? tab->url : QUrl(), spaces_, tabs_);
    });

    mini_lay->addWidget(show_sb_btn);
    mini_lay->addWidget(mini_back_btn_);
    mini_lay->addWidget(mini_fwd_btn_);
    mini_lay->addWidget(mini_reload_btn_);
    mini_lay->addWidget(mini_url_btn_);
    tb->addWidget(mini_nav_group_);

    space_badge_ = new finn::ui::FinnBadge(
        QStringLiteral("Personal"), finn::ui::FinnBadge::Tone::Accent, top_bar);
    page_title_lbl_ = new finn::ui::FinnLabel(
        QStringLiteral("Flourish"), finn::ui::FinnLabel::Role::Body,
        finn::ui::FinnLabel::Tone::Dim, top_bar);
    page_title_lbl_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    split_btn_ = new finn::ui::FinnIconButton(QStringLiteral("view-open-windows"), 26, 14, top_bar);
    split_btn_->setToolTip(QStringLiteral("Toggle Split View (Ctrl+Shift+D)"));
    split_btn_->setAccessibleName(QStringLiteral("Toggle Split View"));
    connect(split_btn_, &QAbstractButton::clicked, this, [this]() { toggleSplitView(); });

    find_btn_ = new finn::ui::FinnIconButton(QStringLiteral("search-01"), 26, 14, top_bar);
    find_btn_->setToolTip(QStringLiteral("Find in Page (Ctrl+F)"));
    find_btn_->setAccessibleName(QStringLiteral("Find in Page"));
    connect(find_btn_, &QAbstractButton::clicked, this, [this]() {
        find_bar_->setVisible(!find_bar_->isVisible());
        if (find_bar_->isVisible()) {
            positionFloatingPanels();
            find_bar_->raise();
            find_input_->focusInput();
        }
    });

    tb->addWidget(space_badge_);
    tb->addWidget(page_title_lbl_, 1);
    tb->addWidget(split_btn_);
    tb->addWidget(find_btn_);
    col->addWidget(top_bar);

    load_bar_ = new finn::ui::FinnProgressBar(3, content_root_);
    load_bar_->setVisible(false);
    col->addWidget(load_bar_);

    col->addWidget(new finn::ui::FinnDivider(Qt::Horizontal, content_root_));

    auto* frame_wrap = new QWidget(content_root_);
    auto* wrap_lay = new QVBoxLayout(frame_wrap);
    wrap_lay->setContentsMargins(8, 8, 8, 8);
    wrap_lay->setSpacing(0);

    auto* viewport_surface = new finn::ui::FinnSurface(
        finn::ui::FinnSurface::Role::Tile, finn::ui::kRadiusLg, true, frame_wrap);
    auto* surf_lay = new QVBoxLayout(viewport_surface);
    surf_lay->setContentsMargins(1, 1, 1, 1);
    surf_lay->setSpacing(0);

    splitter_ = new QSplitter(Qt::Horizontal, viewport_surface);
    splitter_->setHandleWidth(2);
    primary_stack_ = new QStackedWidget(splitter_);
    finn::ui::install_stack_animator(primary_stack_);
    split_stack_ = new QStackedWidget(splitter_);
    finn::ui::install_stack_animator(split_stack_);
    split_stack_->setVisible(false);

    splitter_->addWidget(primary_stack_);
    splitter_->addWidget(split_stack_);
    surf_lay->addWidget(splitter_, 1);
    wrap_lay->addWidget(viewport_surface, 1);
    col->addWidget(frame_wrap, 1);

    setContentWidget(content_root_);
}

void BrowserWindow::setupFindBar() {
    find_bar_ = new finn::ui::FinnGlassPanel(finn::ui::kRadiusLg, content_root_);
    find_bar_->setFixedSize(330, 46);
    find_bar_->hide();

    auto* lay = new QHBoxLayout(find_bar_);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(6);

    find_input_ = new finn::ui::FinnSearchField(QStringLiteral("Find in page..."), find_bar_);
    find_count_lbl_ = new finn::ui::FinnLabel(
        QString(), finn::ui::FinnLabel::Role::Caption,
        finn::ui::FinnLabel::Tone::Dim, find_bar_);
    auto* prev_btn = new finn::ui::FinnIconButton(QStringLiteral("backward"), 24, 13, find_bar_);
    auto* next_btn = new finn::ui::FinnIconButton(QStringLiteral("forward"), 24, 13, find_bar_);
    auto* close_btn = new finn::ui::FinnIconButton(QStringLiteral("x"), 24, 12, find_bar_);

    lay->addWidget(find_input_, 1);
    lay->addWidget(find_count_lbl_);
    lay->addWidget(prev_btn);
    lay->addWidget(next_btn);
    lay->addWidget(close_btn);

    auto run_find = [this](QWebEnginePage::FindFlags flags) {
        if (auto* v = activeWebView()) {
            const QString q = find_input_->text();
            if (q.isEmpty()) {
                v->page()->findText(QString());
                find_count_lbl_->setText(QString());
                return;
            }
            v->page()->findText(q, flags, [this](const QWebEngineFindTextResult& res) {
                if (res.numberOfMatches() == 0) {
                    find_count_lbl_->setText(QStringLiteral("0/0"));
                } else {
                    find_count_lbl_->setText(QStringLiteral("%1/%2")
                                                 .arg(res.activeMatch())
                                                 .arg(res.numberOfMatches()));
                }
            });
        }
    };

    connect(find_input_, &finn::ui::FinnSearchField::textChanged, this,
            [run_find](const QString&) { run_find({}); });
    connect(find_input_, &finn::ui::FinnSearchField::submitted, this,
            [run_find](const QString&) { run_find({}); });
    connect(next_btn, &QAbstractButton::clicked, this, [run_find]() { run_find({}); });
    connect(prev_btn, &QAbstractButton::clicked, this,
            [run_find]() { run_find(QWebEnginePage::FindBackward); });
    connect(close_btn, &QAbstractButton::clicked, this, [this]() {
        find_bar_->hide();
        if (auto* v = activeWebView()) v->page()->findText(QString());
    });
}

void BrowserWindow::setupDownloadsPanel() {
    downloads_panel_ = new finn::ui::FinnGlassPanel(finn::ui::kRadiusXl, content_root_);
    downloads_panel_->setFixedWidth(340);
    downloads_panel_->hide();

    auto* col = new QVBoxLayout(downloads_panel_);
    col->setContentsMargins(14, 12, 14, 12);
    col->setSpacing(8);

    auto* hdr = new QHBoxLayout();
    hdr->addWidget(new finn::ui::FinnLabel(
        QStringLiteral("Downloads"), finn::ui::FinnLabel::Role::Headline,
        finn::ui::FinnLabel::Tone::Ink, downloads_panel_));
    hdr->addStretch(1);
    auto* close_btn = new finn::ui::FinnIconButton(QStringLiteral("x"), 24, 12, downloads_panel_);
    connect(close_btn, &QAbstractButton::clicked, downloads_panel_, &QWidget::hide);
    hdr->addWidget(close_btn);
    col->addLayout(hdr);
    col->addWidget(new finn::ui::FinnDivider(Qt::Horizontal, downloads_panel_));

    downloads_list_layout_ = new QVBoxLayout();
    downloads_list_layout_->setSpacing(6);
    col->addLayout(downloads_list_layout_);
}

void BrowserWindow::setupShortcuts() {
    auto bind = [this](const QKeySequence& seq, const std::function<void()>& fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    bind(QKeySequence(Qt::CTRL | Qt::Key_T), [this]() {
        const BrowserTab* tab = findTab(active_tab_id_);
        command_bar_->open(CommandBarOverlay::Mode::NewTab,
                           tab ? tab->url : QUrl(), spaces_, tabs_);
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_L), [this]() {
        const BrowserTab* tab = findTab(active_tab_id_);
        command_bar_->open(CommandBarOverlay::Mode::EditCurrentUrl,
                           tab ? tab->url : QUrl(), spaces_, tabs_);
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_W), [this]() {
        if (!active_tab_id_.isEmpty()) closeTab(active_tab_id_);
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_R), [this]() {
        if (auto* v = activeWebView()) v->reload();
    });
    bind(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), [this]() {
        if (auto* v = activeWebView()) v->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_S), [this]() {
        toggleSidebarAnimated();
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_B), [this]() {
        toggleSidebarAnimated();
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_F), [this]() {
        find_bar_->setVisible(true);
        positionFloatingPanels();
        find_bar_->raise();
        find_input_->focusInput();
    });
    bind(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [this]() {
        toggleSplitView();
    });
    bind(QKeySequence(Qt::ALT | Qt::Key_Left), [this]() {
        if (auto* v = activeWebView()) v->back();
    });
    bind(QKeySequence(Qt::ALT | Qt::Key_Right), [this]() {
        if (auto* v = activeWebView()) v->forward();
    });
    bind(QKeySequence(Qt::Key_Escape), [this]() {
        if (command_bar_->isVisible()) {
            command_bar_->dismiss();
        } else if (find_bar_->isVisible()) {
            find_bar_->hide();
            if (auto* v = activeWebView()) v->page()->findText(QString());
        } else if (downloads_panel_->isVisible()) {
            downloads_panel_->hide();
        }
    });
}

FlourishWebView* BrowserWindow::ensureWebView(const QString& tab_id) {
    if (auto* existing = views_.value(tab_id, nullptr)) {
        return existing;
    }

    auto* view = new FlourishWebView(
        profile_,
        [this](QWebEnginePage::WebWindowType) -> QWebEngineView* {
            const QString id = new_tab_id();
            tabs_.push_back({id, active_space_id_, QUrl(QStringLiteral("about:blank")),
                             QStringLiteral("New Tab"), false, false, true, 10, {}});
            auto* child_view = ensureWebView(id);
            activateTab(id);
            return child_view;
        },
        primary_stack_);
    views_.insert(tab_id, view);
    primary_stack_->addWidget(view);

    connect(view, &QWebEngineView::loadStarted, this, [this, tab_id]() {
        if (auto* t = findTab(tab_id)) {
            t->loading = true;
            t->load_progress = 10;
            syncUi();
        }
    });
    connect(view, &QWebEngineView::loadProgress, this, [this, tab_id](int progress) {
        if (auto* t = findTab(tab_id)) {
            t->load_progress = progress;
            if (tab_id == active_tab_id_) {
                load_bar_->setVisible(progress < 100);
                load_bar_->setValue(static_cast<double>(progress) / 100.0);
            }
        }
    });
    connect(view, &QWebEngineView::loadFinished, this, [this, tab_id](bool ok) {
        (void)ok;
        if (auto* t = findTab(tab_id)) {
            t->loading = false;
            t->load_progress = 100;
            if (tab_id == active_tab_id_) {
                load_bar_->setVisible(false);
            }
            syncUi();
            SessionStore::save(spaces_, tabs_, active_space_id_);
        }
    });
    connect(view, &QWebEngineView::titleChanged, this, [this, tab_id](const QString& title) {
        if (auto* t = findTab(tab_id)) {
            if (t->url.scheme() != QStringLiteral("flourish")) {
                t->title = title;
            }
            syncUi();
        }
    });
    connect(view, &QWebEngineView::urlChanged, this, [this, tab_id](const QUrl& url) {
        if (auto* t = findTab(tab_id)) {
            if (url.scheme() != QStringLiteral("data")) {
                t->url = url;
            }
            syncUi();
        }
    });
    connect(view, &QWebEngineView::iconChanged, this, [this, tab_id](const QIcon& icon) {
        if (auto* t = findTab(tab_id)) {
            t->favicon = icon;
            syncUi();
        }
    });

    if (const BrowserTab* tab = findTab(tab_id)) {
        navigateTab(tab_id, tab->url);
    }
    return view;
}

FlourishWebView* BrowserWindow::activeWebView() const {
    return views_.value(active_tab_id_, nullptr);
}

BrowserTab* BrowserWindow::findTab(const QString& tab_id) {
    for (auto& t : tabs_) {
        if (t.id == tab_id) return &t;
    }
    return nullptr;
}

const BrowserSpace* BrowserWindow::findSpace(const QString& space_id) const {
    for (const auto& s : spaces_) {
        if (s.id == space_id) return &s;
    }
    return nullptr;
}

void BrowserWindow::loadStartPageInto(FlourishWebView* view, const QString& space_id) {
    const BrowserSpace* space = findSpace(space_id);
    const auto& tok = finn::ui::FinnThemeManager::instance().tokens();
    const BrowserSpace fallback{space_id, QStringLiteral("Personal"),
                                QStringLiteral("home-09"), tok.accent};
    QVector<BrowserTab> favs;
    for (const auto& t : tabs_) {
        if (t.space_id == space_id && t.favorite) favs.push_back(t);
    }
    const QString html = render_start_page_html(
        tok, space ? *space : fallback, favs);
    view->setHtml(html, QUrl(QStringLiteral("flourish://home")));
}

void BrowserWindow::navigateTab(const QString& tab_id, const QUrl& url) {
    BrowserTab* tab = findTab(tab_id);
    if (!tab) return;
    tab->url = url;

    FlourishWebView* view = ensureWebView(tab_id);
    if (url.scheme() == QStringLiteral("flourish")) {
        tab->title = QStringLiteral("New Tab");
        loadStartPageInto(view, tab->space_id);
    } else {
        view->load(url);
    }
    syncUi();
    SessionStore::save(spaces_, tabs_, active_space_id_);
}

void BrowserWindow::openUrlInNewTab(const QUrl& url, bool switch_to) {
    const QString id = new_tab_id();
    const QUrl target = url.isValid() ? url : QUrl(QStringLiteral("flourish://home"));
    tabs_.push_back({id, active_space_id_, target,
                     target.scheme() == QStringLiteral("flourish")
                         ? QStringLiteral("New Tab")
                         : display_host_for_url(target),
                     false, false, false, 100, {}});
    ensureWebView(id);
    if (switch_to) {
        activateTab(id);
    } else {
        syncUi();
    }
    SessionStore::save(spaces_, tabs_, active_space_id_);
}

void BrowserWindow::activateTab(const QString& tab_id) {
    const BrowserTab* tab = findTab(tab_id);
    if (!tab) return;

    if (tab->space_id != active_space_id_) {
        active_space_id_ = tab->space_id;
    }
    active_tab_id_ = tab_id;

    FlourishWebView* view = ensureWebView(tab_id);
    if (view->parentWidget() != primary_stack_) {
        split_stack_->removeWidget(view);
        primary_stack_->addWidget(view);
    }
    primary_stack_->setCurrentWidget(view);

    if (split_tab_id_ == active_tab_id_) {
        split_tab_id_.clear();
        split_stack_->setVisible(false);
    }
    syncUi();
}

void BrowserWindow::closeTab(const QString& tab_id) {
    int idx = -1;
    for (int i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == tab_id) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    const QString space_id = tabs_[idx].space_id;
    tabs_.removeAt(idx);

    if (auto* v = views_.take(tab_id)) {
        primary_stack_->removeWidget(v);
        split_stack_->removeWidget(v);
        v->deleteLater();
    }

    if (split_tab_id_ == tab_id) {
        split_tab_id_.clear();
        split_stack_->setVisible(false);
    }

    if (active_tab_id_ == tab_id) {
        QString next_id;
        for (const auto& t : tabs_) {
            if (t.space_id == space_id) {
                next_id = t.id;
                break;
            }
        }
        if (next_id.isEmpty()) {
            openUrlInNewTab(QUrl(QStringLiteral("flourish://home")), true);
            return;
        }
        activateTab(next_id);
    } else {
        syncUi();
    }
    SessionStore::save(spaces_, tabs_, active_space_id_);
}

void BrowserWindow::togglePinTab(const QString& tab_id) {
    if (auto* t = findTab(tab_id)) {
        t->pinned = !t->pinned;
        syncUi();
        SessionStore::save(spaces_, tabs_, active_space_id_);
    }
}

void BrowserWindow::toggleFavoriteTab(const QString& tab_id) {
    if (auto* t = findTab(tab_id)) {
        t->favorite = !t->favorite;
        if (t->favorite) t->pinned = true;
        syncUi();
        SessionStore::save(spaces_, tabs_, active_space_id_);
    }
}

void BrowserWindow::toggleSplitView(const QString& tab_id) {
    if (!split_tab_id_.isEmpty() && (tab_id.isEmpty() || tab_id == split_tab_id_)) {
        if (auto* v = views_.value(split_tab_id_, nullptr)) {
            split_stack_->removeWidget(v);
            primary_stack_->addWidget(v);
        }
        split_tab_id_.clear();
        split_stack_->setVisible(false);
        syncUi();
        return;
    }

    QString target = tab_id;
    if (target.isEmpty() || target == active_tab_id_) {
        for (const auto& t : tabs_) {
            if (t.space_id == active_space_id_ && t.id != active_tab_id_) {
                target = t.id;
                break;
            }
        }
    }
    if (target.isEmpty() || target == active_tab_id_) {
        openUrlInNewTab(QUrl(QStringLiteral("flourish://home")), false);
        target = tabs_.last().id;
    }

    split_tab_id_ = target;
    FlourishWebView* split_view = ensureWebView(split_tab_id_);
    primary_stack_->removeWidget(split_view);
    split_stack_->addWidget(split_view);
    split_stack_->setCurrentWidget(split_view);
    split_stack_->setVisible(true);
    splitter_->setSizes({width() / 2, width() / 2});
    syncUi();
}

void BrowserWindow::duplicateTab(const QString& tab_id) {
    if (const auto* t = findTab(tab_id)) {
        openUrlInNewTab(t->url, true);
    }
}

void BrowserWindow::switchSpace(const QString& space_id) {
    active_space_id_ = space_id;
    QString first_tab;
    for (const auto& t : tabs_) {
        if (t.space_id == space_id) {
            first_tab = t.id;
            break;
        }
    }
    if (first_tab.isEmpty()) {
        openUrlInNewTab(QUrl(QStringLiteral("flourish://home")), true);
    } else {
        activateTab(first_tab);
    }
    SessionStore::save(spaces_, tabs_, active_space_id_);
}

void BrowserWindow::createNewSpace() {
    const QString name = finn::ui::FinnDialog::prompt(
        this, QStringLiteral("New Space"), QStringLiteral("Space name"));
    if (name.trimmed().isEmpty()) return;

    const auto presets = finn::ui::FinnThemeManager::avatarPresets();
    const QColor accent = presets.isEmpty()
                              ? finn::ui::FinnThemeManager::instance().tokens().accent
                              : presets[spaces_.size() % presets.size()].top_color;
    const QString id = new_tab_id();
    spaces_.push_back({id, name.trimmed(), QStringLiteral("folder"), accent});
    switchSpace(id);
}

void BrowserWindow::syncUi() {
    sidebar_->setState(spaces_, active_space_id_, tabs_, active_tab_id_, split_tab_id_);

    const BrowserTab* tab = findTab(active_tab_id_);
    const auto* view = activeWebView();
    const bool can_back = view && view->history()->canGoBack();
    const bool can_fwd = view && view->history()->canGoForward();
    const bool loading = tab && tab->loading;
    bool active_dl = false;
    for (const auto& d : downloads_) {
        if (!d.finished && !d.cancelled) {
            active_dl = true;
            break;
        }
    }

    const QUrl url = tab ? tab->url : QUrl();
    sidebar_->setNavigationState(url, can_back, can_fwd, loading, active_dl);

    if (const auto* space = findSpace(active_space_id_)) {
        space_badge_->setText(space->name);
    }
    const QString title = tab && !tab->title.isEmpty() ? tab->title : QStringLiteral("Flourish");
    page_title_lbl_->setText(title);
    setWindowTitleText(title + QStringLiteral(" — Flourish"));

    mini_back_btn_->setEnabled(can_back);
    mini_fwd_btn_->setEnabled(can_fwd);
    mini_reload_btn_->setIconName(loading ? QStringLiteral("x") : QStringLiteral("refresh"));
    mini_url_btn_->setText(display_host_for_url(url));
    split_btn_->setActive(!split_tab_id_.isEmpty());
}

void BrowserWindow::syncTopBarForSidebar() {
    mini_nav_group_->setVisible(isSidebarCollapsed());
}

void BrowserWindow::toggleSidebarAnimated() {
    const bool collapsing = !isSidebarCollapsed();
    if (finn::ui::FinnThemeManager::instance().tokens().reduced_motion) {
        setSidebarCollapsed(collapsing);
        syncTopBarForSidebar();
        return;
    }

    if (!collapsing) {
        sidebar_->setMaximumWidth(0);
        setSidebarCollapsed(false);
        syncTopBarForSidebar();
    }

    auto* anim = new QPropertyAnimation(sidebar_, "maximumWidth", this);
    anim->setDuration(200);
    anim->setStartValue(collapsing ? 256 : 0);
    anim->setEndValue(collapsing ? 0 : 256);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QAbstractAnimation::finished, this, [this, collapsing]() {
        if (collapsing) {
            setSidebarCollapsed(true);
            syncTopBarForSidebar();
        }
        sidebar_->setMaximumWidth(256);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void BrowserWindow::showIntroOverlay() {
    if (intro_) {
        intro_->deleteLater();
        intro_ = nullptr;
    }
    intro_ = new IntroOverlay(spaces_, active_space_id_, this);
    const bool max = isMaximized() || isFullScreen();
    intro_->syncToWindowBounds(max ? rect() : rect().adjusted(10, 10, -10, -10), max);
    connect(intro_, &IntroOverlay::minimizeRequested, this, &QWidget::showMinimized);
    connect(intro_, &IntroOverlay::maximizeRequested, this, &FinnWindow::toggleMaximized);
    connect(intro_, &IntroOverlay::closeRequested, this, &QWidget::close);
    connect(intro_, &IntroOverlay::finished, this, [this](const QString& space_id) {
        intro_ = nullptr;
        if (!space_id.isEmpty() && space_id != active_space_id_) {
            switchSpace(space_id);
        }
        SessionStore::save(spaces_, tabs_, active_space_id_);
    });
    intro_->playEntrance();
}

void BrowserWindow::syncWebDarkMode() {
    const bool dark = finn::ui::FinnThemeManager::instance().isDark();
    profile_->settings()->setAttribute(QWebEngineSettings::ForceDarkMode, dark);
}

void BrowserWindow::handleDownload(QWebEngineDownloadRequest* download) {
    const QString dl_dir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(dl_dir);
    const QString fname = download->downloadFileName();
    download->setDownloadDirectory(dl_dir);
    download->accept();

    const int id = next_download_id_++;
    downloads_.prepend({id, fname, dl_dir + QLatin1Char('/') + fname,
                        0, download->totalBytes(), false, false});
    showToast(QStringLiteral("Downloading %1").arg(fname),
              finn::ui::FinnToast::Severity::Info);
    syncUi();

    connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, this,
            [this, id, download]() {
                for (auto& d : downloads_) {
                    if (d.id == id) {
                        d.received_bytes = download->receivedBytes();
                        d.total_bytes = download->totalBytes();
                        break;
                    }
                }
                if (downloads_panel_->isVisible()) refreshDownloadsList();
            });
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this,
            [this, id, download]() {
                for (auto& d : downloads_) {
                    if (d.id == id) {
                        d.finished = download->isFinished();
                        d.cancelled =
                            download->state() == QWebEngineDownloadRequest::DownloadCancelled;
                        break;
                    }
                }
                if (download->state() == QWebEngineDownloadRequest::DownloadCompleted) {
                    showToast(QStringLiteral("Downloaded %1").arg(download->downloadFileName()),
                              finn::ui::FinnToast::Severity::Success);
                }
                syncUi();
                if (downloads_panel_->isVisible()) refreshDownloadsList();
            });
}

void BrowserWindow::refreshDownloadsList() {
    while (QLayoutItem* item = downloads_list_layout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (downloads_.isEmpty()) {
        downloads_list_layout_->addWidget(new finn::ui::FinnEmptyState(
            QStringLiteral("download"),
            QStringLiteral("No downloads yet"),
            QStringLiteral("Files you download appear here and save to ~/Downloads."),
            {}, downloads_panel_));
        downloads_panel_->adjustSize();
        return;
    }

    const int count = std::min<int>(downloads_.size(), 5);
    for (int i = 0; i < count; ++i) {
        const auto& d = downloads_[i];
        auto* card = new finn::ui::FinnCard(finn::ui::kRadiusMd, false, downloads_panel_);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(10, 8, 10, 8);
        row->setSpacing(8);

        auto* meta = new QVBoxLayout();
        meta->setSpacing(2);
        meta->addWidget(new finn::ui::FinnLabel(
            d.filename, finn::ui::FinnLabel::Role::Body,
            finn::ui::FinnLabel::Tone::Ink, card));
        const QString status =
            d.cancelled ? QStringLiteral("Cancelled")
            : d.finished ? QStringLiteral("Completed · %1").arg(format_bytes(d.total_bytes))
                         : QStringLiteral("%1 / %2")
                               .arg(format_bytes(d.received_bytes),
                                    format_bytes(d.total_bytes));
        meta->addWidget(new finn::ui::FinnLabel(
            status, finn::ui::FinnLabel::Role::Caption,
            finn::ui::FinnLabel::Tone::Dim, card));
        row->addLayout(meta, 1);

        if (d.finished && !d.cancelled) {
            auto* open_btn = new finn::ui::FinnButton(
                QStringLiteral("Open"), finn::ui::FinnButton::Variant::Secondary, {}, card);
            const QString path = d.path;
            connect(open_btn, &QAbstractButton::clicked, this, [path]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
            row->addWidget(open_btn);
        }
        downloads_list_layout_->addWidget(card);
    }
    downloads_panel_->adjustSize();
}

void BrowserWindow::positionFloatingPanels() {
    if (find_bar_ && find_bar_->isVisible()) {
        find_bar_->move(std::max(12, content_root_->width() - find_bar_->width() - 16), 46);
    }
    if (downloads_panel_ && downloads_panel_->isVisible()) {
        downloads_panel_->move(16, 46);
    }
}

void BrowserWindow::resizeEvent(QResizeEvent* event) {
    finn::ui::FinnWindow::resizeEvent(event);
    if (command_bar_ && command_bar_->isVisible()) {
        command_bar_->setGeometry(rect());
    }
    if (intro_ && intro_->isVisible()) {
        const bool max = isMaximized() || isFullScreen();
        intro_->syncToWindowBounds(max ? rect() : rect().adjusted(10, 10, -10, -10), max);
    }
    positionFloatingPanels();
}

}  // namespace flourish

#include "browser_window.moc"
