// C++20 Image & SVG Viewer backend for finn-viewer.

#ifndef FINN_APPS_VIEWER_CONTROLLER_HPP
#define FINN_APPS_VIEWER_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QVariantList>

namespace finn::apps {

class ViewerController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList gallery READ gallery NOTIFY galleryChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentUrl READ currentUrl NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QString dimensionsText READ dimensionsText NOTIFY currentChanged)
    Q_PROPERTY(QString fileSizeText READ fileSizeText NOTIFY currentChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY transformChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY transformChanged)
    Q_PROPERTY(bool flipH READ flipH NOTIFY transformChanged)
    Q_PROPERTY(bool flipV READ flipV NOTIFY transformChanged)

public:
    explicit ViewerController(QObject* parent = nullptr);

    QVariantList gallery() const { return gallery_; }
    int currentIndex() const { return current_index_; }
    void setCurrentIndex(int idx);

    QString currentUrl() const { return current_url_; }
    QString currentName() const { return current_name_; }
    QString dimensionsText() const { return dims_text_; }
    QString fileSizeText() const { return size_text_; }
    double zoom() const { return zoom_; }
    void setZoom(double z);
    int rotation() const { return rotation_; }
    bool flipH() const { return flip_h_; }
    bool flipV() const { return flip_v_; }

    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void nextImage();
    Q_INVOKABLE void prevImage();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void rotateLeft();
    Q_INVOKABLE void rotateRight();
    Q_INVOKABLE void toggleFlipH();
    Q_INVOKABLE void toggleFlipV();

signals:
    void galleryChanged();
    void currentChanged();
    void transformChanged();

private:
    void scanFolder(const QString& dir_path, const QString& select_file = {});
    void updateCurrentMeta();

    QVariantList gallery_;
    int current_index_ = -1;
    QString current_url_;
    QString current_name_;
    QString dims_text_;
    QString size_text_;
    double zoom_ = 1.0;
    int rotation_ = 0;
    bool flip_h_ = false;
    bool flip_v_ = false;
};

}  // namespace finn::apps

#endif  // FINN_APPS_VIEWER_CONTROLLER_HPP
