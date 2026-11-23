// Real-time compositor backdrop blur QQuickPaintedItem for finn-comp.
// Renders the live blurred composite of everything strictly below a window
// (wallpaper + all lower Wayland client surfaces) at 1/4 resolution.

#ifndef FINN_COMP_BACKDROP_ITEM_HPP
#define FINN_COMP_BACKDROP_ITEM_HPP

#include <QQuickPaintedItem>

namespace finn::comp {

class CompositorServer;

class CompositorBackdropItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* server READ server WRITE setServer NOTIFY changed)
    Q_PROPERTY(quint64 windowId READ windowId WRITE setWindowId NOTIFY changed)
    Q_PROPERTY(qreal screenX READ screenX WRITE setScreenX NOTIFY changed)
    Q_PROPERTY(qreal screenY READ screenY WRITE setScreenY NOTIFY changed)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY changed)
    Q_PROPERTY(int backdropVersion READ backdropVersion WRITE setBackdropVersion NOTIFY changed)

public:
    explicit CompositorBackdropItem(QQuickItem* parent = nullptr);

    QObject* server() const { return server_; }
    void setServer(QObject* s);

    quint64 windowId() const { return window_id_; }
    void setWindowId(quint64 id);

    qreal screenX() const { return screen_x_; }
    void setScreenX(qreal x);

    qreal screenY() const { return screen_y_; }
    void setScreenY(qreal y);

    qreal radius() const { return radius_; }
    void setRadius(qreal r);

    int backdropVersion() const { return backdrop_version_; }
    void setBackdropVersion(int v);

    void paint(QPainter* p) override;

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

signals:
    void changed();

private:
    QObject* server_ = nullptr;
    quint64 window_id_ = 0;
    qreal screen_x_ = 0.0;
    qreal screen_y_ = 0.0;
    qreal radius_ = 12.0;
    int backdrop_version_ = 0;
};

}  // namespace finn::comp

#endif  // FINN_COMP_BACKDROP_ITEM_HPP
