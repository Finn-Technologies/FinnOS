#ifndef FLOURISH_SIDEBAR_PANEL_HPP
#define FLOURISH_SIDEBAR_PANEL_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QColor>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include "tab_model.hpp"

class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QVBoxLayout;

namespace flourish {

/**
 * @brief Arc-inspired vertical browser sidebar styled with FinnUI tokens.
 */
class SidebarPanel final : public QWidget {
    Q_OBJECT

public:
    explicit SidebarPanel(QWidget* parent = nullptr);

    void setState(const QVector<BrowserSpace>& spaces,
                  const QString& active_space_id,
                  const QVector<BrowserTab>& tabs,
                  const QString& active_tab_id,
                  const QString& split_tab_id);

    void setNavigationState(const QUrl& current_url,
                            bool can_back,
                            bool can_forward,
                            bool loading,
                            bool has_active_downloads);

signals:
    void collapseRequested();
    void backRequested();
    void forwardRequested();
    void reloadOrStopRequested();
    void downloadsRequested();
    void commandBarRequested();
    void newTabRequested();
    void tabSelected(const QString& tab_id);
    void tabClosed(const QString& tab_id);
    void tabPinToggled(const QString& tab_id);
    void tabFavoriteToggled(const QString& tab_id);
    void tabSplitRequested(const QString& tab_id);
    void tabDuplicateRequested(const QString& tab_id);
    void tabReloadRequested(const QString& tab_id);
    void spaceSelected(const QString& space_id);
    void newSpaceRequested();

private:
    void rebuildFavorites(const QVector<BrowserTab>& favorites,
                          const QString& active_tab_id,
                          const QColor& space_accent);
    void rebuildTabLists(const QVector<BrowserTab>& space_tabs,
                         const QString& active_tab_id,
                         const QString& split_tab_id,
                         const QColor& space_accent);
    void rebuildSpaces(const QVector<BrowserSpace>& spaces,
                       const QString& active_space_id);

    finn::ui::FinnIconButton* collapse_btn_ = nullptr;
    finn::ui::FinnIconButton* back_btn_ = nullptr;
    finn::ui::FinnIconButton* forward_btn_ = nullptr;
    finn::ui::FinnIconButton* reload_btn_ = nullptr;
    finn::ui::FinnIconButton* downloads_btn_ = nullptr;

    QWidget* url_pill_ = nullptr;
    finn::ui::FinnLabel* url_label_ = nullptr;
    QLabel* url_sec_icon_ = nullptr;

    QWidget* favorites_container_ = nullptr;
    QGridLayout* favorites_grid_ = nullptr;

    QScrollArea* scroll_ = nullptr;
    QWidget* scroll_body_ = nullptr;
    QVBoxLayout* pinned_layout_ = nullptr;
    QVBoxLayout* today_layout_ = nullptr;
    finn::ui::FinnLabel* pinned_header_ = nullptr;

    QHBoxLayout* spaces_layout_ = nullptr;
};

}  // namespace flourish

#endif
