#ifndef FLOURISH_INTRO_OVERLAY_HPP
#define FLOURISH_INTRO_OVERLAY_HPP

#include <FinnSDK/FinnSDK.hpp>
#include <QString>
#include <QVector>
#include <QWidget>

#include "tab_model.hpp"

class QGraphicsOpacityEffect;

namespace flourish {

/**
 * @brief Full-screen Arc-inspired onboarding view with asymmetric split layout
 *        and live Space preview, styled with FinnTokens.
 */
class IntroOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit IntroOverlay(const QVector<BrowserSpace>& spaces,
                          const QString& active_space_id,
                          QWidget* parent = nullptr);

    void playEntrance();
    void syncToWindowBounds(const QRect& frame_rect, bool maximized);

signals:
    void finished(const QString& chosen_space_id);
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void dismissWithAnimation();

    QVector<BrowserSpace> spaces_;
    QString chosen_space_id_;
    bool maximized_ = false;
    finn::ui::FinnWindowControls* controls_ = nullptr;
    QWidget* preview_ = nullptr;
    QGraphicsOpacityEffect* opacity_ = nullptr;
};

}  // namespace flourish

#endif
