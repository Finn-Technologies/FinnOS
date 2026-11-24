// Implementation of finn-comp: Qt 6 + Wayland compositor with KWin-inspired
// WorkspaceModel, InputRouter, SceneStack, GPU Qt Quick scene graph, and
// real systemd readiness + lock-request IPC.
//
// Zero app IDs appear anywhere in this file.

#include "comp/server.hpp"

#include "common/paths.hpp"
#include "comp/backdrop_item.hpp"
#include "system/sys_files.hpp"

#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTouchEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <grp.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

#ifdef FINN_HAVE_LIBINPUT
#include <libinput.h>
#include <libudev.h>
#endif

#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
#include <QtWaylandCompositor/QWaylandBufferRef>
#include <QtWaylandCompositor/QWaylandKeymap>
#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandView>
#endif

#ifdef FINN_HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

// Standard XKB keysym alias kept explicit for keyboard shortcut routing.
#ifndef XKB_KEY_l
#define XKB_KEY_l 0x006c
#endif

namespace finn::comp {
namespace {

std::uint64_t monotonic_now_ms() {
    return static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
}

}  // namespace

// ---- CompositorWindowListModel ----

CompositorWindowListModel::CompositorWindowListModel(WorkspaceModel* workspace,
                                                     QObject* parent)
    : QAbstractListModel(parent), workspace_(workspace) {}

int CompositorWindowListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !workspace_) return 0;
    return static_cast<int>(workspace_->windows().size());
}

QVariant CompositorWindowListModel::data(const QModelIndex& index, int role) const {
    if (!workspace_ || !index.isValid()) return {};
    const auto& wins = workspace_->windows();
    if (index.row() < 0 || static_cast<std::size_t>(index.row()) >= wins.size()) return {};
    const auto& w = wins[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole:              return static_cast<quint64>(w.id);
        case TitleRole:           return QString::fromStdString(w.title);
        case XRole:               return w.spring_x.value();
        case YRole:               return w.spring_y.value();
        case WidthRole:           return w.spring_w.value();
        case HeightRole:          return w.spring_h.value();
        case OpacityRole:         return w.spring_opacity.value();
        case ScaleRole:           return w.spring_scale.value();
        case FocusedRole:         return w.focused;
        case MaximizedRole:       return w.maximized;
        case FullscreenRole:      return w.fullscreen;
        case MinimizedRole:       return w.minimized;
        case ServerDecoratedRole: return w.server_decorated;
        case ZIndexRole:          return w.z_index(index.row() + (w.focused ? 500 : 0));
        case DesktopIndexRole:    return w.desktop_index;
        case SurfaceObjectRole:   return QVariant::fromValue(surface_objects_.value(w.id, nullptr));
        default:                  return {};
    }
}

QHash<int, QByteArray> CompositorWindowListModel::roleNames() const {
    return {
        {IdRole, "windowId"},
        {TitleRole, "title"},
        {XRole, "winX"},
        {YRole, "winY"},
        {WidthRole, "winWidth"},
        {HeightRole, "winHeight"},
        {OpacityRole, "winOpacity"},
        {ScaleRole, "winScale"},
        {FocusedRole, "focused"},
        {MaximizedRole, "maximized"},
        {FullscreenRole, "fullscreen"},
        {MinimizedRole, "minimized"},
        {ServerDecoratedRole, "serverDecorated"},
        {ZIndexRole, "zIndex"},
        {DesktopIndexRole, "desktopIndex"},
        {SurfaceObjectRole, "surfaceObject"},
    };
}

void CompositorWindowListModel::bindSurfaceObject(std::uint64_t id, QObject* surfaceObj) {
    surface_objects_.insert(id, surfaceObj);
}

void CompositorWindowListModel::unbindSurfaceObject(std::uint64_t id) {
    surface_objects_.remove(id);
}

void CompositorWindowListModel::syncAll() {
    beginResetModel();
    endResetModel();
}

void CompositorWindowListModel::syncGeometryFrames() {
    const int n = rowCount();
    if (n <= 0) return;
    emit dataChanged(index(0), index(n - 1),
                     {XRole, YRole, WidthRole, HeightRole, OpacityRole, ScaleRole,
                      FocusedRole, MaximizedRole, FullscreenRole, MinimizedRole, ZIndexRole});
}

// ---- CompositorServer ----

CompositorServer::CompositorServer(QObject* parent)
    : QObject(parent),
      window_model_(new CompositorWindowListModel(&workspace_, this)),
      socket_path_(QString::fromLatin1(finn::kWaylandPath)),
      last_activity_msec_(monotonic_now_ms()) {
    const bool sw_backend =
        qEnvironmentVariable("QT_QUICK_BACKEND").compare(QStringLiteral("software"), Qt::CaseInsensitive) == 0;
    frame_timer_.setInterval(sw_backend ? 16 : 8);  // 60Hz on software fb, 120Hz on GPU
    connect(&frame_timer_, &QTimer::timeout, this, &CompositorServer::stepFrame);

    settings_timer_.setInterval(1000);
    connect(&settings_timer_, &QTimer::timeout, this,
            &CompositorServer::pollSettingsAndIdle);

    capture_timer_.setInterval(200);
    connect(&capture_timer_, &QTimer::timeout, this,
            &CompositorServer::pollCaptureRequests);

    watchdog_timer_.setInterval(5000);
    connect(&watchdog_timer_, &QTimer::timeout, this, []() {
#ifdef FINN_HAVE_SYSTEMD
        sd_notify(0, "WATCHDOG=1");
#endif
    });

    window_cmd_timer_.setInterval(60);
    connect(&window_cmd_timer_, &QTimer::timeout, this, [this]() {
        pollWindowCommands();
        pollOverlayBlurRects();
    });

    backdrop_timer_.setSingleShot(true);
    backdrop_timer_.setInterval(16);
    connect(&backdrop_timer_, &QTimer::timeout, this,
            &CompositorServer::flushBackdropRefresh);
}

