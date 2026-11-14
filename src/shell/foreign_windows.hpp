// Client for wlr_foreign_toplevel_management_v1 tracking live compositor
// windows cleanly on the Qt main thread without blocking UI rendering.

#ifndef FINN_SHELL_FOREIGN_WINDOWS_HPP
#define FINN_SHELL_FOREIGN_WINDOWS_HPP

#include <QObject>
#include <QString>
#include <QVector>
#include <cstdint>

struct wl_registry;
struct wl_seat;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace finn::shell {

struct ForeignWindowInfo {
    quintptr id = 0;
    QString title;
    QString app_id;
    bool maximized = false;
    bool minimized = false;
    bool activated = false;
    bool fullscreen = false;
};

class ForeignWindowsManager final : public QObject {
    Q_OBJECT

public:
    explicit ForeignWindowsManager(QObject* parent = nullptr);
    ~ForeignWindowsManager() override;

    const QVector<ForeignWindowInfo>& windows() const { return windows_; }
    QVector<ForeignWindowInfo> windowsForApp(const QString& app_id) const;
    bool hasRunningApp(const QString& app_id) const;

public slots:
    void activateWindow(quintptr id);
    void setMinimized(quintptr id, bool minimized);
    void toggleMinimize(quintptr id);
    void setMaximized(quintptr id, bool maximized);
    void closeWindow(quintptr id);

signals:
    void windowsChanged();

public:
    struct Entry;
    void handleDone();
    void handleClosed(Entry* entry);

private:
    void bindWayland();
    void rebuildPublicList();

private:
    static void onGlobal(void* data, wl_registry* reg, std::uint32_t name,
                         const char* interface, std::uint32_t version);
    static void onGlobalRemove(void* data, wl_registry* reg, std::uint32_t name);
    static void onToplevel(void* data,
                           zwlr_foreign_toplevel_manager_v1* manager,
                           zwlr_foreign_toplevel_handle_v1* handle);
    static void onManagerFinished(void* data,
                                  zwlr_foreign_toplevel_manager_v1* manager);

    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
    QVector<Entry*> entries_;
    QVector<ForeignWindowInfo> windows_;
};

}  // namespace finn::shell

#endif
