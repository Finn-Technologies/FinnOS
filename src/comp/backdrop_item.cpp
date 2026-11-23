// Real-time compositor backdrop blur QQuickPaintedItem implementation.

#include "comp/backdrop_item.hpp"

#include "comp/server.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace finn::comp {

namespace {

QPainterPath rounded_backdrop_path(const QRectF& rect, qreal radius) {
    const qreal half_min = std::min(rect.width(), rect.height()) * 0.5;
    const qreal r = std::clamp(radius, 0.0, half_min);
    QPainterPath p;
    if (r <= 0.5) {
        p.addRect(rect);
    } else {
        p.addRoundedRect(rect, r, r);
    }
    return p;
}

}  // namespace

CompositorBackdropItem::CompositorBackdropItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setOpaquePainting(false);
    setRenderTarget(QQuickPaintedItem::Image);
}

void CompositorBackdropItem::setServer(QObject* s) {
    if (server_ == s) return;
    server_ = s;
    update();
    emit changed();
}

void CompositorBackdropItem::setWindowId(quint64 id) {
    if (window_id_ == id) return;
    window_id_ = id;
    update();
    emit changed();
}

void CompositorBackdropItem::setScreenX(qreal x) {
    if (std::abs(screen_x_ - x) < 0.5) return;
    screen_x_ = x;
    update();
    emit changed();
}

void CompositorBackdropItem::setScreenY(qreal y) {
    if (std::abs(screen_y_ - y) < 0.5) return;
    screen_y_ = y;
    update();
    emit changed();
}

void CompositorBackdropItem::setRadius(qreal r) {
    if (std::abs(radius_ - r) < 0.1) return;
    radius_ = r;
    update();
    emit changed();
}

void CompositorBackdropItem::setBackdropVersion(int v) {
    if (backdrop_version_ == v) return;
    backdrop_version_ = v;
    update();
    emit changed();
}

void CompositorBackdropItem::geometryChange(const QRectF& newGeometry,
                                            const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void CompositorBackdropItem::paint(QPainter* p) {
    const qreal w = width();
    const qreal h = height();
    if (w <= 0.0 || h <= 0.0 || !server_) return;
    auto* srv = qobject_cast<CompositorServer*>(server_);
    if (!srv) return;

    const QImage blur = srv->sampleBlurredBackdrop(
        window_id_, QRectF(screen_x_, screen_y_, w, h));
    if (blur.isNull()) return;

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF body(0.0, 0.0, w, h);
    p->save();
    p->setClipPath(rounded_backdrop_path(body, radius_));
    p->drawImage(body, blur, QRectF(0.0, 0.0, blur.width(), blur.height()));
    p->restore();
}

}  // namespace finn::comp