CompositorServer::~CompositorServer() {
    stopLibinputGestures();
    if (xwayland_proc_ && xwayland_proc_->state() != QProcess::NotRunning) {
        xwayland_proc_->terminate();
        xwayland_proc_->waitForFinished(1000);
    }
}

QString CompositorServer::formFactor() const {
    const auto id = finn::ui::form_factor_id(workspace_.form_factor());
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}

bool CompositorServer::waylandActive() const {
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    return wayland_compositor_ != nullptr && wayland_compositor_->isCreated();
#else
    return false;
#endif
}

int CompositorServer::read_auto_lock_minutes() {
    return finn::sys::load_merged_settings().auto_lock_minutes;
}

void CompositorServer::request_lock() {
    const QString state_dir = QString::fromLatin1(finn::kCompositorStateDir);
    QDir().mkpath(state_dir);
    QFile req(QString::fromLatin1(finn::kLockRequestPath));
    if (req.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        req.write("lock\n");
        req.close();
    }
    session_locked_ = true;
    emit stateChanged();
}

void CompositorServer::ensureWaylandSocketPermissions() {
    const QByteArray path_utf8 = socket_path_.toUtf8();
    if (!QFileInfo::exists(socket_path_)) return;
    ::chmod(path_utf8.constData(), 0660);
    if (struct group* gr = ::getgrnam("finn"); gr != nullptr) {
        if (::chown(path_utf8.constData(), 0, gr->gr_gid) != 0) {
            // Non-root dev runs keep owner permissions cleanly.
        }
    }
}

void CompositorServer::startXwaylandIfEnabled() {
    if (qEnvironmentVariableIntValue("FINN_DISABLE_XWAYLAND") == 1) return;
    const QString xw = QStandardPaths::findExecutable(QStringLiteral("Xwayland"));
    if (xw.isEmpty()) return;

    xwayland_proc_ = new QProcess(this);
    xwayland_proc_->setProgram(xw);
    xwayland_proc_->setArguments({QStringLiteral("-rootless"), QStringLiteral("-noreset")});
    // Started on demand when X11 clients request a display in Linux sessions.
}

