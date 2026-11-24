// Public interface for finn-comp: FinnOS Qt 6 + Wayland Compositor.
// KWin-inspired architecture: WorkspaceModel + InputRouter + SceneStack +
// Qt Quick GPU Scene Graph (with Dual-Kawase blur) + Qt6WaylandCompositor
// (xdg-shell, xdg-decoration, text-input-v3, viewporter, layer/lock coordination).
//
// Contains ZERO app IDs; windows are managed purely through Wayland protocols.

#ifndef FINN_COMP_SERVER_HPP
#define FINN_COMP_SERVER_HPP

#include "comp/core/input_router.hpp"
#include "comp/core/scene_stack.hpp"
#include "comp/core/workspace_model.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <cstdint>
#include <memory>

#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
#include <QtWaylandCompositor/QWaylandQuickCompositor>
#include <QtWaylandCompositor/QWaylandQuickOutput>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandTextInputManagerV3>
#include <QtWaylandCompositor/QWaylandViewporter>
#include <QtWaylandCompositor/QWaylandXdgDecorationManagerV1>
#include <QtWaylandCompositor/QWaylandXdgShell>
#include <QtWaylandCompositor/QWaylandXdgToplevel>
#include <QHash>
#endif

class QQmlApplicationEngine;
class QQuickWindow;

namespace finn::comp {

// Reactive QAbstractListModel exposing WorkspaceModel windows to CompositorScene.qml
class CompositorWindowListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        OpacityRole,
        ScaleRole,
        FocusedRole,
        MaximizedRole,
        FullscreenRole,
        MinimizedRole,
        ServerDecoratedRole,
        ZIndexRole,
        DesktopIndexRole,
        SurfaceObjectRole,
    };

    explicit CompositorWindowListModel(WorkspaceModel* workspace,
                                       QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void bindSurfaceObject(std::uint64_t id, QObject* surfaceObj);
    void unbindSurfaceObject(std::uint64_t id);
    void syncAll();
    void syncGeometryFrames();

private:
    WorkspaceModel* workspace_ = nullptr;
    QHash<std::uint64_t, QObject*> surface_objects_;
};

class CompositorServer final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY stateChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY stateChanged)
    Q_PROPERTY(bool sessionLocked READ sessionLocked NOTIFY stateChanged)
    Q_PROPERTY(bool overviewActive READ overviewActive NOTIFY stateChanged)
    Q_PROPERTY(int currentDesktop READ currentDesktop NOTIFY stateChanged)
    Q_PROPERTY(int desktopCount READ desktopCount NOTIFY stateChanged)
    Q_PROPERTY(QString formFactor READ formFactor NOTIFY stateChanged)
    Q_PROPERTY(QString socketPath READ socketPath CONSTANT)
    Q_PROPERTY(bool waylandActive READ waylandActive CONSTANT)
    Q_PROPERTY(int backdropVersion READ backdropVersion NOTIFY backdropChanged)
    Q_PROPERTY(QVariantList overlayBlurRects READ overlayBlurRects NOTIFY overlayBlurRectsChanged)

public:
    explicit CompositorServer(QObject* parent = nullptr);
    ~CompositorServer() override;

    bool start(QQmlApplicationEngine* engine,
               const QString& socket_override = {},
               bool preview_mode = false);
    void attachOutputWindow(QQuickWindow* window);

    Q_INVOKABLE void setOutputSize(int width, int height);

    bool darkTheme() const { return dark_theme_; }
    bool reducedMotion() const { return workspace_.reduced_motion(); }
    bool sessionLocked() const { return session_locked_; }
    bool overviewActive() const { return workspace_.overview_active(); }
    int currentDesktop() const { return workspace_.current_desktop(); }
    int desktopCount() const { return workspace_.desktop_count(); }
    QString formFactor() const;
    QString socketPath() const { return socket_path_; }
    bool waylandActive() const;
    int backdropVersion() const { return backdrop_version_; }
    QVariantList overlayBlurRects() const { return overlay_blur_rects_; }
    QImage sampleBlurredBackdrop(quint64 excludeWindowId, const QRectF& screenRect) const;

    WorkspaceModel& workspace() { return workspace_; }
    CompositorWindowListModel* windowModel() const { return window_model_; }

    // Trigger lock request file in /run/finn/state/lock-request (shared with finn-shell)
    Q_INVOKABLE void request_lock();
    Q_INVOKABLE void focusWindow(quint64 id);
    Q_INVOKABLE void closeWindow(quint64 id);
    Q_INVOKABLE void toggleMaximize(quint64 id);
    Q_INVOKABLE void minimizeWindow(quint64 id);
    Q_INVOKABLE void reportWindowMoved(quint64 id, int x, int y);
    Q_INVOKABLE void snapWindowLeft(quint64 id);
    Q_INVOKABLE void snapWindowRight(quint64 id);
    Q_INVOKABLE void toggleOverview();
    Q_INVOKABLE void switchDesktop(int index);
    Q_INVOKABLE void handleShortcutKey(quint32 modifiers, quint32 keysym);
    Q_INVOKABLE void dispatchTrackpadSwipe(int fingers, double dx, double dy);
    Q_INVOKABLE void dispatchTrackpadPinch(int fingers, double scale);
    Q_INVOKABLE void noteUserActivity();

    bool eventFilter(QObject* watched, QEvent* event) override;

    static int read_auto_lock_minutes();

