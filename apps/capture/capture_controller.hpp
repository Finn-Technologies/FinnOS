// C++20 Spectacle-grade Screenshot backend for finn-capture.
// Uses finn-comp's native capture IPC (/run/finn/state/capture-request) and
// saves screenshots into ~/Pictures/Screenshots with clipboard copy support.

#ifndef FINN_APPS_CAPTURE_CONTROLLER_HPP
#define FINN_APPS_CAPTURE_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QTimer>

namespace finn::apps {

class CaptureController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastImageUrl READ lastImageUrl NOTIFY captureChanged)
    Q_PROPERTY(QString lastImagePath READ lastImagePath NOTIFY captureChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)

public:
    explicit CaptureController(QObject* parent = nullptr);

    QString lastImageUrl() const { return last_url_; }
    QString lastImagePath() const { return last_path_; }
    bool capturing() const { return capturing_; }

    Q_INVOKABLE void captureNow(int delaySeconds);
    Q_INVOKABLE void copyToClipboard();

signals:
    void captureChanged();
    void capturingChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    void executeCapture();
    void pollCaptureResult();

    QString pending_path_;
    QString last_path_;
    QString last_url_;
    bool capturing_ = false;
    int poll_tries_ = 0;
    QTimer poll_timer_;
};

}  // namespace finn::apps

#endif  // FINN_APPS_CAPTURE_CONTROLLER_HPP
