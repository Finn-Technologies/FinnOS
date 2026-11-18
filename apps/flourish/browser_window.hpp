#ifndef FLOURISH_BROWSER_WINDOW_HPP
#define FLOURISH_BROWSER_WINDOW_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QHash>
#include <QString>
#include <QUrl>
#include <QVector>

#include "command_bar.hpp"
#include "intro_overlay.hpp"
#include "sidebar_panel.hpp"
#include "tab_model.hpp"

class QSplitter;
class QStackedWidget;
class QVBoxLayout;
class QWebEngineDownloadRequest;
class QWebEngineProfile;
class QWebEngineView;

namespace flourish {

class FlourishWebView;

/**
 * @brief Main Flourish browser window built on FinnWindow (FigmaSplit) + Chromium WebEngine.
 */
class BrowserWindow final : public finn::ui::FinnWindow {
    Q_OBJECT

public:
    explicit BrowserWindow(const QUrl& initial_url = {}, QWidget* parent = nullptr);
    ~BrowserWindow() override;

    void openUrlInNewTab(const QUrl& url, bool switch_to = true);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupShortcuts();
    void setupContentArea();
    void setupFindBar();
    void setupDownloadsPanel();

    FlourishWebView* ensureWebView(const QString& tab_id);
    FlourishWebView* activeWebView() const;
    BrowserTab* findTab(const QString& tab_id);
    const BrowserSpace* findSpace(const QString& space_id) const;

    void navigateTab(const QString& tab_id, const QUrl& url);
    void loadStartPageInto(FlourishWebView* view, const QString& space_id);
    void activateTab(const QString& tab_id);
    void closeTab(const QString& tab_id);
    void togglePinTab(const QString& tab_id);
    void toggleFavoriteTab(const QString& tab_id);
    void toggleSplitView(const QString& tab_id = {});
    void duplicateTab(const QString& tab_id);
    void switchSpace(const QString& space_id);
    void createNewSpace();

    void syncUi();
    void syncTopBarForSidebar();
    void toggleSidebarAnimated();
    void showIntroOverlay();
    void syncWebDarkMode();
    void handleDownload(QWebEngineDownloadRequest* download);
    void refreshDownloadsList();
    void positionFloatingPanels();

    QWebEngineProfile* profile_ = nullptr;
    QVector<BrowserSpace> spaces_;
    QVector<BrowserTab> tabs_;
    QString active_space_id_;
    QString active_tab_id_;
    QString split_tab_id_;

    QHash<QString, FlourishWebView*> views_;
    QVector<DownloadEntry> downloads_;
    int next_download_id_ = 1;

    SidebarPanel* sidebar_ = nullptr;
    CommandBarOverlay* command_bar_ = nullptr;
    IntroOverlay* intro_ = nullptr;

    QWidget* content_root_ = nullptr;
    QWidget* mini_nav_group_ = nullptr;
    finn::ui::FinnIconButton* mini_back_btn_ = nullptr;
    finn::ui::FinnIconButton* mini_fwd_btn_ = nullptr;
    finn::ui::FinnIconButton* mini_reload_btn_ = nullptr;
    finn::ui::FinnButton* mini_url_btn_ = nullptr;
    finn::ui::FinnBadge* space_badge_ = nullptr;
    finn::ui::FinnLabel* page_title_lbl_ = nullptr;
    finn::ui::FinnIconButton* split_btn_ = nullptr;
    finn::ui::FinnIconButton* find_btn_ = nullptr;
    finn::ui::FinnProgressBar* load_bar_ = nullptr;

    QSplitter* splitter_ = nullptr;
    QStackedWidget* primary_stack_ = nullptr;
    QStackedWidget* split_stack_ = nullptr;

    finn::ui::FinnGlassPanel* find_bar_ = nullptr;
    finn::ui::FinnSearchField* find_input_ = nullptr;
    finn::ui::FinnLabel* find_count_lbl_ = nullptr;

    finn::ui::FinnGlassPanel* downloads_panel_ = nullptr;
    QVBoxLayout* downloads_list_layout_ = nullptr;
};

}  // namespace flourish

#endif
