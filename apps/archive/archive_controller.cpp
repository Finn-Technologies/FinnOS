// C++20 Ark-grade Archive Manager implementation for finn-archive.

#include "archive_controller.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace finn::apps {

ArchiveController::ArchiveController(QObject* parent) : QObject(parent) {
    scanLocalArchives();
}

QString ArchiveController::archiveName() const {
    return archive_path_.isEmpty() ? QStringLiteral("No Archive Open")
                                   : QFileInfo(archive_path_).fileName();
}

void ArchiveController::scanLocalArchives() {
    recent_archives_.clear();
    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QDir::homePath(),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
    };
    const QStringList globs = {
        QStringLiteral("*.zip"), QStringLiteral("*.tar.gz"), QStringLiteral("*.tgz"),
        QStringLiteral("*.tar.xz"), QStringLiteral("*.tar.zst"), QStringLiteral("*.tar"),
    };
    QStringList seen;
    for (const QString& d : dirs) {
        if (d.isEmpty() || !QDir(d).exists()) continue;
        for (const QFileInfo& fi : QDir(d).entryInfoList(globs, QDir::Files, QDir::Time)) {
            if (seen.contains(fi.absoluteFilePath())) continue;
            seen.append(fi.absoluteFilePath());
            QVariantMap m;
            m.insert(QStringLiteral("name"), fi.fileName());
            m.insert(QStringLiteral("path"), fi.absoluteFilePath());
            m.insert(QStringLiteral("sizeText"),
                     QStringLiteral("%1 KB").arg(std::max<qint64>(1, fi.size() / 1024)));
            recent_archives_.append(m);
        }
    }
    emit recentChanged();
}

void ArchiveController::openArchive(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) {
        emit toastRequested(QStringLiteral("Archive not found"), QStringLiteral("warning"));
        return;
    }
    archive_path_ = fi.absoluteFilePath();
    entries_.clear();

    QProcess p;
    if (archive_path_.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        p.start(QStringLiteral("unzip"), {QStringLiteral("-Z1"), archive_path_});
    } else {
        p.start(QStringLiteral("tar"), {QStringLiteral("-tf"), archive_path_});
    }
    if (p.waitForFinished(5000) && p.exitCode() == 0) {
        const QStringList lines =
            QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QVariantMap m;
            m.insert(QStringLiteral("name"), line);
            m.insert(QStringLiteral("isDir"), line.endsWith(QLatin1Char('/')));
            entries_.append(m);
        }
    }
    emit archiveChanged();
}

void ArchiveController::extractToDownloads() {
    if (archive_path_.isEmpty()) return;
    const QFileInfo fi(archive_path_);
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) base = QDir::homePath();
    const QString out_dir = QDir(base).filePath(fi.completeBaseName());
    QDir().mkpath(out_dir);

    int rc = -1;
    if (archive_path_.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        rc = QProcess::execute(QStringLiteral("unzip"),
                               {QStringLiteral("-o"), archive_path_, QStringLiteral("-d"), out_dir});
    } else {
        rc = QProcess::execute(QStringLiteral("tar"),
                               {QStringLiteral("-xf"), archive_path_, QStringLiteral("-C"), out_dir});
    }
    if (rc == 0) {
        emit toastRequested(QStringLiteral("Extracted to %1").arg(out_dir), QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Extraction failed"), QStringLiteral("error"));
    }
}

void ArchiveController::createArchiveFromHome(const QString& name) {
    const QString clean = name.trimmed().isEmpty() ? QStringLiteral("Documents-backup") : name.trimmed();
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) base = QDir::homePath();
    QDir().mkpath(base);
    const QString out_path = QDir(base).filePath(clean + QStringLiteral(".tar.gz"));
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString src = (!docs.isEmpty() && QDir(docs).exists()) ? docs : QDir::homePath();
    const QFileInfo sfi(src);
    const int rc = QProcess::execute(
        QStringLiteral("tar"),
        {QStringLiteral("-czf"), out_path, QStringLiteral("-C"), sfi.absolutePath(), sfi.fileName()});
    if (rc == 0) {
        scanLocalArchives();
        openArchive(out_path);
        emit toastRequested(QStringLiteral("Created %1").arg(QFileInfo(out_path).fileName()),
                            QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Could not create archive"), QStringLiteral("error"));
    }
}

}  // namespace finn::apps
