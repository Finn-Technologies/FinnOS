// C++20 Image & SVG Viewer backend implementation for finn-viewer.

#include "viewer_controller.hpp"
#include "common/paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

namespace finn::apps {

ViewerController::ViewerController(QObject* parent) : QObject(parent) {
    const QStringList candidates = {
        QString::fromLatin1(finn::kWallpaperDir),
        QStringLiteral("assets/wallpaper"),
        QStringLiteral("assets/figma/wallpaper"),
    };
    for (const QString& dir : candidates) {
        if (QDir(dir).exists()) {
            scanFolder(dir);
            if (!gallery_.isEmpty()) break;
        }
    }
}

void ViewerController::openPath(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;
    if (fi.isDir()) {
        scanFolder(fi.absoluteFilePath());
    } else {
        scanFolder(fi.absolutePath(), fi.absoluteFilePath());
    }
}

void ViewerController::scanFolder(const QString& dir_path, const QString& select_file) {
    QDir d(dir_path);
    const QFileInfoList files = d.entryInfoList(
        {QStringLiteral("*.webp"), QStringLiteral("*.png"), QStringLiteral("*.jpg"),
         QStringLiteral("*.jpeg"), QStringLiteral("*.svg")},
        QDir::Files, QDir::Name);

    gallery_.clear();
    int target = 0;
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo& fi = files[i];
        QVariantMap m;
        m.insert(QStringLiteral("name"), fi.fileName());
        m.insert(QStringLiteral("path"), fi.absoluteFilePath());
        m.insert(QStringLiteral("url"), QUrl::fromLocalFile(fi.absoluteFilePath()).toString());
        m.insert(QStringLiteral("size"), fi.size());
        gallery_.append(m);
        if (!select_file.isEmpty() && fi.absoluteFilePath() == select_file) {
            target = i;
        }
    }
    emit galleryChanged();
    current_index_ = gallery_.isEmpty() ? -1 : target;
    zoom_ = 1.0;
    rotation_ = 0;
    updateCurrentMeta();
    emit transformChanged();
}

void ViewerController::setCurrentIndex(int idx) {
    if (gallery_.isEmpty()) return;
    const int clamped = std::clamp(idx, 0, static_cast<int>(gallery_.size()) - 1);
    if (clamped == current_index_) return;
    current_index_ = clamped;
    zoom_ = 1.0;
    rotation_ = 0;
    updateCurrentMeta();
    emit transformChanged();
}

void ViewerController::setZoom(double z) {
    const double clamped = std::clamp(z, 0.25, 4.0);
    if (std::abs(clamped - zoom_) < 0.005) return;
    zoom_ = clamped;
    emit transformChanged();
}

void ViewerController::nextImage() {
    if (gallery_.isEmpty()) return;
    setCurrentIndex((current_index_ + 1) % gallery_.size());
}

void ViewerController::prevImage() {
    if (gallery_.isEmpty()) return;
    setCurrentIndex((current_index_ - 1 + gallery_.size()) % gallery_.size());
}

void ViewerController::zoomIn() { setZoom(zoom_ * 1.25); }
void ViewerController::zoomOut() { setZoom(zoom_ / 1.25); }

void ViewerController::resetView() {
    zoom_ = 1.0;
    rotation_ = 0;
    flip_h_ = false;
    flip_v_ = false;
    emit transformChanged();
}

void ViewerController::rotateLeft() {
    rotation_ = (rotation_ + 270) % 360;
    emit transformChanged();
}

void ViewerController::rotateRight() {
    rotation_ = (rotation_ + 90) % 360;
    emit transformChanged();
}

void ViewerController::toggleFlipH() {
    flip_h_ = !flip_h_;
    emit transformChanged();
}

void ViewerController::toggleFlipV() {
    flip_v_ = !flip_v_;
    emit transformChanged();
}

void ViewerController::updateCurrentMeta() {
    if (current_index_ < 0 || current_index_ >= gallery_.size()) {
        current_url_.clear();
        current_name_ = QStringLiteral("No image loaded");
        dims_text_.clear();
        size_text_.clear();
        emit currentChanged();
        return;
    }
    const QVariantMap item = gallery_[current_index_].toMap();
    const QString path = item.value(QStringLiteral("path")).toString();
    current_url_ = item.value(QStringLiteral("url")).toString();
    current_name_ = item.value(QStringLiteral("name")).toString();

    QImageReader reader(path);
    const QSize sz = reader.size();
    dims_text_ = sz.isValid() ? QStringLiteral("%1 x %2").arg(sz.width()).arg(sz.height())
                              : QStringLiteral("Vector");
    const qint64 bytes = item.value(QStringLiteral("size")).toLongLong();
    size_text_ = bytes >= 1024 * 1024
                     ? QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1)
                     : QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    emit currentChanged();
}

}  // namespace finn::apps
