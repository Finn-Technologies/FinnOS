// C++20 File Manager backend implementation for finn-files.

#include "files_controller.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace finn::apps {

FilesController::FilesController(QObject* parent) : QAbstractListModel(parent) {
    loadDirectory(QDir::homePath(), true);
}

int FilesController::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant FilesController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};
    const QFileInfo& fi = entries_[index.row()];
    switch (role) {
        case NameRole: return fi.fileName();
        case PathRole: return fi.absoluteFilePath();
        case IsDirRole: return fi.isDir();
        case SizeTextRole: return fi.isDir() ? QStringLiteral("Folder") : formatSize(fi.size());
        case ModifiedTextRole: return fi.lastModified().toString(QStringLiteral("dd MMM yyyy, HH:mm"));
        case IconNameRole: return iconForEntry(fi);
        case ExtensionRole: return fi.suffix().toUpper();
        default: return {};
    }
}

QHash<int, QByteArray> FilesController::roleNames() const {
    return {
        {NameRole, "name"},
        {PathRole, "filePath"},
        {IsDirRole, "isDir"},
        {SizeTextRole, "sizeText"},
        {ModifiedTextRole, "modifiedText"},
        {IconNameRole, "iconName"},
        {ExtensionRole, "extension"},
    };
}

QString FilesController::currentFolderName() const {
    if (current_path_ == QStringLiteral("/")) return QStringLiteral("Root");
    const QString name = QFileInfo(current_path_).fileName();
    return name.isEmpty() ? current_path_ : name;
}

QVariantList FilesController::breadcrumbs() const {
    QVariantList out;
    const QString clean = QDir::cleanPath(current_path_);
    const QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString acc = QStringLiteral("/");
    QVariantMap root_crumb;
    root_crumb.insert(QStringLiteral("label"), QStringLiteral("/"));
    root_crumb.insert(QStringLiteral("path"), QStringLiteral("/"));
    out.append(root_crumb);
    for (const QString& part : parts) {
        acc = (acc == QStringLiteral("/")) ? (acc + part) : (acc + QLatin1Char('/') + part);
        QVariantMap m;
        m.insert(QStringLiteral("label"), part);
        m.insert(QStringLiteral("path"), acc);
        out.append(m);
    }
    return out;
}

bool FilesController::canGoUp() const {
    QDir d(current_path_);
    return d.cdUp();
}

void FilesController::setShowHidden(bool show) {
    if (show == show_hidden_) return;
    show_hidden_ = show;
    emit filterChanged();
    refresh();
}

void FilesController::setFilterText(const QString& text) {
    if (text == filter_text_) return;
    filter_text_ = text;
    emit filterChanged();
    refresh();
}

void FilesController::setSortBy(const QString& mode) {
    if (mode == sort_by_) return;
    sort_by_ = mode;
    emit filterChanged();
    refresh();
}

void FilesController::setSelectedIndex(int idx) {
    if (idx < -1 || idx >= entries_.size()) idx = -1;
    if (idx == selected_index_) return;
    selected_index_ = idx;
    emit selectionChanged();
}

QVariantMap FilesController::selectedItem() const {
    QVariantMap m;
    if (selected_index_ < 0 || selected_index_ >= entries_.size()) return m;
    const QFileInfo& fi = entries_[selected_index_];
    m.insert(QStringLiteral("name"), fi.fileName());
    m.insert(QStringLiteral("path"), fi.absoluteFilePath());
    m.insert(QStringLiteral("isDir"), fi.isDir());
    m.insert(QStringLiteral("sizeText"), fi.isDir() ? QStringLiteral("Folder") : formatSize(fi.size()));
    m.insert(QStringLiteral("modifiedText"), fi.lastModified().toString(QStringLiteral("dd MMM yyyy, HH:mm")));
    m.insert(QStringLiteral("iconName"), iconForEntry(fi));
    m.insert(QStringLiteral("permissions"),
             QStringLiteral("%1%2%3")
                 .arg(fi.isReadable() ? QStringLiteral("r") : QStringLiteral("-"))
                 .arg(fi.isWritable() ? QStringLiteral("w") : QStringLiteral("-"))
                 .arg(fi.isExecutable() ? QStringLiteral("x") : QStringLiteral("-")));
    return m;
}

void FilesController::navigateTo(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        emit toastRequested(QStringLiteral("Folder not found: %1").arg(path), QStringLiteral("warning"));
        return;
    }
    loadDirectory(fi.absoluteFilePath(), true);
}