bool CompositorServer::start(QQmlApplicationEngine* engine,
                             const QString& socket_override,
                             bool preview_mode) {
    if (!socket_override.isEmpty()) {
        socket_path_ = socket_override;
    }

    pollSettingsAndIdle();
    settings_timer_.start();
    capture_timer_.start();
    watchdog_timer_.start();
    window_cmd_timer_.start();

#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    if (!preview_mode) {
        const QFileInfo sock_info(socket_path_);
        QDir().mkpath(sock_info.absolutePath());
        qputenv("XDG_RUNTIME_DIR", sock_info.absolutePath().toUtf8());

        wayland_compositor_ = new QWaylandQuickCompositor(this);
        wayland_compositor_->setSocketName(sock_info.fileName().toUtf8());

        xdg_shell_ = new QWaylandXdgShell(wayland_compositor_);
        xdg_decoration_ = new QWaylandXdgDecorationManagerV1();
        xdg_decoration_->setExtensionContainer(wayland_compositor_);
        xdg_decoration_->setParent(wayland_compositor_);
        xdg_decoration_->setPreferredMode(QWaylandXdgToplevel::ClientSideDecoration);
        text_input_ = new QWaylandTextInputManagerV3(wayland_compositor_);
        viewporter_ = new QWaylandViewporter(wayland_compositor_);

        connect(xdg_shell_, &QWaylandXdgShell::toplevelCreated, this,
                [this](QWaylandXdgToplevel* toplevel, QWaylandXdgSurface* xdg_surface) {
                    const QString title = toplevel->title().isEmpty()
                                              ? QStringLiteral("Application")
                                              : toplevel->title();
                    const bool ssd =
                        toplevel->decorationMode() == QWaylandXdgToplevel::ServerSideDecoration;
                    const auto out_rect = workspace_.outputs().empty()
                                              ? RectI{0, 0, 1280, 832}
                                              : workspace_.outputs().front().geometry;
                    const bool start_fullscreen = toplevel->fullscreen();
                    const int init_w = start_fullscreen ? out_rect.width : 800;
                    const int init_h = start_fullscreen ? out_rect.height : 500;
                    const std::uint64_t wid =
                        workspace_.add_window(title.toStdString(), init_w, init_h, ssd, false);
                    if (start_fullscreen) {
                        workspace_.set_fullscreen(wid, true);
                    }
                    toplevel_map_.insert(wid, toplevel);
                    window_model_->bindSurfaceObject(wid, xdg_surface);
                    window_model_->syncAll();

                    QList<QWaylandXdgToplevel::State> states;
                    states.append(QWaylandXdgToplevel::ActivatedState);
                    if (start_fullscreen) {
                        states.append(QWaylandXdgToplevel::FullscreenState);
                        toplevel->sendConfigure(QSize(out_rect.width, out_rect.height), states);
                    } else {
                        // QSize(0, 0) lets the Wayland client choose its own exact native window size
                        toplevel->sendConfigure(QSize(0, 0), states);
                    }

                    if (!frame_timer_.isActive()) {
                        frame_clock_.restart();
                        frame_timer_.start();
                    }

                    if (auto* surf = xdg_surface->surface()) {
                        connect(surf, &QWaylandSurface::redraw, this,
                                [this, wid]() { scheduleBackdropRefresh(wid); });
                        connect(surf, &QWaylandSurface::hasContentChanged, this,
                                [this, wid]() { scheduleBackdropRefresh(wid); });
                        connect(surf, &QWaylandSurface::destinationSizeChanged, this,
                                [this, wid, toplevel, surf]() {
                                    const QSize sz = surf->destinationSize();
                                    if (sz.width() <= 0 || sz.height() <= 0) return;
                                    if (toplevel->fullscreen()) {
                                        workspace_.set_fullscreen(wid, true);
                                        window_model_->syncAll();
                                        scheduleBackdropRefresh(wid);
                                        return;
                                    }
                                    if (auto* w = workspace_.find_window(wid)) {
                                        if (w->fullscreen) {
                                            // Keep fullscreen shell overlay fullscreen
                                            return;
                                        }
                                        if (!w->maximized &&
                                            (w->target_geometry.width != sz.width() ||
                                             w->target_geometry.height != sz.height())) {
                                            const auto ua = workspace_.primary_output().usable_area;
                                            const int cx = ua.x + std::max(0, (ua.width - sz.width()) / 2);
                                            const int cy = ua.y + std::max(0, (ua.height - sz.height()) / 2);
                                            workspace_.move_window(wid, cx, cy, false);
                                            workspace_.resize_window(wid, sz.width(), sz.height(), false);
                                            window_model_->syncAll();
                                            scheduleBackdropRefresh(wid);
                                        }
                                    }
                                });
                    }

                    connect(toplevel, &QWaylandXdgToplevel::titleChanged, this, [this, wid, toplevel]() {
                        if (auto* w = workspace_.find_window(wid)) {
                            w->title = toplevel->title().toStdString();
                            window_model_->syncAll();
                            writeWindowStateFile();
                        }
                    });
                    connect(toplevel, &QWaylandXdgToplevel::appIdChanged, this, [this]() {
                        writeWindowStateFile();
                    });
                    connect(toplevel, &QWaylandXdgToplevel::maximizedChanged, this, [this, wid, toplevel]() {
                        workspace_.set_maximized(wid, toplevel->maximized());
                        if (auto* w = workspace_.find_window(wid)) {
                            QList<QWaylandXdgToplevel::State> st{QWaylandXdgToplevel::ActivatedState};
                            if (w->maximized) {
                                st.append(QWaylandXdgToplevel::MaximizedState);
                            }
                            toplevel->sendConfigure(
                                QSize(w->target_geometry.width, w->target_geometry.height), st);
                        }
                        if (!frame_timer_.isActive()) {
                            frame_clock_.restart();
                            frame_timer_.start();
                        }
                        window_model_->syncAll();
                        writeWindowStateFile();
                        scheduleBackdropRefresh(wid);
                    });
                    connect(toplevel, &QWaylandXdgToplevel::fullscreenChanged, this, [this, wid, toplevel]() {
                        workspace_.set_fullscreen(wid, toplevel->fullscreen());
                        const auto r = workspace_.outputs().empty()
                                           ? RectI{0, 0, 1280, 832}
                                           : workspace_.outputs().front().geometry;
                        QList<QWaylandXdgToplevel::State> st{QWaylandXdgToplevel::ActivatedState};
                        if (toplevel->fullscreen()) {
                            st.append(QWaylandXdgToplevel::FullscreenState);
                            toplevel->sendConfigure(QSize(r.width, r.height), st);
                        } else if (auto* w = workspace_.find_window(wid)) {
                            toplevel->sendConfigure(
                                QSize(w->target_geometry.width, w->target_geometry.height), st);
                        } else {
                            toplevel->sendConfigure(QSize(0, 0), st);
                        }
                        if (!frame_timer_.isActive()) {
                            frame_clock_.restart();
                            frame_timer_.start();
                        }
                        window_model_->syncAll();
                        writeWindowStateFile();
                        scheduleBackdropRefresh(wid);
                    });
                    connect(toplevel, &QObject::destroyed, this, [this, wid]() {
                        toplevel_map_.remove(wid);
                        surface_thumbs_.remove(wid);
                        dirty_thumbs_.remove(wid);
                        window_model_->unbindSurfaceObject(wid);
                        workspace_.remove_window(wid);
                        window_model_->syncAll();
                        writeWindowStateFile();
                        scheduleBackdropRefresh();
                    });
                    writeWindowStateFile();
                    scheduleBackdropRefresh(wid);
                });

        wayland_compositor_->create();
        if (auto* seat = wayland_compositor_->defaultSeat()) {
            if (auto* km = seat->keymap()) {
                km->setLayout(QString::fromStdString(kb_layout_.empty() ? "us" : kb_layout_));
                km->setVariant(QString::fromStdString(kb_variant_));
            }
        }
        ensureWaylandSocketPermissions();
        startXwaylandIfEnabled();
    }
#else
    Q_UNUSED(preview_mode);
#endif

    if (preview_mode) {
        // Populate neutral placeholder surfaces for host visual verification (zero app IDs!)
        workspace_.add_window("Window A", 520, 340, true, false);
        workspace_.add_window("Window B", 540, 360, false, false);
        window_model_->syncAll();
    }

    if (engine) {
        qmlRegisterType<CompositorBackdropItem>("FinnComp", 1, 0, "CompositorBackdrop");
        engine->rootContext()->setContextProperty(QStringLiteral("FinnComp"), this);
        engine->rootContext()->setContextProperty(QStringLiteral("FinnCompWindows"), window_model_);
    }
    reloadWallpaperQuarter();
    pollOverlayBlurRects();

    frame_clock_.restart();
    frame_timer_.start();

#ifdef FINN_HAVE_SYSTEMD
    sd_notify(0, "READY=1\nSTATUS=FinnOS Qt 6 Wayland Compositor active");
#endif
    emit stateChanged();
    return true;
}