signals:
    void stateChanged();
    void shellActionRequested(const QString& action);
    void backdropChanged();
    void overlayBlurRectsChanged();

private:
    void pollSettingsAndIdle();
    void pollCaptureRequests();
    void pollWindowCommands();
    void pollOverlayBlurRects();
    void writeWindowStateFile();
    void reloadWallpaperQuarter();
    void scheduleBackdropRefresh(std::uint64_t dirty_window_id = 0);
    void flushBackdropRefresh();
    void stepFrame();
    void ensureWaylandSocketPermissions();
    void startXwaylandIfEnabled();
    void executeShortcutAction(ShortcutAction act);
    void writeShellActionFile(const QString& action);
    void startLibinputGestures();
    void stopLibinputGestures();
    void handleLibinputEvents();

    WorkspaceModel workspace_;
    InputRouter input_router_;
    CompositorWindowListModel* window_model_ = nullptr;
    QQuickWindow* output_window_ = nullptr;

    QString socket_path_;
    bool dark_theme_ = false;
    bool session_locked_ = false;
    int auto_lock_minutes_ = 5;
    std::string kb_layout_ = "us";
    std::string kb_variant_;
    std::uint64_t last_activity_msec_ = 0;

    QTimer frame_timer_;
    QElapsedTimer frame_clock_;
    QTimer settings_timer_;
    QTimer capture_timer_;
    QTimer watchdog_timer_;
    QTimer window_cmd_timer_;
    QTimer backdrop_timer_;
    QProcess* xwayland_proc_ = nullptr;

    QImage wallpaper_quarter_;
    QSize wallpaper_output_size_;
    QString wallpaper_path_cache_;
    bool wallpaper_dark_cache_ = false;
    QHash<std::uint64_t, QImage> surface_thumbs_;
    QSet<std::uint64_t> dirty_thumbs_;
    int backdrop_version_ = 0;
    QVariantList overlay_blur_rects_;
    qint64 blur_rects_mtime_ = 0;

    // Trackpad multi-finger gesture state (Qt NativeGesture / Touch + native libinput)
    int gesture_fingers_ = 0;
    double gesture_dx_ = 0.0;
    double gesture_dy_ = 0.0;
    double gesture_scale_ = 1.0;
    bool gesture_pinch_active_ = false;
    QPointF touch_start_center_;
    int touch_max_fingers_ = 0;
    void* libinput_ctx_ = nullptr;
    void* udev_ctx_ = nullptr;
    QSocketNotifier* libinput_notifier_ = nullptr;

#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    QWaylandQuickCompositor* wayland_compositor_ = nullptr;
    QWaylandQuickOutput* wayland_output_ = nullptr;
    QWaylandXdgShell* xdg_shell_ = nullptr;
    QWaylandXdgDecorationManagerV1* xdg_decoration_ = nullptr;
    QWaylandTextInputManagerV3* text_input_ = nullptr;
    QWaylandViewporter* viewporter_ = nullptr;
    QHash<std::uint64_t, QWaylandXdgToplevel*> toplevel_map_;
#endif
};

}  // namespace finn::comp

#endif  // FINN_COMP_SERVER_HPP