void FilesController::navigatePlace(const QString& placeId) {
    QString target = QDir::homePath();
    if (placeId == QLatin1String("desktop")) {
        target = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    } else if (placeId == QLatin1String("documents")) {
        target = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    } else if (placeId == QLatin1String("downloads")) {
        target = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    } else if (placeId == QLatin1String("pictures")) {
        target = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    } else if (placeId == QLatin1String("music")) {
        target = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    } else if (placeId == QLatin1String("root")) {
        target = QStringLiteral("/");
    }
    if (target.isEmpty() || !QDir(target).exists()) target = QDir::homePath();
    loadDirectory(target, true);
}

void FilesController::goBack() {
    if (!canGoBack()) return;
    --history_index_;
    loadDirectory(history_[history_index_], false);
    emit historyChanged();
}

void FilesController::goForward() {
    if (!canGoForward()) return;
    ++history_index_;
    loadDirectory(history_[history_index_], false);
    emit historyChanged();
}

void FilesController::goUp() {
    QDir d(current_path_);
    if (d.cdUp()) {
        loadDirectory(d.absolutePath(), true);
    }
}

void FilesController::refresh() {
    loadDirectory(current_path_, false);
}

void FilesController::activateIndex(int idx) {
    if (idx < 0 || idx >= entries_.size()) return;
    const QFileInfo& fi = entries_[idx];
    if (fi.isDir()) {
        loadDirectory(fi.absoluteFilePath(), true);
    } else {
        const bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
        if (!ok) {
            emit toastRequested(QStringLiteral("No handler registered for %1").arg(fi.fileName()),
                                QStringLiteral("warning"));
        }
    }
}

void FilesController::createFolder(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))) {
        emit toastRequested(QStringLiteral("Invalid folder name"), QStringLiteral("warning"));
        return;
    }
    QDir d(current_path_);
    if (d.mkdir(trimmed)) {
        refresh();
        emit toastRequested(QStringLiteral("Created folder \"%1\"").arg(trimmed), QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Could not create folder \"%1\"").arg(trimmed), QStringLiteral("error"));
    }
}

void FilesController::renameSelected(const QString& newName) {
    if (selected_index_ < 0 || selected_index_ >= entries_.size()) return;
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))) {
        emit toastRequested(QStringLiteral("Invalid name"), QStringLiteral("warning"));
        return;
    }
    const QFileInfo fi = entries_[selected_index_];
    QDir d(current_path_);
    if (d.rename(fi.fileName(), trimmed)) {
        refresh();
        emit toastRequested(QStringLiteral("Renamed to \"%1\"").arg(trimmed), QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Could not rename \"%1\"").arg(fi.fileName()), QStringLiteral("error"));
    }
}

void FilesController::copySelected() {
    if (selected_index_ < 0 || selected_index_ >= entries_.size()) return;
    clipboard_path_ = entries_[selected_index_].absoluteFilePath();
    clipboard_cut_ = false;
    emit clipboardChanged();
    emit toastRequested(QStringLiteral("Copied \"%1\"").arg(entries_[selected_index_].fileName()), QStringLiteral("info"));
}

void FilesController::cutSelected() {
    if (selected_index_ < 0 || selected_index_ >= entries_.size()) return;
    clipboard_path_ = entries_[selected_index_].absoluteFilePath();
    clipboard_cut_ = true;
    emit clipboardChanged();
    emit toastRequested(QStringLiteral("Cut \"%1\"").arg(entries_[selected_index_].fileName()), QStringLiteral("info"));
}

void FilesController::pasteHere() {
    if (clipboard_path_.isEmpty()) return;
    const QFileInfo src(clipboard_path_);
    if (!src.exists()) {
        emit toastRequested(QStringLiteral("Source no longer exists"), QStringLiteral("warning"));
        return;
    }
    QString dst = QDir(current_path_).filePath(src.fileName());
    if (QFileInfo::exists(dst)) {
        dst = QDir(current_path_).filePath(
            src.completeBaseName() + QStringLiteral(" (copy)") +
            (src.suffix().isEmpty() ? QString() : QLatin1Char('.') + src.suffix()));
    }
    bool ok = false;
    if (clipboard_cut_) {
        ok = QFile::rename(src.absoluteFilePath(), dst);
        if (ok) {
            clipboard_path_.clear();
            clipboard_cut_ = false;
            emit clipboardChanged();
        }
    } else if (!src.isDir()) {
        ok = QFile::copy(src.absoluteFilePath(), dst);
    } else {
        ok = (QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), src.absoluteFilePath(), dst}) == 0);
    }
    if (ok) {
        refresh();
        emit toastRequested(QStringLiteral("Pasted \"%1\"").arg(QFileInfo(dst).fileName()), QStringLiteral("success"));
    } else {
        emit toastRequested(QStringLiteral("Paste failed"), QStringLiteral("error"));
    }
}

