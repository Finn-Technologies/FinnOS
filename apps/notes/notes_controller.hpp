// C++20 Notes backend for finn-notes (real Markdown/UTF-8 files on disk).

#ifndef FINN_APPS_NOTES_CONTROLLER_HPP
#define FINN_APPS_NOTES_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QVariantList>

namespace finn::apps {

class NotesController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList notesList READ notesList NOTIFY listChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle WRITE setActiveTitle NOTIFY activeChanged)
    Q_PROPERTY(QString activeContent READ activeContent WRITE setActiveContent NOTIFY activeChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY activeChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY activeChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY activeChanged)
    Q_PROPERTY(QString storageDir READ storageDir CONSTANT)

public:
    explicit NotesController(QObject* parent = nullptr);

    QVariantList notesList() const { return notes_; }
    QString activeId() const { return active_id_; }
    QString activeTitle() const { return active_title_; }
    void setActiveTitle(const QString& title);
    QString activeContent() const { return active_content_; }
    void setActiveContent(const QString& content);
    bool dirty() const { return dirty_; }
    int wordCount() const;
    int lineCount() const;
    QString storageDir() const { return notes_dir_; }

    Q_INVOKABLE void refreshList();
    Q_INVOKABLE void openNote(const QString& id);
    Q_INVOKABLE void createNote();
    Q_INVOKABLE void saveActive();
    Q_INVOKABLE void deleteActive();

signals:
    void listChanged();
    void activeChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    void ensureDefaultNote();

    QString notes_dir_;
    QVariantList notes_;
    QString active_id_;
    QString active_title_;
    QString active_content_;
    bool dirty_ = false;
};

}  // namespace finn::apps

#endif  // FINN_APPS_NOTES_CONTROLLER_HPP
