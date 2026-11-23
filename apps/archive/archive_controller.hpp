// C++20 Ark-grade Archive Manager backend for finn-archive.
// Inspects, extracts, and creates .zip, .tar.gz, .tar.xz, .tar.zst, and .tar archives.

#ifndef FINN_APPS_ARCHIVE_CONTROLLER_HPP
#define FINN_APPS_ARCHIVE_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QVariantList>

namespace finn::apps {

class ArchiveController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString archivePath READ archivePath NOTIFY archiveChanged)
    Q_PROPERTY(QString archiveName READ archiveName NOTIFY archiveChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY archiveChanged)
    Q_PROPERTY(QVariantList recentArchives READ recentArchives NOTIFY recentChanged)

public:
    explicit ArchiveController(QObject* parent = nullptr);

    QString archivePath() const { return archive_path_; }
    QString archiveName() const;
    QVariantList entries() const { return entries_; }
    QVariantList recentArchives() const { return recent_archives_; }

    Q_INVOKABLE void openArchive(const QString& path);
    Q_INVOKABLE void extractToDownloads();
    Q_INVOKABLE void createArchiveFromHome(const QString& name);
    Q_INVOKABLE void scanLocalArchives();

signals:
    void archiveChanged();
    void recentChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    QString archive_path_;
    QVariantList entries_;
    QVariantList recent_archives_;
};

}  // namespace finn::apps

#endif  // FINN_APPS_ARCHIVE_CONTROLLER_HPP
