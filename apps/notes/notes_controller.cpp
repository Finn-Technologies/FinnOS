// C++20 Notes backend implementation for finn-notes.

#include "notes_controller.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

namespace finn::apps {

NotesController::NotesController(QObject* parent) : QObject(parent) {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    notes_dir_ = (docs.isEmpty() ? QDir::homePath() : docs) + QStringLiteral("/FinnNotes");
    QDir().mkpath(notes_dir_);
    ensureDefaultNote();
    refreshList();
    if (!notes_.isEmpty()) {
        openNote(notes_.first().toMap().value(QStringLiteral("id")).toString());
    }
}

void NotesController::ensureDefaultNote() {
    QDir dir(notes_dir_);
    if (!dir.entryList({QStringLiteral("*.md"), QStringLiteral("*.txt")}, QDir::Files).isEmpty()) {
        return;
    }
    QFile f(dir.filePath(QStringLiteral("Welcome.md")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write("# Welcome to Finn Notes\n\n"
                "Finn Notes stores plain UTF-8 Markdown files directly in `~/Documents/FinnNotes`.\n\n"
                "- Adaptive across Desktop, Tablet, Mobile, and TV\n"
                "- Instant local save with zero cloud lock-in\n"
                "- Live word and line counters\n");
    }
}

void NotesController::refreshList() {
    QDir dir(notes_dir_);
    const QFileInfoList files = dir.entryInfoList(
        {QStringLiteral("*.md"), QStringLiteral("*.txt")},
        QDir::Files, QDir::Time);

    notes_.clear();
    for (const QFileInfo& fi : files) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), fi.fileName());
        m.insert(QStringLiteral("label"), fi.completeBaseName());
        m.insert(QStringLiteral("icon"), QStringLiteral("file-text"));
        m.insert(QStringLiteral("modified"), fi.lastModified().toString(QStringLiteral("dd MMM, HH:mm")));
        notes_.append(m);
    }
    emit listChanged();
}

void NotesController::openNote(const QString& id) {
    const QString path = QDir(notes_dir_).filePath(id);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    active_id_ = id;
    active_title_ = QFileInfo(id).completeBaseName();
    active_content_ = QString::fromUtf8(f.readAll());
    dirty_ = false;
    emit activeChanged();
}

void NotesController::setActiveTitle(const QString& title) {
    if (title == active_title_) return;
    active_title_ = title;
    dirty_ = true;
    emit activeChanged();
}

void NotesController::setActiveContent(const QString& content) {
    if (content == active_content_) return;
    active_content_ = content;
    dirty_ = true;
    emit activeChanged();
}

int NotesController::wordCount() const {
    const QString trimmed = active_content_.trimmed();
    if (trimmed.isEmpty()) return 0;
    return trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

int NotesController::lineCount() const {
    if (active_content_.isEmpty()) return 1;
    return active_content_.count(QLatin1Char('\n')) + 1;
}

void NotesController::createNote() {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString fname = QStringLiteral("Note-%1.md").arg(stamp);
    QFile f(QDir(notes_dir_).filePath(fname));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write("# New Note\n\n");
        f.close();
    }
    refreshList();
    openNote(fname);
    emit toastRequested(QStringLiteral("Created new note"), QStringLiteral("success"));
}

void NotesController::saveActive() {
    if (active_id_.isEmpty()) return;
    QString clean_title = active_title_.trimmed();
    clean_title.remove(QLatin1Char('/'));
    if (clean_title.isEmpty()) clean_title = QStringLiteral("Untitled");
    const QString new_id = clean_title + QStringLiteral(".md");
    QDir dir(notes_dir_);

    if (new_id != active_id_ && QFile::exists(dir.filePath(active_id_))) {
        QFile::rename(dir.filePath(active_id_), dir.filePath(new_id));
        active_id_ = new_id;
    }

    QFile f(dir.filePath(active_id_));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        f.write(active_content_.toUtf8());
        f.close();
        dirty_ = false;
        refreshList();
        emit activeChanged();
        emit toastRequested(QStringLiteral("Saved \"%1\"").arg(clean_title), QStringLiteral("success"));
    }
}

void NotesController::deleteActive() {
    if (active_id_.isEmpty()) return;
    const QString removed = active_title_;
    QFile::remove(QDir(notes_dir_).filePath(active_id_));
    active_id_.clear();
    active_title_.clear();
    active_content_.clear();
    dirty_ = false;
    refreshList();
    if (!notes_.isEmpty()) {
        openNote(notes_.first().toMap().value(QStringLiteral("id")).toString());
    } else {
        emit activeChanged();
    }
    emit toastRequested(QStringLiteral("Deleted \"%1\"").arg(removed), QStringLiteral("info"));
}

}  // namespace finn::apps