void CompositorServer::attachOutputWindow(QQuickWindow* window) {
    output_window_ = window;
    if (!output_window_) return;
    output_window_->installEventFilter(this);
    startLibinputGestures();
    if (output_window_->width() > 0 && output_window_->height() > 0) {
        setOutputSize(output_window_->width(), output_window_->height());
    }
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    if (wayland_compositor_ && !wayland_output_) {
        wayland_output_ = new QWaylandQuickOutput(wayland_compositor_, output_window_);
        wayland_output_->setSizeFollowsWindow(true);
        wayland_compositor_->setDefaultOutput(wayland_output_);
    }
#endif
}

void CompositorServer::setOutputSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    OutputInfo out;
    out.id = 1;
    out.name = "primary";
    out.geometry = RectI{0, 0, width, height};
    out.usable_area = RectI{0, 0, width, std::max(100, height - 59)};
    out.primary = true;
    workspace_.set_outputs({out});
    emit stateChanged();
}

void CompositorServer::pollCaptureRequests() {
    if (!output_window_) return;
    const QString req_path = QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/capture-request");
    if (!QFileInfo::exists(req_path)) return;
    QFile req(req_path);
    if (!req.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString target_png = QString::fromUtf8(req.readLine()).trimmed();
    req.close();
    QFile::remove(req_path);
    if (target_png.isEmpty()) return;

    const QImage frame = output_window_->grabWindow();
    if (!frame.isNull() && frame.save(target_png, "PNG")) {
        ::chmod(target_png.toUtf8().constData(), 0644);
    }
    QFile done(target_png + QStringLiteral(".done"));
    if (done.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        done.write("ok\n");
        done.close();
        ::chmod((target_png + QStringLiteral(".done")).toUtf8().constData(), 0644);
    }
}

void CompositorServer::writeWindowStateFile() {
    const QString state_dir = QString::fromLatin1(finn::kCompositorStateDir);
    QDir().mkpath(state_dir);
    ::chmod(state_dir.toUtf8().constData(), 0775);
    const QString tmp_path = state_dir + QStringLiteral("/windows.tsv.tmp");
    const QString dst_path = state_dir + QStringLiteral("/windows.tsv");
    QFile f(tmp_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    const auto out = workspace_.primary_output().geometry;
    QByteArray buf;
    buf.append(QStringLiteral("#OUT\t%1\t%2\n").arg(out.width).arg(out.height).toUtf8());

    for (const auto& w : workspace_.windows()) {
        if (!w.mapped || w.fullscreen) continue;
        QString app_id;
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
        if (auto* top = toplevel_map_.value(w.id, nullptr)) {
            app_id = top->appId();
        }
#endif
        QString clean_title = QString::fromStdString(w.title);
        clean_title.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
        app_id.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
        buf.append(QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\n")
                       .arg(w.id)
                       .arg(app_id)
                       .arg(clean_title)
                       .arg(w.target_geometry.x)
                       .arg(w.target_geometry.y)
                       .arg(w.target_geometry.width)
                       .arg(w.target_geometry.height)
                       .arg(w.focused ? 1 : 0)
                       .arg(w.minimized ? 1 : 0)
                       .arg(w.maximized ? 1 : 0)
                       .toUtf8());
    }
    f.write(buf);
    f.close();
    ::chmod(tmp_path.toUtf8().constData(), 0644);
    ::rename(tmp_path.toUtf8().constData(), dst_path.toUtf8().constData());
}

void CompositorServer::pollWindowCommands() {
    const QString cmd_path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/window-cmd");
    if (!QFileInfo::exists(cmd_path)) return;
    QFile f(cmd_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString line = QString::fromUtf8(f.readLine()).trimmed();
    f.close();
    QFile::remove(cmd_path);
    if (line.isEmpty()) return;

    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 2) return;
    const QString verb = parts[0];
    bool ok = false;
    const quint64 wid = parts[1].toULongLong(&ok);
    if (!ok || wid == 0) return;

    if (verb == QLatin1String("focus")) {
        workspace_.set_minimized(wid, false);
        workspace_.focus_window(wid);
    } else if (verb == QLatin1String("minimize")) {
        workspace_.set_minimized(wid, true);
    } else if (verb == QLatin1String("toggle")) {
        if (const auto* w = workspace_.find_window(wid)) {
            if (w->focused && !w->minimized) {
                workspace_.set_minimized(wid, true);
            } else {
                workspace_.set_minimized(wid, false);
                workspace_.focus_window(wid);
            }
        }
    } else if (verb == QLatin1String("close")) {
        closeWindow(wid);
        return;
    }
    if (!frame_timer_.isActive()) {
        frame_clock_.restart();
        frame_timer_.start();
    }
    window_model_->syncAll();
    writeWindowStateFile();
}

void CompositorServer::pollSettingsAndIdle() {
    const auto s = finn::sys::load_merged_settings();
    bool dirty = false;
    if (dark_theme_ != s.dark) {
        dark_theme_ = s.dark;
        dirty = true;
    }
    if (workspace_.reduced_motion() != s.reduced_motion) {
        workspace_.set_reduced_motion(s.reduced_motion);
        dirty = true;
    }
    auto_lock_minutes_ = read_auto_lock_minutes();
    const auto prev_ff = workspace_.form_factor();
    workspace_.set_form_factor_override(s.form_factor);
    if (workspace_.form_factor() != prev_ff) {
        dirty = true;
    }
    if (kb_layout_ != s.kb_layout || kb_variant_ != s.kb_variant) {
        kb_layout_ = s.kb_layout.empty() ? "us" : s.kb_layout;
        kb_variant_ = s.kb_variant;
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
        if (wayland_compositor_) {
            if (auto* seat = wayland_compositor_->defaultSeat()) {
                if (auto* km = seat->keymap()) {
                    km->setLayout(QString::fromStdString(kb_layout_));
                    km->setVariant(QString::fromStdString(kb_variant_));
                }
            }
        }
#endif
    }

    if (InputRouter::should_auto_lock(monotonic_now_ms(), last_activity_msec_,
                                      auto_lock_minutes_, session_locked_)) {
        request_lock();
    }

    if (dirty) emit stateChanged();
}

void CompositorServer::stepFrame() {
    const qint64 ns = frame_clock_.nsecsElapsed();
    frame_clock_.restart();
    const double dt = std::clamp(static_cast<double>(ns) * 1e-9, 0.001, 0.05);
    if (workspace_.step_animations(dt)) {
        window_model_->syncGeometryFrames();
        scheduleBackdropRefresh();
    } else {
        frame_timer_.stop();
        writeWindowStateFile();
        scheduleBackdropRefresh();
    }
}

void CompositorServer::reportWindowMoved(quint64 id, int x, int y) {
    if (auto* w = workspace_.find_window(id)) {
        if (!w->maximized && !w->fullscreen &&
            (w->target_geometry.x != x || w->target_geometry.y != y)) {
            w->target_geometry.x = x;
            w->target_geometry.y = y;
            w->saved_geometry.x = x;
            w->saved_geometry.y = y;
            w->spring_x.set(static_cast<double>(x), 0.0);
            w->spring_y.set(static_cast<double>(y), 0.0);
            writeWindowStateFile();
            scheduleBackdropRefresh();
        }
    }
}

void CompositorServer::focusWindow(quint64 id) {
    noteUserActivity();
    workspace_.focus_window(id);
    window_model_->syncGeometryFrames();
    writeWindowStateFile();
    scheduleBackdropRefresh();
}

void CompositorServer::closeWindow(quint64 id) {
    noteUserActivity();
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    if (auto* top = toplevel_map_.value(id, nullptr)) {
        top->sendClose();
        return;
    }
#endif
    workspace_.remove_window(id);
    window_model_->syncAll();
}

void CompositorServer::toggleMaximize(quint64 id) {
    noteUserActivity();
    workspace_.toggle_maximize(id);
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
}

void CompositorServer::minimizeWindow(quint64 id) {
    noteUserActivity();
    workspace_.set_minimized(id, true);
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
}

void CompositorServer::snapWindowLeft(quint64 id) {
    noteUserActivity();
    workspace_.snap_window(id, SnapEdge::Left);
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
}

void CompositorServer::snapWindowRight(quint64 id) {
    noteUserActivity();
    workspace_.snap_window(id, SnapEdge::Right);
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
}

void CompositorServer::toggleOverview() {
    noteUserActivity();
    workspace_.toggle_overview();
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
    emit stateChanged();
}

void CompositorServer::switchDesktop(int index) {
    noteUserActivity();
    workspace_.switch_desktop(index);
    if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
    window_model_->syncGeometryFrames();
    emit stateChanged();
}

void CompositorServer::handleShortcutKey(quint32 modifiers, quint32 keysym) {
    noteUserActivity();
    if ((modifiers & ModSuper) && keysym == XKB_KEY_l && !session_locked_) {
        request_lock();
        return;
    }
    const ShortcutAction act =
        input_router_.match_key(modifiers, keysym, session_locked_);
    executeShortcutAction(act);
}

void CompositorServer::dispatchTrackpadSwipe(int fingers, double dx, double dy) {
    noteUserActivity();
    const ShortcutAction act =
        input_router_.recognize_trackpad_swipe(fingers, dx, dy, session_locked_);
    executeShortcutAction(act);
}

void CompositorServer::dispatchTrackpadPinch(int fingers, double scale) {
    noteUserActivity();
    const ShortcutAction act =
        input_router_.recognize_trackpad_pinch(fingers, scale, session_locked_);
    executeShortcutAction(act);
}

void CompositorServer::writeShellActionFile(const QString& action) {
    const QString path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/shell-action");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write((action + QLatin1Char('\n')).toUtf8());
        f.close();
    }
}

void CompositorServer::executeShortcutAction(ShortcutAction act) {
    switch (act) {
        case ShortcutAction::RequestLock:
            request_lock();
            break;
        case ShortcutAction::ToggleOverview:
            toggleOverview();
            break;
        case ShortcutAction::CycleFocusForward:
            workspace_.cycle_focus(true);
            window_model_->syncGeometryFrames();
            writeWindowStateFile();
            scheduleBackdropRefresh();
            break;
        case ShortcutAction::CycleFocusBackward:
            workspace_.cycle_focus(false);
            window_model_->syncGeometryFrames();
            writeWindowStateFile();
            scheduleBackdropRefresh();
            break;
        case ShortcutAction::SnapLeft:
            snapWindowLeft(workspace_.focused_window_id());
            break;
        case ShortcutAction::SnapRight:
            snapWindowRight(workspace_.focused_window_id());
            break;
        case ShortcutAction::ToggleMaximize:
            toggleMaximize(workspace_.focused_window_id());
            break;
        case ShortcutAction::ToggleShowDesktop:
            workspace_.toggle_show_desktop();
            if (!frame_timer_.isActive()) { frame_clock_.restart(); frame_timer_.start(); }
            window_model_->syncGeometryFrames();
            writeWindowStateFile();
            scheduleBackdropRefresh();
            break;
        case ShortcutAction::CloseFocused:
            if (workspace_.focused_window_id() != 0) {
                closeWindow(workspace_.focused_window_id());
            }
            break;
        case ShortcutAction::SwitchDesktopPrev:
            workspace_.switch_desktop_relative(-1);
            window_model_->syncGeometryFrames();
            writeWindowStateFile();
            scheduleBackdropRefresh();
            emit stateChanged();
            break;
        case ShortcutAction::SwitchDesktopNext:
            workspace_.switch_desktop_relative(1);
            window_model_->syncGeometryFrames();
            writeWindowStateFile();
            scheduleBackdropRefresh();
            emit stateChanged();
            break;
        case ShortcutAction::ToggleLauncher:
            writeShellActionFile(QStringLiteral("home"));
            emit shellActionRequested(QStringLiteral("home"));
            break;
        case ShortcutAction::ToggleSearch:
            writeShellActionFile(QStringLiteral("search"));
            emit shellActionRequested(QStringLiteral("search"));
            break;
        default:
            break;
    }
}

bool CompositorServer::eventFilter(QObject* watched, QEvent* event) {
    if (watched == output_window_ && event) {
        if (event->type() == QEvent::NativeGesture) {
            auto* ng = static_cast<QNativeGestureEvent*>(event);
            switch (ng->gestureType()) {
                case Qt::BeginNativeGesture:
                    gesture_fingers_ = std::max(3, ng->fingerCount());
                    gesture_dx_ = 0.0;
                    gesture_dy_ = 0.0;
                    gesture_scale_ = 1.0;
                    gesture_pinch_active_ = false;
                    break;
                case Qt::PanNativeGesture:
                    gesture_dx_ += ng->delta().x();
                    gesture_dy_ += ng->delta().y();
                    break;
                case Qt::ZoomNativeGesture:
                    gesture_pinch_active_ = true;
                    gesture_scale_ *= (1.0 + ng->value());
                    break;
                case Qt::SwipeNativeGesture: {
                    const double deg = ng->value();
                    const double rad = deg * 3.14159265358979323846 / 180.0;
                    dispatchTrackpadSwipe(std::max(3, ng->fingerCount()),
                                          std::cos(rad) * 80.0,
                                          -std::sin(rad) * 80.0);
                    break;
                }
                case Qt::EndNativeGesture:
                    if (gesture_pinch_active_) {
                        dispatchTrackpadPinch(gesture_fingers_, gesture_scale_);
                    } else if (std::abs(gesture_dx_) > 1.0 || std::abs(gesture_dy_) > 1.0) {
                        dispatchTrackpadSwipe(gesture_fingers_, gesture_dx_, gesture_dy_);
                    }
                    gesture_fingers_ = 0;
                    gesture_dx_ = 0.0;
                    gesture_dy_ = 0.0;
                    gesture_scale_ = 1.0;
                    gesture_pinch_active_ = false;
                    break;
                default:
                    break;
            }
        } else if (event->type() == QEvent::TouchBegin ||
                   event->type() == QEvent::TouchUpdate ||
                   event->type() == QEvent::TouchEnd) {
            auto* te = static_cast<QTouchEvent*>(event);
            const auto pts = te->points();
            if (!pts.isEmpty()) {
                QPointF center(0.0, 0.0);
                for (const auto& pt : pts) center += pt.position();
                center /= static_cast<qreal>(pts.size());
                if (event->type() == QEvent::TouchBegin || pts.size() > touch_max_fingers_) {
                    touch_start_center_ = center;
                    touch_max_fingers_ = static_cast<int>(pts.size());
                }
                if (event->type() == QEvent::TouchEnd) {
                    if (touch_max_fingers_ >= 3) {
                        const QPointF d = center - touch_start_center_;
                        dispatchTrackpadSwipe(touch_max_fingers_, d.x(), d.y());
                    }
                    touch_max_fingers_ = 0;
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void CompositorServer::startLibinputGestures() {
#ifdef FINN_HAVE_LIBINPUT
    if (libinput_ctx_) return;
    static const struct libinput_interface kIface = {
        [](const char* path, int flags, void*) -> int {
            const int fd = ::open(path, flags | O_NONBLOCK | O_CLOEXEC);
            return fd < 0 ? -errno : fd;
        },
        [](int fd, void*) {
            if (fd >= 0) ::close(fd);
        },
    };
    struct udev* udev = udev_new();
    if (!udev) return;
    struct libinput* li = libinput_udev_create_context(&kIface, nullptr, udev);
    if (!li) {
        udev_unref(udev);
        return;
    }
    if (libinput_udev_assign_seat(li, "seat0") != 0) {
        libinput_unref(li);
        udev_unref(udev);
        return;
    }
    udev_ctx_ = udev;
    libinput_ctx_ = li;
    const int fd = libinput_get_fd(li);
    if (fd >= 0) {
        libinput_notifier_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(libinput_notifier_, &QSocketNotifier::activated, this,
                &CompositorServer::handleLibinputEvents);
    }
#endif
}

void CompositorServer::stopLibinputGestures() {
#ifdef FINN_HAVE_LIBINPUT
    if (libinput_notifier_) {
        libinput_notifier_->setEnabled(false);
        libinput_notifier_->deleteLater();
        libinput_notifier_ = nullptr;
    }
    if (libinput_ctx_) {
        libinput_unref(static_cast<struct libinput*>(libinput_ctx_));
        libinput_ctx_ = nullptr;
    }
    if (udev_ctx_) {
        udev_unref(static_cast<struct udev*>(udev_ctx_));
        udev_ctx_ = nullptr;
    }
#endif
}

void CompositorServer::handleLibinputEvents() {
#ifdef FINN_HAVE_LIBINPUT
    auto* li = static_cast<struct libinput*>(libinput_ctx_);
    if (!li) return;
    libinput_dispatch(li);
    while (struct libinput_event* ev = libinput_get_event(li)) {
        const enum libinput_event_type type = libinput_event_get_type(ev);
        if (type == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN) {
            auto* g = libinput_event_get_gesture_event(ev);
            gesture_fingers_ = libinput_event_gesture_get_finger_count(g);
            gesture_dx_ = 0.0;
            gesture_dy_ = 0.0;
        } else if (type == LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE) {
            auto* g = libinput_event_get_gesture_event(ev);
            gesture_dx_ += libinput_event_gesture_get_dx(g);
            gesture_dy_ += libinput_event_gesture_get_dy(g);
        } else if (type == LIBINPUT_EVENT_GESTURE_SWIPE_END) {
            auto* g = libinput_event_get_gesture_event(ev);
            if (!libinput_event_gesture_get_cancelled(g)) {
                dispatchTrackpadSwipe(gesture_fingers_, gesture_dx_, gesture_dy_);
            }
            gesture_fingers_ = 0;
            gesture_dx_ = 0.0;
            gesture_dy_ = 0.0;
        } else if (type == LIBINPUT_EVENT_GESTURE_PINCH_BEGIN) {
            auto* g = libinput_event_get_gesture_event(ev);
            gesture_fingers_ = libinput_event_gesture_get_finger_count(g);
            gesture_scale_ = 1.0;
        } else if (type == LIBINPUT_EVENT_GESTURE_PINCH_UPDATE) {
            auto* g = libinput_event_get_gesture_event(ev);
            gesture_scale_ = libinput_event_gesture_get_scale(g);
        } else if (type == LIBINPUT_EVENT_GESTURE_PINCH_END) {
            auto* g = libinput_event_get_gesture_event(ev);
            if (!libinput_event_gesture_get_cancelled(g)) {
                dispatchTrackpadPinch(gesture_fingers_, gesture_scale_);
            }
            gesture_fingers_ = 0;
            gesture_scale_ = 1.0;
        }
        libinput_event_destroy(ev);
    }
#endif
}

void CompositorServer::noteUserActivity() {
    last_activity_msec_ = monotonic_now_ms();
}

void CompositorServer::reloadWallpaperQuarter() {
    const auto out = workspace_.outputs().empty()
                         ? RectI{0, 0, 1280, 800}
                         : workspace_.outputs().front().geometry;
    const QSize out_sz(std::max(64, out.width), std::max(64, out.height));
    const auto st = finn::sys::load_merged_settings();
    QString wp = QString::fromStdString(st.wallpaper);
    if (wp.isEmpty() || !QFileInfo::exists(wp)) {
        for (const QString& cand : {
                 QString::fromLatin1(finn::kWallpaperDir) + QStringLiteral("/default.webp"),
                 QStringLiteral("/usr/share/finn/wallpaper/default.webp"),
                 QStringLiteral("assets/wallpaper/default.webp")}) {
            if (QFileInfo::exists(cand)) {
                wp = cand;
                break;
            }
        }
    }
    if (!wallpaper_quarter_.isNull() && wallpaper_output_size_ == out_sz &&
        wallpaper_path_cache_ == wp && wallpaper_dark_cache_ == dark_theme_) {
        return;
    }

    const QSize qsz(std::max(16, out_sz.width() / 4), std::max(16, out_sz.height() / 4));
    QImage qimg(qsz, QImage::Format_ARGB32_Premultiplied);
    qimg.fill(dark_theme_ ? QColor(11, 14, 20) : QColor(232, 238, 247));

    QImage raw(wp);
    if (!raw.isNull()) {
        QImage scaled = raw.scaled(qsz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int ox = std::max(0, (scaled.width() - qsz.width()) / 2);
        const int oy = std::max(0, (scaled.height() - qsz.height()) / 2);
        qimg = scaled.copy(ox, oy, qsz.width(), qsz.height());
    }
    if (dark_theme_) {
        QPainter dp(&qimg);
        dp.fillRect(qimg.rect(), QColor(13, 15, 20, 90));
    }

    wallpaper_quarter_ = std::move(qimg);
    wallpaper_output_size_ = out_sz;
    wallpaper_path_cache_ = wp;
    wallpaper_dark_cache_ = dark_theme_;
}

void CompositorServer::scheduleBackdropRefresh(std::uint64_t dirty_window_id) {
    if (dirty_window_id != 0) {
        dirty_thumbs_.insert(dirty_window_id);
    }
    if (!backdrop_timer_.isActive()) {
        backdrop_timer_.start();
    }
}

void CompositorServer::flushBackdropRefresh() {
    reloadWallpaperQuarter();
#ifdef FINN_HAVE_QT_WAYLAND_COMPOSITOR
    if (!dirty_thumbs_.isEmpty()) {
        const QSet<std::uint64_t> pending = dirty_thumbs_;
        dirty_thumbs_.clear();
        for (std::uint64_t wid : pending) {
            auto* top = toplevel_map_.value(wid, nullptr);
            if (!top || !top->xdgSurface() || !top->xdgSurface()->surface()) continue;
            auto* surf = top->xdgSurface()->surface();
            if (auto* view = surf->primaryView()) {
                QWaylandBufferRef buf = view->currentBuffer();
                if (buf.hasContent()) {
                    const QImage img = buf.image();
                    if (!img.isNull()) {
                        const int qw = std::max(8, img.width() / 4);
                        const int qh = std::max(8, img.height() / 4);
                        surface_thumbs_.insert(
                            wid,
                            img.scaled(qw, qh, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                .convertToFormat(QImage::Format_ARGB32_Premultiplied));
                    }
                }
            }
        }
    }
#endif
    ++backdrop_version_;
    emit backdropChanged();
}

void CompositorServer::pollOverlayBlurRects() {
    const QString path =
        QString::fromLatin1(finn::kCompositorStateDir) + QStringLiteral("/blur-rects.tsv");
    QFileInfo fi(path);
    if (!fi.exists()) return;
    const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
    if (mt == blur_rects_mtime_) return;
    blur_rects_mtime_ = mt;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QVariantList next;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList p = line.split(QLatin1Char('\t'));
        if (p.size() < 5) continue;
        QVariantMap m;
        m.insert(QStringLiteral("x"), p[0].toDouble());
        m.insert(QStringLiteral("y"), p[1].toDouble());
        m.insert(QStringLiteral("w"), p[2].toDouble());
        m.insert(QStringLiteral("h"), p[3].toDouble());
        m.insert(QStringLiteral("r"), p[4].toDouble());
        next.append(m);
    }
    overlay_blur_rects_ = std::move(next);
    emit overlayBlurRectsChanged();
}

QImage CompositorServer::sampleBlurredBackdrop(quint64 excludeWindowId,
                                               const QRectF& screenRect) const {
    if (screenRect.width() <= 0.0 || screenRect.height() <= 0.0) return {};
    if (wallpaper_quarter_.isNull()) return {};

    constexpr double kScale = 0.25;
    constexpr int kMargin = 6;
    const int qx = static_cast<int>(std::floor(screenRect.x() * kScale));
    const int qy = static_cast<int>(std::floor(screenRect.y() * kScale));
    const int qw = std::max(2, static_cast<int>(std::ceil(screenRect.width() * kScale)));
    const int qh = std::max(2, static_cast<int>(std::ceil(screenRect.height() * kScale)));

    const int cw = qw + 2 * kMargin;
    const int ch = qh + 2 * kMargin;
    QImage crop(cw, ch, QImage::Format_ARGB32_Premultiplied);
    crop.fill(dark_theme_ ? QColor(11, 14, 20) : QColor(232, 238, 247));

    QPainter cp(&crop);
    cp.setRenderHint(QPainter::SmoothPixmapTransform, false);
    // 1. Draw the quarter-res wallpaper behind the crop
    cp.drawImage(QPointF(-(qx - kMargin), -(qy - kMargin)), wallpaper_quarter_);

    // 2. Find target z-index (0 = fullscreen shell overlay above all normal windows)
    int target_z = std::numeric_limits<int>::max();
    const auto& wins = workspace_.windows();
    if (excludeWindowId != 0) {
        for (std::size_t i = 0; i < wins.size(); ++i) {
            if (wins[i].id == excludeWindowId) {
                target_z = wins[i].z_index(static_cast<int>(i) + (wins[i].focused ? 500 : 0));
                break;
            }
        }
    }

    // 3. Composite all visible lower windows in ascending z-order!
    struct DrawWin {
        int z;
        const WindowNode* node;
    };
    std::vector<DrawWin> lower;
    lower.reserve(wins.size());
    const QRectF expanded = screenRect.adjusted(-24.0, -24.0, 24.0, 24.0);
    for (std::size_t i = 0; i < wins.size(); ++i) {
        const auto& w = wins[i];
        if (w.id == excludeWindowId || w.fullscreen || w.minimized || w.spring_opacity.value() < 0.05) {
            continue;
        }
        if (w.desktop_index >= 0 && w.desktop_index != workspace_.current_desktop()) continue;
        const int wz = w.z_index(static_cast<int>(i) + (w.focused ? 500 : 0));
        if (wz >= target_z) continue;
        const QRectF wr(w.spring_x.value(), w.spring_y.value(),
                        w.spring_w.value(), w.spring_h.value());
        if (!wr.intersects(expanded)) continue;
        lower.push_back(DrawWin{wz, &w});
    }
    std::sort(lower.begin(), lower.end(),
              [](const DrawWin& a, const DrawWin& b) { return a.z < b.z; });

    for (const auto& dw : lower) {
        const QImage thumb = surface_thumbs_.value(dw.node->id);
        if (thumb.isNull()) continue;
        const QRectF dst(dw.node->spring_x.value() * kScale - (qx - kMargin),
                         dw.node->spring_y.value() * kScale - (qy - kMargin),
                         dw.node->spring_w.value() * kScale,
                         dw.node->spring_h.value() * kScale);
        cp.setOpacity(dw.node->spring_opacity.value());
        cp.drawImage(dst, thumb);
    }
    cp.end();

    // 4. Fast 2-pass separable box blur (radius=4 at 1/4 res == 16px real-time frost)
    auto box_blur_pass = [](QImage& img, int rad) {
        const int w = img.width();
        const int h = img.height();
        const int span = 2 * rad + 1;
        QImage tmp(w, h, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(tmp.scanLine(y));
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int i = -rad; i <= rad; ++i) {
                const QRgb c = src[std::clamp(i, 0, w - 1)];
                sr += qRed(c); sg += qGreen(c); sb += qBlue(c); sa += qAlpha(c);
            }
            for (int x = 0; x < w; ++x) {
                dst[x] = qRgba(sr / span, sg / span, sb / span, sa / span);
                const QRgb out_c = src[std::clamp(x - rad, 0, w - 1)];
                const QRgb in_c = src[std::clamp(x + rad + 1, 0, w - 1)];
                sr += qRed(in_c) - qRed(out_c);
                sg += qGreen(in_c) - qGreen(out_c);
                sb += qBlue(in_c) - qBlue(out_c);
                sa += qAlpha(in_c) - qAlpha(out_c);
            }
        }
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int i = -rad; i <= rad; ++i) {
                const QRgb c = reinterpret_cast<const QRgb*>(tmp.constScanLine(std::clamp(i, 0, h - 1)))[x];
                sr += qRed(c); sg += qGreen(c); sb += qBlue(c); sa += qAlpha(c);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(img.scanLine(y))[x] =
                    qRgba(sr / span, sg / span, sb / span, sa / span);
                const QRgb out_c = reinterpret_cast<const QRgb*>(tmp.constScanLine(std::clamp(y - rad, 0, h - 1)))[x];
                const QRgb in_c = reinterpret_cast<const QRgb*>(tmp.constScanLine(std::clamp(y + rad + 1, 0, h - 1)))[x];
                sr += qRed(in_c) - qRed(out_c);
                sg += qGreen(in_c) - qGreen(out_c);
                sb += qBlue(in_c) - qBlue(out_c);
                sa += qAlpha(in_c) - qAlpha(out_c);
            }
        }
    };

    box_blur_pass(crop, 4);
    box_blur_pass(crop, 3);
    return crop.copy(kMargin, kMargin, qw, qh);
}

}  // namespace finn::comp
