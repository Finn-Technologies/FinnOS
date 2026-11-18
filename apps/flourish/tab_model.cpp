#include "tab_model.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>

namespace flourish {

namespace {

QString make_id() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString default_space_icon(const QString& id) {
    if (id == QStringLiteral("personal")) return QStringLiteral("home-09");
    if (id == QStringLiteral("work")) return QStringLiteral("briefcase-01");
    if (id == QStringLiteral("dev")) return QStringLiteral("code-01");
    return QStringLiteral("folder");
}

void populate_defaults(QVector<BrowserSpace>& spaces,
                       QVector<BrowserTab>& tabs,
                       QString& active_space_id) {
    spaces = {
        {QStringLiteral("personal"), QStringLiteral("Personal"),
         QStringLiteral("home-09"), QColor(0x00, 0x7B, 0xFF)},
        {QStringLiteral("work"), QStringLiteral("Work"),
         QStringLiteral("briefcase-01"), QColor(0x22, 0xC5, 0x5E)},
        {QStringLiteral("dev"), QStringLiteral("Dev"),
         QStringLiteral("code-01"), QColor(0xF5, 0x9E, 0x0B)},
    };
    active_space_id = spaces.front().id;

    tabs = {
        {make_id(), QStringLiteral("personal"), QUrl(QStringLiteral("https://www.google.com")),
         QStringLiteral("Google"), true, true, false, 100, {}},
        {make_id(), QStringLiteral("personal"), QUrl(QStringLiteral("https://en.wikipedia.org")),
         QStringLiteral("Wikipedia"), true, true, false, 100, {}},
        {make_id(), QStringLiteral("personal"), QUrl(QStringLiteral("https://github.com")),
         QStringLiteral("GitHub"), true, true, false, 100, {}},
        {make_id(), QStringLiteral("dev"), QUrl(QStringLiteral("https://finnos.org/docs/")),
         QStringLiteral("FinnOS Docs"), true, true, false, 100, {}},
        {make_id(), QStringLiteral("personal"), QUrl(QStringLiteral("flourish://home")),
         QStringLiteral("New Tab"), false, false, false, 100, {}},
    };
}

}  // namespace

QUrl normalize_user_url(const QString& raw_input) {
    const QString trimmed = raw_input.trimmed();
    if (trimmed.isEmpty()) {
        return QUrl(QStringLiteral("flourish://home"));
    }
    if (trimmed == QStringLiteral("flourish://home") ||
        trimmed == QStringLiteral("about:home")) {
        return QUrl(QStringLiteral("flourish://home"));
    }
    if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("flourish://"), Qt::CaseInsensitive)) {
        return QUrl::fromUserInput(trimmed);
    }

    const bool has_space = trimmed.contains(QLatin1Char(' '));
    const bool looks_like_host =
        !has_space &&
        (trimmed.contains(QLatin1Char('.')) ||
         trimmed.startsWith(QStringLiteral("localhost"), Qt::CaseInsensitive));

    if (looks_like_host) {
        return QUrl::fromUserInput(trimmed);
    }

    QUrl search(QStringLiteral("https://www.google.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), trimmed);
    search.setQuery(query);
    return search;
}

QString display_host_for_url(const QUrl& url) {
    if (!url.isValid() || url.scheme() == QStringLiteral("flourish")) {
        return QStringLiteral("Search or enter address");
    }
    QString host = url.host();
    if (host.startsWith(QStringLiteral("www."))) {
        host = host.mid(4);
    }
    if (!host.isEmpty()) {
        return host;
    }
    return url.toString(QUrl::RemoveFragment);
}

QString SessionStore::sessionFilePath() {
    const QString config_dir =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return config_dir + QStringLiteral("/finn/flourish-session.json");
}

void SessionStore::load(QVector<BrowserSpace>& spaces,
                        QVector<BrowserTab>& tabs,
                        QString& active_space_id) {
    spaces.clear();
    tabs.clear();
    active_space_id.clear();

    QFile file(sessionFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        populate_defaults(spaces, tabs, active_space_id);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        populate_defaults(spaces, tabs, active_space_id);
        return;
    }

    const QJsonObject root = doc.object();
    active_space_id = root.value(QStringLiteral("active_space_id")).toString();

    const QJsonArray space_arr = root.value(QStringLiteral("spaces")).toArray();
    for (const QJsonValue& v : space_arr) {
        const QJsonObject obj = v.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString icon = obj.value(QStringLiteral("icon")).toString();
        const QColor accent(obj.value(QStringLiteral("accent")).toString());
        if (!id.isEmpty() && !name.isEmpty()) {
            spaces.push_back({id, name,
                              icon.isEmpty() ? default_space_icon(id) : icon,
                              accent.isValid() ? accent : QColor(0x00, 0x7B, 0xFF)});
        }
    }

    if (spaces.isEmpty()) {
        populate_defaults(spaces, tabs, active_space_id);
        return;
    }

    const QJsonArray tab_arr = root.value(QStringLiteral("tabs")).toArray();
    for (const QJsonValue& v : tab_arr) {
        const QJsonObject obj = v.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        const QString space_id = obj.value(QStringLiteral("space_id")).toString();
        const QUrl url(obj.value(QStringLiteral("url")).toString());
        const QString title = obj.value(QStringLiteral("title")).toString();
        const bool pinned = obj.value(QStringLiteral("pinned")).toBool(false);
        const bool favorite = obj.value(QStringLiteral("favorite")).toBool(false);
        if (!id.isEmpty() && !space_id.isEmpty() && url.isValid()) {
            tabs.push_back({id, space_id, url, title.isEmpty() ? display_host_for_url(url) : title,
                            pinned, favorite, false, 100, {}});
        }
    }

    if (active_space_id.isEmpty()) {
        active_space_id = spaces.front().id;
    }
    if (tabs.isEmpty()) {
        tabs.push_back({make_id(), active_space_id, QUrl(QStringLiteral("flourish://home")),
                        QStringLiteral("New Tab"), false, false, false, 100, {}});
    }
}

void SessionStore::save(const QVector<BrowserSpace>& spaces,
                        const QVector<BrowserTab>& tabs,
                        const QString& active_space_id) {
    const QString path = sessionFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray space_arr;
    for (const auto& s : spaces) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), s.id);
        obj.insert(QStringLiteral("name"), s.name);
        obj.insert(QStringLiteral("icon"), s.icon_name);
        obj.insert(QStringLiteral("accent"), s.accent.name(QColor::HexRgb));
        space_arr.append(obj);
    }

    QJsonArray tab_arr;
    for (const auto& t : tabs) {
        // Persist favorites, pinned tabs, and open tabs so session restores cleanly.
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), t.id);
        obj.insert(QStringLiteral("space_id"), t.space_id);
        obj.insert(QStringLiteral("url"), t.url.toString());
        obj.insert(QStringLiteral("title"), t.title);
        obj.insert(QStringLiteral("pinned"), t.pinned);
        obj.insert(QStringLiteral("favorite"), t.favorite);
        tab_arr.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("active_space_id"), active_space_id);
    root.insert(QStringLiteral("spaces"), space_arr);
    root.insert(QStringLiteral("tabs"), tab_arr);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

}  // namespace flourish
