#ifndef FLOURISH_COMMAND_BAR_HPP
#define FLOURISH_COMMAND_BAR_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include "tab_model.hpp"

class QGraphicsOpacityEffect;
class QVBoxLayout;

namespace flourish {

/**
 * @brief Arc-style floating Command Bar overlay (Ctrl+T / Ctrl+L) built with FinnGlassPanel.
 */
class CommandBarOverlay final : public QWidget {
    Q_OBJECT

public:
    enum class Mode { NewTab, EditCurrentUrl };

    explicit CommandBarOverlay(QWidget* parent = nullptr);

    void open(Mode mode,
              const QUrl& current_url,
              const QVector<BrowserSpace>& spaces,
              const QVector<BrowserTab>& tabs);
    void dismiss();

signals:
    void navigateInCurrentTab(const QUrl& url);
    void openInNewTab(const QUrl& url);
    void switchToTab(const QString& tab_id);
    void switchToSpace(const QString& space_id);
    void actionRequested(const QString& action_id);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct Entry {
        enum class Kind { Navigate, SwitchTab, SwitchSpace, Action };
        Kind kind = Kind::Navigate;
        QString label;
        QString subtitle;
        QString badge;
        QString target_id;
        QUrl url;
    };

    void rebuildSuggestions(const QString& query);
    void executeSelected();
    void renderRows();

    Mode mode_ = Mode::NewTab;
    QVector<BrowserSpace> spaces_;
    QVector<BrowserTab> tabs_;
    QVector<Entry> entries_;
    int selected_index_ = 0;

    finn::ui::FinnGlassPanel* panel_ = nullptr;
    QGraphicsOpacityEffect* panel_opacity_ = nullptr;
    finn::ui::FinnSearchField* input_ = nullptr;
    QVBoxLayout* rows_layout_ = nullptr;
};

}  // namespace flourish

#endif
