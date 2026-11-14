// Finn Viewer: native image, wallpaper, and SVG vector inspector redesigned to
// match Figma Settings (20:64) 1:1 with ChromeMode::FigmaSplit, 250px sidebar
// with 26x26 circular colored badges, quick presets (Wallpapers, System Icons,
// Pointers, Open File...), live folder image list, and an r=6 canvas card with
// adaptive checkerboard.

#include <FinnSDK/FinnSDK.hpp>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

using namespace finn::ui;

QString rgba_css(const QColor& c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

QString scrollbar_qss(const FinnTokens& t) {
    const QColor thumb(t.ink.red(), t.ink.green(), t.ink.blue(), t.dark ? 68 : 52);
    const QColor hover(t.ink.red(), t.ink.green(), t.ink.blue(), t.dark ? 112 : 88);
    return QStringLiteral(
               "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
               "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 28px; }"
               "QScrollBar::handle:vertical:hover { background: %2; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(rgba_css(thumb), rgba_css(hover));
}

void apply_header_16px_regular(FinnLabel* lbl) {
    if (!lbl) return;
    auto sync = [lbl]() {
        QFont f = finn_font(12.0, QFont::Normal);
        f.setPixelSize(16);
        lbl->setFont(f);
    };
    sync();
    QObject::connect(&FinnThemeManager::instance(),
                     &FinnThemeManager::themeChanged,
                     lbl, sync);
}

bool is_supported_image_ext(const QString& suffix) {
    const QString ext = suffix.toLower();
    if (ext == QStringLiteral("svg") || ext == QStringLiteral("svgz")) return true;
    for (const QByteArray& fmt : QImageReader::supportedImageFormats()) {
        if (QString::fromLatin1(fmt).toLower() == ext) return true;
    }
    return false;
}

QString resolve_preset_dir(const QStringList& candidates) {
    for (const QString& c : candidates) {
        if (QFileInfo(c).isDir()) return QDir(c).absolutePath();
    }
    return QDir::homePath();
}

class ImageCanvas final : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void clearImage() {
        image_ = QImage();
        svg_.reset();
        rotation_deg_ = 0;
        fit_mode_ = true;
        zoom_ = 1.0;
        pan_ = QPointF();
        update();
    }

    bool loadImage(const QString& path, QString* format_out, QSize* size_out, QString* err_out) {
        clearImage();
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            if (err_out) *err_out = QStringLiteral("File does not exist.");
            return false;
        }
        const QString ext = fi.suffix().toLower();
        if (ext == QStringLiteral("svg") || ext == QStringLiteral("svgz")) {
            auto renderer = std::make_unique<QSvgRenderer>(path);
            if (!renderer->isValid()) {
                if (err_out) *err_out = QStringLiteral("Invalid SVG vector file.");
                return false;
            }
            const QSize def = renderer->defaultSize().isEmpty()
                                  ? QSize(512, 512)
                                  : renderer->defaultSize();
            svg_ = std::move(renderer);
            if (format_out) *format_out = QStringLiteral("SVG");
            if (size_out) *size_out = def;
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QByteArray fmt = reader.format();
            const QImage img = reader.read();
            if (img.isNull()) {
                if (err_out) *err_out = reader.errorString();
                return false;
            }
            image_ = img;
            if (format_out) *format_out = QString::fromLatin1(fmt).toUpper();
            if (size_out) *size_out = img.size();
        }
        fit_mode_ = true;
        recomputeFitZoom();
        update();
        emit zoomChanged(effectiveZoom());
        return true;
    }

    [[nodiscard]] bool hasImage() const { return !image_.isNull() || svg_ != nullptr; }
    [[nodiscard]] double effectiveZoom() const { return zoom_; }
    [[nodiscard]] bool checkerEnabled() const { return checker_; }

    void setChecker(bool on) {
        checker_ = on;
        update();
    }

    void fitToWindow() {
        if (!hasImage()) return;
        fit_mode_ = true;
        pan_ = QPointF();
        recomputeFitZoom();
        update();
        emit zoomChanged(zoom_);
    }

    void setZoom(double z) {
        if (!hasImage()) return;
        fit_mode_ = false;
        zoom_ = qBound(0.05, z, 16.0);
        update();
        emit zoomChanged(zoom_);
    }

    void zoomBy(double factor) { setZoom(zoom_ * factor); }

    void rotate90() {
        if (!hasImage()) return;
        rotation_deg_ = (rotation_deg_ + 90) % 360;
        if (fit_mode_) recomputeFitZoom();
        update();
        emit zoomChanged(zoom_);
    }

signals:
    void zoomChanged(double zoom);

protected:
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        if (fit_mode_ && hasImage()) {
            recomputeFitZoom();
            emit zoomChanged(zoom_);
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto& t = FinnThemeManager::instance().tokens();
        const QRectF card_r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(card_r, kRadiusMd, kRadiusMd);
        p.save();
        p.setClipPath(clip);
        p.fillRect(card_r, t.tile);

        if (hasImage()) {
            const QSizeF base_sz = baseLogicalSize();
            const QSizeF rot_sz = (rotation_deg_ % 180 == 0)
                                      ? base_sz
                                      : QSizeF(base_sz.height(), base_sz.width());
            const QSizeF draw_sz = rot_sz * zoom_;
            const QPointF center = QPointF(width() * 0.5, height() * 0.5) + pan_;
            const QRectF target(center.x() - draw_sz.width() * 0.5,
                                center.y() - draw_sz.height() * 0.5,
                                draw_sz.width(), draw_sz.height());

            const bool is_transparent =
                (svg_ != nullptr) || (!image_.isNull() && image_.hasAlphaChannel());
            if (checker_ && is_transparent) {
                p.save();
                const QRectF pad_target = target.adjusted(-8.0, -8.0, 8.0, 8.0);
                QPainterPath chk_clip;
                chk_clip.addRoundedRect(pad_target, kRadiusMd, kRadiusMd);
                p.setClipPath(chk_clip);
                const int step = 12;
                // Adaptive checkerboard: for SVGs (which often use dark strokes) or
                // transparent PNGs in Dark Mode, use a crisp contrasting two-tone grid
                // so dark icons are 100% visible in Dark Mode.
                const QColor c1 = svg_
                                      ? (t.dark ? QColor(206, 212, 222) : QColor(235, 238, 244))
                                      : (t.dark ? QColor(46, 52, 64) : QColor(235, 238, 244));
                const QColor c2 = svg_
                                      ? (t.dark ? QColor(226, 231, 239) : QColor(250, 251, 253))
                                      : (t.dark ? QColor(62, 70, 86) : QColor(250, 251, 253));
                const int x0 = static_cast<int>(pad_target.left());
                const int y0 = static_cast<int>(pad_target.top());
                for (int y = y0; y < static_cast<int>(pad_target.bottom()) + step; y += step) {
                    for (int x = x0; x < static_cast<int>(pad_target.right()) + step; x += step) {
                        const bool odd = (((x - x0) / step) + ((y - y0) / step)) % 2 != 0;
                        p.fillRect(QRect(x, y, step, step), odd ? c2 : c1);
                    }
                }
                p.restore();
            }

            p.save();
            p.translate(center);
            p.rotate(rotation_deg_);
            const QRectF local(-base_sz.width() * zoom_ * 0.5,
                               -base_sz.height() * zoom_ * 0.5,
                               base_sz.width() * zoom_,
                               base_sz.height() * zoom_);
            if (svg_) {
                svg_->render(&p, local);
            } else {
                p.drawImage(local, image_);
            }
            p.restore();
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(card_r, kRadiusMd, kRadiusMd);
    }

    void wheelEvent(QWheelEvent* e) override {
        if (!hasImage()) {
            QWidget::wheelEvent(e);
            return;
        }
        const double delta = e->angleDelta().y();
        if (delta != 0.0) {
            zoomBy(delta > 0 ? 1.15 : (1.0 / 1.15));
            e->accept();
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && hasImage()) {
            dragging_ = true;
            last_pos_ = e->position();
            setCursor(Qt::ClosedHandCursor);
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            const QPointF delta = e->position() - last_pos_;
            last_pos_ = e->position();
            fit_mode_ = false;
            pan_ += delta;
            update();
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (dragging_ && e->button() == Qt::LeftButton) {
            dragging_ = false;
            unsetCursor();
        }
        QWidget::mouseReleaseEvent(e);
    }

private:
    QSizeF baseLogicalSize() const {
        if (svg_) {
            const QSize def = svg_->defaultSize();
            return def.isEmpty() ? QSizeF(256, 256) : QSizeF(def);
        }
        return QSizeF(image_.size());
    }

    void recomputeFitZoom() {
        const QSizeF base_sz = baseLogicalSize();
        const QSizeF rot_sz = (rotation_deg_ % 180 == 0)
                                  ? base_sz
                                  : QSizeF(base_sz.height(), base_sz.width());
        if (rot_sz.width() <= 0 || rot_sz.height() <= 0 || width() <= 40 || height() <= 40) {
            zoom_ = 1.0;
            return;
        }
        const double zx = (width() - 40.0) / rot_sz.width();
        const double zy = (height() - 40.0) / rot_sz.height();
        zoom_ = qBound(0.05, std::min(zx, zy), 6.0);
    }

    QImage image_;
    std::unique_ptr<QSvgRenderer> svg_;
    int rotation_deg_ = 0;
    bool fit_mode_ = true;
    bool checker_ = true;
    double zoom_ = 1.0;
    QPointF pan_;
    bool dragging_ = false;
    QPointF last_pos_;
};

class ViewerWindow final : public FinnWindow {
    Q_OBJECT
public:
    explicit ViewerWindow(const QString& initial_file)
        : FinnWindow(QStringLiteral("Finn Viewer"), QStringLiteral("viewer")) {
        setMinimumSize(800, 500);
        resize(940, 600);
        setChromeMode(ChromeMode::FigmaSplit);

        setupSidebar();
        setupContent();

        new QShortcut(QKeySequence::Open, this, [this]() { openDialog(); });
        new QShortcut(QKeySequence(Qt::Key_Left), this, [this]() { stepSibling(-1); });
        new QShortcut(QKeySequence(Qt::Key_Right), this, [this]() { stepSibling(1); });
        new QShortcut(QKeySequence(Qt::Key_0), this, [this]() { canvas_->fitToWindow(); });
        new QShortcut(QKeySequence(Qt::Key_1), this, [this]() { canvas_->setZoom(1.0); });
        new QShortcut(QKeySequence(Qt::Key_Plus), this, [this]() { canvas_->zoomBy(1.2); });
        new QShortcut(QKeySequence(Qt::Key_Equal), this, [this]() { canvas_->zoomBy(1.2); });
        new QShortcut(QKeySequence(Qt::Key_Minus), this, [this]() { canvas_->zoomBy(1.0 / 1.2); });
        new QShortcut(QKeySequence(Qt::Key_R), this, [this]() { canvas_->rotate90(); });

        updateControlStates();
        if (!initial_file.isEmpty()) {
            openFile(initial_file);
        } else {
            loadPresetFolder(QStringLiteral("wallpapers"));
        }
    }

private:
    void setupSidebar() {
        // Left 250px sidebar matching Figma Settings (20:64) with quick presets
        // and scrollable list of images in the current folder.
        sidebar_ = new QWidget(this);
        sidebar_->setFixedWidth(250);
        auto* side_root = new QVBoxLayout(sidebar_);
        side_root->setContentsMargins(12, 24, 12, 12);
        side_root->setSpacing(10);

        auto* hdr_lbl = new FinnLabel(
            QStringLiteral("Viewer"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, sidebar_);
        apply_header_16px_regular(hdr_lbl);
        side_root->addWidget(hdr_lbl);

        sidebar_search_ = new FinnSearchField(QStringLiteral("Filter images"), sidebar_);
        sidebar_search_->setFixedHeight(30);
        sidebar_search_->setPillMode(true);
        connect(sidebar_search_, &FinnSearchField::textChanged,
                this, [this](const QString&) { rebuildSidebarImages(); });
        side_root->addWidget(sidebar_search_);

        // Quick preset items with 26x26 circular colored badges
        auto* presets_col = new QVBoxLayout();
        presets_col->setContentsMargins(0, 0, 0, 2);
        presets_col->setSpacing(4);

        auto add_preset = [&](const QString& id, const QString& icon,
                              const QString& label, const QColor& badge) {
            auto* item = new FinnNavItem(icon, label, sidebar_);
            item->setBadgeColor(badge);
            presets_col->addWidget(item);
            preset_items_.push_back({id, item});
            connect(item, &QAbstractButton::clicked, this, [this, id]() {
                if (id == QLatin1String("open")) {
                    openDialog();
                } else {
                    loadPresetFolder(id);
                }
            });
        };

        add_preset(QStringLiteral("wallpapers"), QStringLiteral("display"),
                   QStringLiteral("Wallpapers"), QColor(0x00, 0x7B, 0xFF));
        add_preset(QStringLiteral("icons"), QStringLiteral("paint-brush"),
                   QStringLiteral("System Icons"), QColor(0x58, 0x56, 0xD6));
        add_preset(QStringLiteral("pointers"), QStringLiteral("cursor"),
                   QStringLiteral("Pointers"), QColor(0x22, 0xC5, 0x5E));
        add_preset(QStringLiteral("open"), QStringLiteral("folder"),
                   QStringLiteral("Open File..."), QColor(0xF5, 0x9E, 0x0B));
        side_root->addLayout(presets_col);

        folder_caption_ = new FinnLabel(
            QStringLiteral("IMAGES IN FOLDER"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, sidebar_);
        folder_caption_->setContentsMargins(6, 4, 6, 0);
        side_root->addWidget(folder_caption_);

        side_scroll_ = new QScrollArea(sidebar_);
        side_scroll_->setWidgetResizable(true);
        side_scroll_->setFrameShape(QFrame::NoFrame);
        side_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        side_scroll_->setAutoFillBackground(false);
        side_scroll_->viewport()->setAutoFillBackground(false);

        auto* list_host = new QWidget(side_scroll_);
        list_host->setAutoFillBackground(false);
        images_list_layout_ = new QVBoxLayout(list_host);
        images_list_layout_->setContentsMargins(0, 0, 0, 4);
        images_list_layout_->setSpacing(4);
        side_scroll_->setWidget(list_host);
        side_root->addWidget(side_scroll_, 1);

        auto sync_scroll_qss = [this]() {
            if (!side_scroll_) return;
            const auto& t = FinnThemeManager::instance().tokens();
            side_scroll_->setStyleSheet(
                QStringLiteral("QScrollArea { background: transparent; border: none; }%1")
                    .arg(scrollbar_qss(t)));
        };
        sync_scroll_qss();
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, sync_scroll_qss);

        setSidebarWidget(sidebar_);
    }

    void setupContent() {
        auto* content = new QWidget(this);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(12, 24, 12, 14);
        root->setSpacing(10);

        // Clean header row (16px Regular filename + Zoom Out, 100%, Fit, Zoom In,
        // Rotate, Checkerboard pills to the left of FinnWindowControls)
        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(6);

        title_label_ = new FinnLabel(
            QStringLiteral("Viewer"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, content);
        apply_header_16px_regular(title_label_);
        header_row->addWidget(title_label_, 1);

        zoom_out_btn_ = new FinnButton(
            QStringLiteral("Zoom -"), FinnButton::Variant::Secondary, {}, content);
        zoom_out_btn_->setFixedHeight(28);

        actual_btn_ = new FinnButton(
            QStringLiteral("100%"), FinnButton::Variant::Secondary, {}, content);
        actual_btn_->setFixedHeight(28);

        fit_btn_ = new FinnButton(
            QStringLiteral("Fit"), FinnButton::Variant::Secondary, {}, content);
        fit_btn_->setFixedHeight(28);

        zoom_in_btn_ = new FinnButton(
            QStringLiteral("Zoom +"), FinnButton::Variant::Secondary, {}, content);
        zoom_in_btn_->setFixedHeight(28);

        rotate_btn_ = new FinnButton(
            QStringLiteral("Rotate"), FinnButton::Variant::Secondary,
            QStringLiteral("refresh"), content);
        rotate_btn_->setFixedHeight(28);

        checker_btn_ = new FinnButton(
            QStringLiteral("Grid"), FinnButton::Variant::Secondary, {}, content);
        checker_btn_->setCheckable(true);
        checker_btn_->setChecked(true);
        checker_btn_->setFixedHeight(28);

        header_row->addWidget(zoom_out_btn_);
        header_row->addWidget(actual_btn_);
        header_row->addWidget(fit_btn_);
        header_row->addWidget(zoom_in_btn_);
        header_row->addWidget(rotate_btn_);
        header_row->addWidget(checker_btn_);
        root->addLayout(header_row);

        stack_ = new QStackedWidget(content);
        finn::ui::install_stack_animator(stack_);

        auto* empty_card = new FinnCard(kRadiusMd, false, stack_);
        auto* empty_lay = new QVBoxLayout(empty_card);
        empty_lay->setContentsMargins(32, 32, 32, 32);
        auto* empty = new FinnEmptyState(
            QStringLiteral("viewer"),
            QStringLiteral("No image opened"),
            QStringLiteral("Choose a preset on the left or open a PNG, JPEG, WebP, or SVG image."),
            QStringLiteral("Open Image"),
            empty_card);
        empty_lay->addStretch(1);
        empty_lay->addWidget(empty, 0, Qt::AlignCenter);
        empty_lay->addStretch(1);

        canvas_ = new ImageCanvas(stack_);

        stack_->addWidget(empty_card);
        stack_->addWidget(canvas_);
        root->addWidget(stack_, 1);

        // Clean metadata footer row
        auto* foot_row = new QHBoxLayout();
        foot_row->setContentsMargins(2, 0, 2, 0);
        foot_row->setSpacing(10);
        info_label_ = new FinnLabel(
            QStringLiteral("Ready"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, content);
        zoom_label_ = new FinnLabel(
            QStringLiteral("100%"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, content);
        foot_row->addWidget(info_label_, 1);
        foot_row->addWidget(zoom_label_);
        root->addLayout(foot_row);

        setContentWidget(content);

        connect(empty, &FinnEmptyState::actionClicked, this, [this]() { openDialog(); });
        connect(zoom_out_btn_, &QAbstractButton::clicked, this, [this]() { canvas_->zoomBy(1.0 / 1.2); });
        connect(zoom_in_btn_, &QAbstractButton::clicked, this, [this]() { canvas_->zoomBy(1.2); });
        connect(fit_btn_, &QAbstractButton::clicked, this, [this]() { canvas_->fitToWindow(); });
        connect(actual_btn_, &QAbstractButton::clicked, this, [this]() { canvas_->setZoom(1.0); });
        connect(rotate_btn_, &QAbstractButton::clicked, this, [this]() { canvas_->rotate90(); });
        connect(checker_btn_, &QAbstractButton::toggled, this, [this](bool on) {
            canvas_->setChecker(on);
        });
        connect(canvas_, &ImageCanvas::zoomChanged, this, [this](double z) {
            zoom_label_->setText(QStringLiteral("%1%").arg(qRound(z * 100.0)));
        });
    }

    void loadPresetFolder(const QString& preset_id) {
        active_preset_ = preset_id;
        for (const auto& p : preset_items_) {
            p.second->setSelected(p.first == preset_id);
        }

        QString dir_path;
        if (preset_id == QLatin1String("wallpapers")) {
            dir_path = resolve_preset_dir({
                QStringLiteral("/usr/share/finn/wallpaper"),
                QStringLiteral("assets/wallpaper"),
            });
        } else if (preset_id == QLatin1String("icons")) {
            dir_path = resolve_preset_dir({
                QStringLiteral("/usr/share/finn/icons"),
                QStringLiteral("assets/icons/apps"),
                QStringLiteral("assets/icons/shell"),
            });
        } else if (preset_id == QLatin1String("pointers")) {
            dir_path = resolve_preset_dir({
                QStringLiteral("/usr/share/finn/pointers"),
                QStringLiteral("assets/pointers"),
            });
        }

        if (!dir_path.isEmpty()) {
            scanSiblings(QDir(dir_path));
            if (!siblings_.isEmpty()) {
                openFile(siblings_.first(), false);
            } else {
                rebuildSidebarImages();
            }
        }
    }

    void openDialog() {
        QString start = QFileInfo(current_path_).exists()
                            ? QFileInfo(current_path_).absolutePath()
                            : QDir::homePath();
        const QString picked = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open Image"), start,
            QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.svg *.gif *.bmp);;All Files (*)"));
        if (!picked.isEmpty()) {
            active_preset_.clear();
            for (const auto& p : preset_items_) p.second->setSelected(false);
            openFile(picked, true);
        }
    }

    void openFile(const QString& path, bool rescan_dir = true) {
        QString fmt;
        QSize px;
        QString err;
        if (!canvas_->loadImage(path, &fmt, &px, &err)) {
            showToast(QStringLiteral("Could not open image: %1").arg(err), FinnToast::Severity::Error);
            return;
        }
        const QFileInfo fi(path);
        current_path_ = fi.absoluteFilePath();
        if (rescan_dir) {
            scanSiblings(fi.dir());
        } else {
            sibling_idx_ = static_cast<int>(siblings_.indexOf(current_path_));
        }
        rebuildSidebarImages();
        stack_->setCurrentIndex(1);

        const QFontMetrics fm(title_label_->font());
        title_label_->setText(fm.elidedText(fi.fileName(), Qt::ElideMiddle, 220));
        title_label_->setToolTip(fi.fileName());

        const QString sz_text = fi.size() >= 1048576
                                    ? QStringLiteral("%1 MB").arg(fi.size() / 1048576.0, 0, 'f', 1)
                                    : QStringLiteral("%1 KB").arg(qMax<qint64>(1, fi.size() / 1024));
        info_label_->setText(QStringLiteral("%1  ·  %2 × %3 px  ·  %4  ·  %5")
                                 .arg(fi.fileName())
                                 .arg(px.width())
                                 .arg(px.height())
                                 .arg(fmt, sz_text));
        setWindowTitleText(QStringLiteral("%1 — Finn Viewer").arg(fi.fileName()));
        updateControlStates();
    }

    void scanSiblings(const QDir& dir) {
        siblings_.clear();
        sibling_idx_ = -1;
        const auto entries = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& fi : entries) {
            if (is_supported_image_ext(fi.suffix())) {
                siblings_.push_back(fi.absoluteFilePath());
            }
        }
        sibling_idx_ = static_cast<int>(siblings_.indexOf(current_path_));
    }

    void rebuildSidebarImages() {
        while (QLayoutItem* item = images_list_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        const QString q =
            sidebar_search_ ? sidebar_search_->text().trimmed() : QString();
        const QColor active_badge(0x00, 0x7B, 0xFF);
        const QColor image_badge(0x0E, 0xA5, 0xE9);

        for (const QString& abs : siblings_) {
            const QFileInfo fi(abs);
            if (!q.isEmpty() && !fi.fileName().contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            const bool is_cur = (abs == current_path_);
            const QString ext = fi.suffix().toLower();
            const QString ico = (ext == QLatin1String("svg") || ext == QLatin1String("svgz"))
                                    ? QStringLiteral("paint-brush")
                                    : QStringLiteral("display");
            auto* item = new FinnNavItem(ico, fi.fileName(), sidebar_);
            item->setBadgeColor(is_cur ? active_badge : image_badge);
            item->setSelected(is_cur);
            item->setToolTip(abs);
            connect(item, &QAbstractButton::clicked, this,
                    [this, abs]() { openFile(abs, false); });
            images_list_layout_->addWidget(item);
        }
        images_list_layout_->addStretch(1);
    }

    void stepSibling(int delta) {
        if (siblings_.size() <= 1 || sibling_idx_ < 0) return;
        const int next = (sibling_idx_ + delta + siblings_.size()) % siblings_.size();
        openFile(siblings_[next], false);
    }

    void updateControlStates() {
        const bool loaded = canvas_->hasImage();
        zoom_out_btn_->setEnabled(loaded);
        fit_btn_->setEnabled(loaded);
        actual_btn_->setEnabled(loaded);
        zoom_in_btn_->setEnabled(loaded);
        rotate_btn_->setEnabled(loaded);
        checker_btn_->setEnabled(loaded);
    }

    QString active_preset_;
    QString current_path_;
    QStringList siblings_;
    int sibling_idx_ = -1;

    QWidget* sidebar_ = nullptr;
    FinnSearchField* sidebar_search_ = nullptr;
    QVector<QPair<QString, FinnNavItem*>> preset_items_;
    FinnLabel* folder_caption_ = nullptr;
    QScrollArea* side_scroll_ = nullptr;
    QVBoxLayout* images_list_layout_ = nullptr;

    FinnLabel* title_label_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
    FinnLabel* info_label_ = nullptr;
    FinnLabel* zoom_label_ = nullptr;
    FinnButton* zoom_out_btn_ = nullptr;
    FinnButton* fit_btn_ = nullptr;
    FinnButton* actual_btn_ = nullptr;
    FinnButton* zoom_in_btn_ = nullptr;
    FinnButton* rotate_btn_ = nullptr;
    FinnButton* checker_btn_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-viewer 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-viewer [image]");
            return 0;
        }
    }
    FinnApplication app(argc, argv, QStringLiteral("viewer"), QStringLiteral("Finn Viewer"));
    const QString initial = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    ViewerWindow win(initial);
    win.show();
    return app.exec();
}

#include "main.moc"
