// Background wallpaper daemon rendered natively via Qt6 + LayerShellQt.
// Displays the configured or default FinnOS wallpaper across outputs without
// stealing pointer input and responds live to dark/light & wallpaper changes.

#include <FinnSDK/FinnSDK.hpp>
#include "common/paths.hpp"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdio>
#include <cstring>

namespace {

class WallpaperWindow final : public finn::ui::FinnLayerWindow {
public:
    WallpaperWindow()
        : FinnLayerWindow(Layer::Background, QStringLiteral("finn-wallpaper")) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::None);
        setInputRegions({});
        loadWallpaper();
        connect(&finn::ui::FinnThemeManager::instance(),
                &finn::ui::FinnThemeManager::themeChanged, this, [this]() {
                    loadWallpaper();
                    refreshFullSurface();
                });

        // Belt-and-suspenders check so atomic settings replacements apply live
        // even if a filesystem watcher event is coalesced across restarts.
        auto* reload_poll = new QTimer(this);
        reload_poll->setInterval(1000);
        connect(reload_poll, &QTimer::timeout, this, []() {
            finn::ui::FinnThemeManager::instance().reload();
        });
        reload_poll->start();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        FinnLayerWindow::resizeEvent(event);
        rebuildScaledCache();
        refreshFullSurface();
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(rect(), t.bone);

        if (scaled_cache_.isNull() && !source_img_.isNull() && !size().isEmpty()) {
            rebuildScaledCache();
        }
        if (!scaled_cache_.isNull()) {
            p.setOpacity(1.0);
            p.drawPixmap(0, 0, scaled_cache_);
        }
        // Subtle dark-mode scrim preserves contrast behind light dock ink without dimming light mode.
        if (t.dark) {
            QColor scrim = t.bone;
            scrim.setAlpha(70);
            p.fillRect(rect(), scrim);
        }
    }

private:
    QString resolveWallpaperPath() const {
        const QString custom =
            finn::ui::FinnThemeManager::instance().wallpaperPath();
        const QString wp_dir = QString::fromLatin1(finn::kWallpaperDir);
        const QStringList candidates = {
            custom,
            wp_dir + QStringLiteral("/default.webp"),
            QStringLiteral("assets/wallpaper/default.webp"),
            wp_dir + QStringLiteral("/Desktop.jpg"),
            QStringLiteral("assets/figma/wallpaper/Desktop.jpg"),
        };
        for (const QString& cand : candidates) {
            if (!cand.isEmpty() && QFileInfo::exists(cand)) return cand;
        }
        return {};
    }

    void loadWallpaper() {
        const QString next_path = resolveWallpaperPath();
        if (next_path == loaded_path_ && !source_img_.isNull()) {
            const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
            const QSize expected_px(qRound(width() * dpr), qRound(height() * dpr));
            if (!size().isEmpty() &&
                (scaled_cache_.isNull() || scaled_cache_.size() != expected_px ||
                 !qFuzzyCompare(scaled_cache_.devicePixelRatio(), dpr))) {
                rebuildScaledCache();
            }
            update();
            return;
        }
        loaded_path_ = next_path;
        source_img_ = QImage();
        scaled_cache_ = QPixmap();

        if (loaded_path_.isEmpty()) {
            if (!warned_) {
                std::fprintf(stderr,
                             "finn-wallpaper: default wallpaper missing; using theme bone\n");
                warned_ = true;
            }
            update();
            return;
        }
        if (!source_img_.load(loaded_path_)) {
            std::fprintf(stderr,
                         "finn-wallpaper: failed to decode %s; using theme bone\n",
                         loaded_path_.toUtf8().constData());
            update();
            return;
        }
        rebuildScaledCache();
        update();
    }

    void rebuildScaledCache() {
        if (source_img_.isNull() || size().isEmpty()) {
            scaled_cache_ = QPixmap();
            return;
        }
        const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
        const QSize target_px(qRound(width() * dpr), qRound(height() * dpr));
        if (target_px.isEmpty()) return;

        const QImage cover = source_img_.scaled(
            target_px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int crop_x = std::max(0, (cover.width() - target_px.width()) / 2);
        const int crop_y = std::max(0, (cover.height() - target_px.height()) / 2);
        QImage cropped =
            cover.copy(crop_x, crop_y, target_px.width(), target_px.height());
        cropped.setDevicePixelRatio(dpr);
        scaled_cache_ = QPixmap::fromImage(cropped);
    }

    void refreshFullSurface() {
        if (!isVisible() || size().isEmpty()) {
            update();
            return;
        }
        // Temporarily clear the 1x1 mask across synchronous repaint so Qt's
        // dirty-region tracking flushes the full surface, then immediately
        // restore the empty input region without leaving an unmasked window.
        clearMask();
        repaint();
        setInputRegions({});
    }

    QString loaded_path_;
    QImage source_img_;
    QPixmap scaled_cache_;
    bool warned_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-wallpaper 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-wallpaper [--version]");
            return 0;
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("finn-wallpaper"),
                                  QStringLiteral("Finn Wallpaper"));
    WallpaperWindow win;
    win.show();
    return app.exec();
}
