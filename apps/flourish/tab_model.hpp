#ifndef FLOURISH_TAB_MODEL_HPP
#define FLOURISH_TAB_MODEL_HPP

#include <QColor>
#include <QIcon>
#include <QString>
#include <QUrl>
#include <QVector>

namespace flourish {

/**
 * @brief Represents a workspace (Arc-style Space) with its own accent color.
 */
struct BrowserSpace {
    QString id;
    QString name;
    QString icon_name;
    QColor accent;
};

/**
 * @brief Represents a single browser tab within a Space.
 */
struct BrowserTab {
    QString id;
    QString space_id;
    QUrl url;
    QString title;
    bool pinned = false;
    bool favorite = false;
    bool loading = false;
    int load_progress = 0;
    QIcon favicon;
};

/**
 * @brief Represents a file download tracked by Chromium/WebEngine.
 */
struct DownloadEntry {
    int id = 0;
    QString filename;
    QString path;
    qint64 received_bytes = 0;
    qint64 total_bytes = 0;
    bool finished = false;
    bool cancelled = false;
};

/**
 * @brief Normalizes user omnibar input into a navigable URL or search query.
 */
QUrl normalize_user_url(const QString& raw_input);

/**
 * @brief Returns a concise host or display label for a URL.
 */
QString display_host_for_url(const QUrl& url);

/**
 * @brief Persists Spaces, Favorites, and Pinned Tabs in ~/.config/finn/flourish-session.json.
 */
class SessionStore {
public:
    static QString sessionFilePath();

    static void load(QVector<BrowserSpace>& spaces,
                     QVector<BrowserTab>& tabs,
                     QString& active_space_id);

    static void save(const QVector<BrowserSpace>& spaces,
                     const QVector<BrowserTab>& tabs,
                     const QString& active_space_id);
};

}  // namespace flourish

#endif