void FilesController::openInTerminal() {
    QString term = QStandardPaths::findExecutable(QStringLiteral("finn-terminal"));
    if (term.isEmpty()) term = QStringLiteral("/usr/local/bin/finn-terminal");
    QProcess::startDetached(term, {}, current_path_);
}

void FilesController::deleteSelected() {
    if (selected_index_ < 0 || selected_index_ >= entries_.size()) return;
    const QFileInfo fi = entries_[selected_index_];
    bool ok = false;
    if (fi.isDir()) {
        ok = QDir(fi.absoluteFilePath()).removeRecursively();
    } else {
        ok = QFile::remove(fi.absoluteFilePath());
    }
    if (ok) {
        selected_index_ = -1;
        refresh();
        emit toastRequested(QStringLiteral("Deleted \"%1\"").arg(fi.fileName()), QStringLiteral("info"));
    } else {
        emit toastRequested(QStringLiteral("Permission denied deleting \"%1\"").arg(fi.fileName()),
                            QStringLiteral("error"));
    }
}

void FilesController::copySelectedPath() {
    QString path = current_path_;
    if (selected_index_ >= 0 && selected_index_ < entries_.size()) {
        path = entries_[selected_index_].absoluteFilePath();
    }
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(path);
    }
    emit toastRequested(QStringLiteral("Copied path to clipboard"), QStringLiteral("success"));
}

void FilesController::loadDirectory(const QString& path, bool push_history) {
    QDir dir(path);
    if (!dir.exists()) return;

    const QString abs_path = dir.absolutePath();
    if (push_history && (history_index_ < 0 || history_[history_index_] != abs_path)) {
        while (history_.size() > history_index_ + 1) history_.removeLast();
        history_.append(abs_path);
        history_index_ = static_cast<int>(history_.size()) - 1;
        emit historyChanged();
    }

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (show_hidden_) filters |= QDir::Hidden;

    QDir::SortFlags sort = QDir::DirsFirst | QDir::IgnoreCase;
    if (sort_by_ == QLatin1String("size")) {
        sort |= QDir::Size;
    } else if (sort_by_ == QLatin1String("date")) {
        sort |= QDir::Time;
    } else {
        sort |= QDir::Name;
    }
    QList<QFileInfo> list = dir.entryInfoList(filters, sort);
    const QString needle = filter_text_.trimmed().toLower();
    if (!needle.isEmpty()) {
        QList<QFileInfo> filtered;
        for (const QFileInfo& fi : list) {
            if (fi.fileName().toLower().contains(needle)) filtered.append(fi);
        }
        list = std::move(filtered);
    }

    beginResetModel();
    current_path_ = abs_path;
    entries_ = std::move(list);
    selected_index_ = entries_.isEmpty() ? -1 : 0;
    endResetModel();

    emit pathChanged();
    emit entriesChanged();
    emit selectionChanged();
}

QString FilesController::iconForEntry(const QFileInfo& fi) {
    if (fi.isDir()) return QStringLiteral("folder");
    const QString ext = fi.suffix().toLower();
    if (ext == QLatin1String("png") || ext == QLatin1String("jpg") ||
        ext == QLatin1String("jpeg") || ext == QLatin1String("webp") ||
        ext == QLatin1String("svg")) {
        return QStringLiteral("image");
    }
    if (ext == QLatin1String("mp3") || ext == QLatin1String("wav") ||
        ext == QLatin1String("flac") || ext == QLatin1String("ogg")) {
        return QStringLiteral("volume-02");
    }
    if (ext == QLatin1String("sh") || ext == QLatin1String("py") ||
        ext == QLatin1String("cpp") || ext == QLatin1String("hpp") ||
        ext == QLatin1String("qml")) {
        return QStringLiteral("code-01");
    }
    return QStringLiteral("file-text");
}

QString FilesController::formatSize(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024) return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

}  // namespace finn::apps
