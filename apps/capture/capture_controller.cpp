// C++20 Spectacle-grade Screenshot implementation for finn-capture.

#include "capture_controller.hpp"
#include "common/paths.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QStandardPaths>
#include <QUrl>

namespace finn::apps {

CaptureController::CaptureController(QObject* parent) : QObject(parent) {
    poll_timer_.setInterval(120);
    connect(&poll_timer_, &QTimer::timeout, this, &CaptureController::pollCaptureResult);
}

void CaptureController::captureNow(int delaySeconds) {
    if (capturing_) return;
    capturing_ = true;
    emit capturingChanged();
    if (delaySeconds > 0) {
        QTimer::singleShot(delaySeconds * 1000, this, &CaptureController::executeCapture);
    } else {
        QTimer::singleShot(250, this, &CaptureController::executeCapture);
    }
}

void CaptureController::executeCapture() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty()) base = QDir::homePath();
    const QString out_dir = QDir(base).filePath(QStringLiteral("Screenshots"));
    QDir().mkpath(out_dir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    pending_path_ = QDir(out_dir).filePath(QStringLiteral("FinnOS-%1.png").arg(stamp));

    const QString req_path = QString::fromLatin1(finn::kCompositorStateDir) +
                             QStringLiteral("/capture-request");
    QFile f(req_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        capturing_ = false;
        emit capturingChanged();
        emit toastRequested(QStringLiteral("Compositor capture IPC unavailable"), QStringLiteral("warning"));
        return;
    }
    f.write((pending_path_ + QLatin1Char('\n')).toUtf8());
    f.close();
    poll_tries_ = 0;
    poll_timer_.start();
}

void CaptureController::pollCaptureResult() {
    ++poll_tries_;
    if (QFileInfo::exists(pending_path_) && QFileInfo(pending_path_).size() > 0) {
        poll_timer_.stop();
        last_path_ = pending_path_;
        last_url_ = QUrl::fromLocalFile(last_path_).toString();
        capturing_ = false;
        emit captureChanged();
        emit capturingChanged();
        emit toastRequested(QStringLiteral("Saved %1").arg(QFileInfo(last_path_).fileName()),
                            QStringLiteral("success"));
        return;
    }
    if (poll_tries_ > 25) {
        poll_timer_.stop();
        capturing_ = false;
        emit capturingChanged();
        emit toastRequested(QStringLiteral("Screenshot timed out"), QStringLiteral("warning"));
    }
}

void CaptureController::copyToClipboard() {
    if (last_path_.isEmpty()) return;
    QImage img(last_path_);
    if (!img.isNull()) {
        if (QClipboard* cb = QGuiApplication::clipboard()) {
            cb->setImage(img);
            emit toastRequested(QStringLiteral("Copied screenshot to clipboard"), QStringLiteral("success"));
        }
    }
}

}  // namespace finn::apps
