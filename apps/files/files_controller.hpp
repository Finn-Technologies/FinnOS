// C++20 File Manager backend for finn-files.
// Real filesystem browsing, history, search, mkdir, rename, delete, and launch.

#ifndef FINN_APPS_FILES_CONTROLLER_HPP
#define FINN_APPS_FILES_CONTROLLER_HPP

#include <QAbstractListModel>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace finn::apps {

class FilesController : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString currentPath READ currentPath NOTIFY pathChanged)
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY pathChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY pathChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY pathChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY filterChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    Q_PROPERTY(bool clipboardIsCut READ clipboardIsCut NOTIFY clipboardChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap selectedItem READ selectedItem NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeTextRole,
        ModifiedTextRole,
        IconNameRole,
        ExtensionRole,
        HasItemsRole,
        FolderColorRole,
        FolderEmblemRole
    };

    explicit FilesController(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString currentPath() const { return current_path_; }
    QString currentFolderName() const;
    QVariantList breadcrumbs() const;
    bool canGoBack() const { return history_index_ > 0; }
    bool canGoForward() const { return history_index_ + 1 < history_.size(); }
    bool canGoUp() const;
    bool showHidden() const { return show_hidden_; }
    void setShowHidden(bool show);
    QString filterText() const { return filter_text_; }
    void setFilterText(const QString& text);
    QString sortBy() const { return sort_by_; }
    void setSortBy(const QString& mode);
    bool hasClipboard() const { return !clipboard_path_.isEmpty(); }
    bool clipboardIsCut() const { return clipboard_cut_; }
    int itemCount() const { return entries_.size(); }
    int selectedIndex() const { return selected_index_; }
    void setSelectedIndex(int idx);
    QVariantMap selectedItem() const;

    Q_INVOKABLE void navigateTo(const QString& path);
    Q_INVOKABLE void navigatePlace(const QString& placeId);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activateIndex(int idx);
    Q_INVOKABLE void showPackageContents(int idx);
    Q_INVOKABLE bool isBundlePath(const QString& path) const;
    Q_INVOKABLE void createFolder(const QString& name);
    Q_INVOKABLE void renameSelected(const QString& newName);
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void cutSelected();
    Q_INVOKABLE void pasteHere();
    Q_INVOKABLE void openInTerminal();
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void copySelectedPath();
    Q_INVOKABLE void setFolderStyle(const QString& path, const QString& colorHex, const QString& emblemName);

signals:
    void pathChanged();
    void historyChanged();
    void filterChanged();
    void clipboardChanged();
    void entriesChanged();
    void selectionChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    struct FolderStyle {
        QString color;
        QString emblem;
    };

    void loadDirectory(const QString& path, bool push_history);
    void loadFolderStyles();
    void saveFolderStyles() const;
    static QString folderStylesPath();
    static bool dirHasItems(const QString& path);
    QString resolveFolderColor(const QFileInfo& fi) const;
    QString resolveFolderEmblem(const QFileInfo& fi) const;
    static QString iconForEntry(const QFileInfo& fi);
    static QString formatSize(qint64 bytes);

    QString current_path_;
    QStringList history_;
    int history_index_ = -1;
    bool show_hidden_ = false;
    QString filter_text_;
    QString sort_by_ = QStringLiteral("name");
    QString clipboard_path_;
    bool clipboard_cut_ = false;
    QList<QFileInfo> entries_;
    QHash<QString, bool> dir_has_items_cache_;
    QHash<QString, FolderStyle> folder_styles_;
    int selected_index_ = -1;
};

}  // namespace finn::apps

#endif  // FINN_APPS_FILES_CONTROLLER_HPP
