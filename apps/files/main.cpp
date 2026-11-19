// Finn Files: native FinnOS file manager redesigned to match Figma Settings (20:64) 1:1.
// Uses ChromeMode::FigmaSplit, 250px translucent sidebar with 26x26 circular colored
// icon badges, (12,64) search pill, floating top-right FinnWindowControls, and
// Settings-style r=6 file cards in both Grid and List views.

#include <FinnSDK/FinnSDK.hpp>

#include "compat/flatpak_installer.hpp"
#include "compat/macos_runner.hpp"
#include "compat/ohos_runner.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#include "common/paths.hpp"
#include "compat/wine_runner.hpp"

namespace {

namespace fs = std::filesystem;

void apply_header_16px_regular(finn::ui::FinnLabel* lbl) {
    if (!lbl) return;
    auto sync = [lbl]() {
        QFont f = finn::ui::finn_font(12.0, QFont::Normal);
        f.setPixelSize(16);
        lbl->setFont(f);
    };
    sync();
    QObject::connect(&finn::ui::FinnThemeManager::instance(),
                     &finn::ui::FinnThemeManager::themeChanged,
                     lbl, sync);
}

QString format_bytes(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

bool is_same_or_descendant(const QString& candidate_dst, const QString& src_dir) {
    std::error_code ec;
    const fs::path src_c = fs::weakly_canonical(src_dir.toStdString(), ec);
    if (ec) return false;
    const fs::path dst_c = fs::weakly_canonical(candidate_dst.toStdString(), ec);
    if (ec) return false;
    auto s_it = src_c.begin();
    auto d_it = dst_c.begin();
    for (; s_it != src_c.end(); ++s_it, ++d_it) {
        if (d_it == dst_c.end() || *s_it != *d_it) return false;
    }
    return true;
}

bool copy_recursive(const QFileInfo& src, const QString& dst_path) {
    if (src.isSymLink()) {
        std::error_code ec;
        const fs::path src_p(src.absoluteFilePath().toStdString());
        const fs::path dst_p(dst_path.toStdString());
        fs::remove(dst_p, ec);
        ec.clear();
        fs::copy_symlink(src_p, dst_p, ec);
        return !ec;
    }
    if (src.isDir()) {
        if (is_same_or_descendant(dst_path, src.absoluteFilePath())) {
            return false;
        }
        if (!QDir().mkpath(dst_path)) return false;
        const QDir src_dir(src.absoluteFilePath());
        for (const QFileInfo& entry :
             src_dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (!copy_recursive(entry, dst_path + QLatin1Char('/') + entry.fileName())) {
                return false;
            }
        }
        return true;
    }
    QFile::remove(dst_path);
    return QFile::copy(src.absoluteFilePath(), dst_path);
}

struct FileEntryData {
    QString path;
    QString name;
    QString kind;
    QString size_text;
    QString modified_text;
    QString icon_name;
    bool is_dir = false;
};

// Clean Settings-style r=6 (kRadiusMd) file card with 36x36 crisp icon,
// clean file name, and muted metadata (12px 50% opacity).
class FileEntryCard final : public QWidget {
public:
    FileEntryCard(FileEntryData data, bool grid_mode, QWidget* parent = nullptr)
        : QWidget(parent), data_(std::move(data)), grid_mode_(grid_mode) {
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(data_.name);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(grid_mode_ ? 64 : 52);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(data_.path);
        connect(&finn::ui::FinnThemeManager::instance(),
                &finn::ui::FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    const FileEntryData& data() const { return data_; }

    void setSelected(bool sel) {
        if (selected_ == sel) return;
        selected_ = sel;
        update();
    }

    bool isSelected() const { return selected_; }

    void setCallbacks(std::function<void()> on_select,
                      std::function<void()> on_activate) {
        on_select_ = std::move(on_select);
        on_activate_ = std::move(on_activate);
    }

protected:
    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (on_select_) on_select_();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (on_select_) on_select_();
            if (on_activate_) on_activate_();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (on_select_) on_select_();
            if (on_activate_) on_activate_();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space) {
            if (on_select_) on_select_();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        const auto& tm = finn::ui::FinnThemeManager::instance();
        const auto& t = tm.tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = t.tile;
        QColor border = t.hair;
        if (selected_) {
            bg = t.tile_on;
            border = t.accent;
        } else if (hovered_) {
            bg = t.surface_hover;
        }

        p.setBrush(bg);
        p.setPen(QPen(hasFocus() ? t.accent : border, (selected_ || hasFocus()) ? 1.6 : 1.0));
        p.drawRoundedRect(r, finn::ui::kRadiusMd, finn::ui::kRadiusMd);

        // 36x36 crisp icon container on the left
        const QRectF icon_box(12.0, (height() - 36.0) / 2.0, 36.0, 36.0);
        const QColor badge_tint = t.mute;
        QColor badge_bg = badge_tint;
        badge_bg.setAlpha(t.dark ? 42 : 26);
        p.setBrush(badge_bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(icon_box, 6.0, 6.0);

        const QRectF glyph_rect(icon_box.x() + 8.0, icon_box.y() + 8.0, 20.0, 20.0);
        tm.paintIcon(&p, glyph_rect, data_.icon_name, badge_tint);

        const QColor muted_half = t.mute;

        QFont name_font = finn::ui::finn_font(10.5, QFont::Normal);
        name_font.setPixelSize(13);
        const QFontMetrics name_fm(name_font);

        QFont meta_font = finn::ui::finn_font(9.5, QFont::Normal);
        meta_font.setPixelSize(12);
        const QFontMetrics meta_fm(meta_font);

        const int text_x = 56;
        if (grid_mode_) {
            const int avail_w = std::max(20, width() - text_x - 12);
            p.setFont(name_font);
            p.setPen(t.ink);
            p.drawText(QRect(text_x, 12, avail_w, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       name_fm.elidedText(data_.name, Qt::ElideMiddle, avail_w));

            const QString meta = data_.is_dir
                                     ? data_.kind
                                     : QStringLiteral("%1 · %2").arg(data_.size_text, data_.kind);
            p.setFont(meta_font);
            p.setPen(muted_half);
            p.drawText(QRect(text_x, 32, avail_w, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       meta_fm.elidedText(meta, Qt::ElideRight, avail_w));
        } else {
            const int right_meta_w = 192;
            const int avail_name_w = std::max(40, width() - text_x - right_meta_w - 16);
            p.setFont(name_font);
            p.setPen(t.ink);
            p.drawText(QRect(text_x, 8, avail_name_w, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       name_fm.elidedText(data_.name, Qt::ElideMiddle, avail_name_w));

            p.setFont(meta_font);
            p.setPen(muted_half);
            p.drawText(QRect(text_x, 26, avail_name_w, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       meta_fm.elidedText(data_.kind, Qt::ElideRight, avail_name_w));

            const QString right_info =
                data_.is_dir
                    ? data_.modified_text
                    : QStringLiteral("%1  ·  %2").arg(data_.size_text, data_.modified_text);
            p.drawText(QRect(width() - right_meta_w - 14, 0, right_meta_w, height()),
                       Qt::AlignRight | Qt::AlignVCenter,
                       meta_fm.elidedText(right_info, Qt::ElideLeft, right_meta_w));
        }
    }

private:
    FileEntryData data_;
    bool grid_mode_ = true;
    bool selected_ = false;
    bool hovered_ = false;
    std::function<void()> on_select_;
    std::function<void()> on_activate_;
};

class FilesWindow final : public finn::ui::FinnWindow {
public:
    explicit FilesWindow(const QString& start_dir = {})
        : FinnWindow(QStringLiteral("Finn Files"), QStringLiteral("files")) {
        setMinimumSize(800, 500);
        resize(920, 580);
        setChromeMode(ChromeMode::FigmaSplit);

        setupSidebar();
        setupContent();
        applyTheme();

        connect(&finn::ui::FinnThemeManager::instance(),
                &finn::ui::FinnThemeManager::themeChanged,
                this, [this](const finn::ui::FinnTokens&) { applyTheme(); });

        const QString initial =
            (!start_dir.isEmpty() && QFileInfo(start_dir).isDir())
                ? QDir(start_dir).absolutePath()
                : QDir::homePath();
        navigateTo(initial, true);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!selected_path_.isEmpty()) {
                openEntry(selected_path_);
                event->accept();
                return;
            }
        } else if (event->key() == Qt::Key_Backspace) {
            goUp();
            event->accept();
            return;
        }
        FinnWindow::keyPressEvent(event);
    }

private:
    void setupSidebar() {
        sidebar_ = new finn::ui::FinnSidebar(250, this);
        sidebar_->setHeaderTitle(QStringLiteral("Files"));
        apply_header_16px_regular(sidebar_->headerLabel());
        sidebar_->setSearchEnabled(true, QStringLiteral("Search"));

        // Disconnect FinnSidebar's internal item filter so typing in "Search files"
        // filters the current directory view instead of hiding sidebar places.
        if (sidebar_->searchField()) {
            sidebar_->searchField()->disconnect(sidebar_);
            connect(sidebar_->searchField(), &finn::ui::FinnSearchField::textChanged,
                    this, [this](const QString&) { refreshListing(); });
        }

        // Places are navigation, not categories: one accent for Home, one
        // neutral for the rest. The icon glyph carries the meaning.
        auto add_place = [&](const QString& id, const QString& icon,
                             const QString& label, const QString& path,
                             bool accent = false) {
            places_.push_back({id, path});
            sidebar_->addItem(id, icon, label,
                              accent ? finn::ui::nav_accent_light()
                                     : finn::ui::nav_neutral_light(),
                              accent ? finn::ui::nav_accent_dark()
                                     : finn::ui::nav_neutral_dark());
        };

        add_place(QStringLiteral("home"), QStringLiteral("home-09"),
                  QStringLiteral("Home"), QDir::homePath(), true);
        add_place(QStringLiteral("desktop"), QStringLiteral("display"),
                  QStringLiteral("Desktop"),
                  QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
        add_place(QStringLiteral("docs"), QStringLiteral("file-text"),
                  QStringLiteral("Documents"),
                  QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        add_place(QStringLiteral("downloads"), QStringLiteral("download"),
                  QStringLiteral("Downloads"),
                  QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        add_place(QStringLiteral("pictures"), QStringLiteral("image"),
                  QStringLiteral("Pictures"),
                  QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        add_place(QStringLiteral("root"), QStringLiteral("hard-drive"),
                  QStringLiteral("Root"), QStringLiteral("/"));

        const QString wine_c =
            QDir::homePath() + QStringLiteral("/") +
            QString::fromUtf8(finn::kWinePrefixRel) + QStringLiteral("/drive_c");
        if (QFileInfo::exists(wine_c)) {
            sidebar_->addSectionSpacer(12);
            add_place(QStringLiteral("wine_c"), QStringLiteral("view-open-windows"),
                      QStringLiteral("Windows C:"), wine_c);
        }

        sidebar_->addStretch();
        setSidebarWidget(sidebar_);

        connect(sidebar_, &finn::ui::FinnSidebar::selected, this,
                [this](const QString& id) {
                    for (const auto& p : places_) {
                        if (p.first == id) {
                            QDir().mkpath(p.second);
                            navigateTo(p.second, true);
                            return;
                        }
                    }
                });
    }

    void setupContent() {
        auto* page = new QWidget(this);
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 8, 12, 10);
        root->setSpacing(8);

        // Top header row aligned with FinnWindowControls (y=8, height=28)
        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(4);

        back_btn_ = new finn::ui::FinnIconButton(QStringLiteral("chevron-left"), 28, 15, page);
        back_btn_->setToolTip(QStringLiteral("Back"));
        back_btn_->setAccessibleName(QStringLiteral("Back"));
        up_btn_ = new finn::ui::FinnIconButton(QStringLiteral("chevron-right"), 28, 15, page);
        up_btn_->setToolTip(QStringLiteral("Parent Folder"));
        up_btn_->setAccessibleName(QStringLiteral("Parent Folder"));

        folder_title_ = new finn::ui::FinnLabel(
            QStringLiteral("Home"), finn::ui::FinnLabel::Role::Title,
            finn::ui::FinnLabel::Tone::Ink, page);
        apply_header_16px_regular(folder_title_);

        breadcrumbs_ = new finn::ui::FinnBreadcrumbs(page);

        auto* new_folder_btn = new finn::ui::FinnButton(
            QString(), finn::ui::FinnButton::Variant::Ghost,
            QStringLiteral("folder-plus"), page);
        new_folder_btn->setToolTip(QStringLiteral("New Folder"));
        new_folder_btn->setAccessibleName(QStringLiteral("New Folder"));
        new_folder_btn->setFixedSize(28, 28);

        auto* term_btn = new finn::ui::FinnButton(
            QString(), finn::ui::FinnButton::Variant::Ghost,
            QStringLiteral("terminal-square"), page);
        term_btn->setToolTip(QStringLiteral("Open Terminal Here"));
        term_btn->setAccessibleName(QStringLiteral("Open Terminal here"));
        term_btn->setFixedSize(28, 28);

        view_toggle_btn_ = new finn::ui::FinnButton(
            QString(), finn::ui::FinnButton::Variant::Ghost,
            QStringLiteral("list"), page);
        view_toggle_btn_->setAccessibleName(QStringLiteral("Switch between Grid and List view"));
        view_toggle_btn_->setFixedSize(28, 28);
        view_toggle_btn_->setToolTip(QStringLiteral("Switch between Grid and List view"));

        header_row->addWidget(back_btn_);
        header_row->addWidget(up_btn_);
        header_row->addSpacing(4);
        header_row->addWidget(folder_title_);
        header_row->addSpacing(6);
        header_row->addWidget(breadcrumbs_, 1);
        header_row->addWidget(new_folder_btn);
        header_row->addWidget(term_btn);
        header_row->addWidget(view_toggle_btn_);
        root->addLayout(header_row);

        // Contextual selection & clipboard pill bar (only visible when an item is selected or clipboard has a file)
        action_bar_ = new finn::ui::FinnCard(finn::ui::kRadiusMd, false, page);
        action_bar_->setFixedHeight(42);
        auto* act_lay = new QHBoxLayout(action_bar_);
        act_lay->setContentsMargins(10, 5, 10, 5);
        act_lay->setSpacing(6);

        selection_lbl_ = new finn::ui::FinnLabel(
            {}, finn::ui::FinnLabel::Role::Caption,
            finn::ui::FinnLabel::Tone::Dim, action_bar_);

        open_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Open"), finn::ui::FinnButton::Variant::Primary,
            {}, action_bar_);
        open_btn_->setAccessibleName(QStringLiteral("Open selected item"));
        open_btn_->setFixedHeight(28);

        wine_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Run in Wine"), finn::ui::FinnButton::Variant::Primary,
            QStringLiteral("view-open-windows"), action_bar_);
        wine_btn_->setAccessibleName(QStringLiteral("Run in Wine"));
        wine_btn_->setFixedHeight(28);
        wine_btn_->setVisible(false);

        rename_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Rename"), finn::ui::FinnButton::Variant::Secondary,
            QStringLiteral("edit-01"), action_bar_);
        rename_btn_->setAccessibleName(QStringLiteral("Rename selected item"));
        rename_btn_->setFixedHeight(28);

        copy_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Copy"), finn::ui::FinnButton::Variant::Secondary,
            {}, action_bar_);
        copy_btn_->setAccessibleName(QStringLiteral("Copy selected item"));
        copy_btn_->setFixedHeight(28);

        cut_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Cut"), finn::ui::FinnButton::Variant::Secondary,
            {}, action_bar_);
        cut_btn_->setAccessibleName(QStringLiteral("Cut selected item"));
        cut_btn_->setFixedHeight(28);

        paste_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Paste"), finn::ui::FinnButton::Variant::Secondary,
            {}, action_bar_);
        paste_btn_->setAccessibleName(QStringLiteral("Paste item"));
        paste_btn_->setFixedHeight(28);

        delete_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Delete"), finn::ui::FinnButton::Variant::Danger,
            {}, action_bar_);
        delete_btn_->setAccessibleName(QStringLiteral("Delete selected item"));
        delete_btn_->setFixedHeight(28);

        act_lay->addWidget(selection_lbl_, 1);
        act_lay->addWidget(open_btn_);
        act_lay->addWidget(wine_btn_);
        act_lay->addWidget(rename_btn_);
        act_lay->addWidget(copy_btn_);
        act_lay->addWidget(cut_btn_);
        act_lay->addWidget(paste_btn_);
        act_lay->addWidget(delete_btn_);
        action_bar_->setVisible(false);
        root->addWidget(action_bar_);

        // Scrollable file cards area
        scroll_ = new QScrollArea(page);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setAutoFillBackground(false);
        scroll_->viewport()->setAutoFillBackground(false);

        cards_host_ = new QWidget(scroll_);
        cards_host_->setAutoFillBackground(false);
        cards_vbox_ = new QVBoxLayout(cards_host_);
        cards_vbox_->setContentsMargins(0, 2, 2, 6);
        cards_vbox_->setSpacing(8);

        cards_grid_ = new QGridLayout();
        cards_grid_->setContentsMargins(0, 0, 0, 0);
        cards_grid_->setHorizontalSpacing(8);
        cards_grid_->setVerticalSpacing(8);
        cards_vbox_->addLayout(cards_grid_);
        cards_vbox_->addStretch(1);

        scroll_->setWidget(cards_host_);
        root->addWidget(scroll_, 1);

        // Clean footer status row
        auto* status_row = new QHBoxLayout();
        status_row->setContentsMargins(2, 0, 2, 0);
        status_row->setSpacing(10);
        status_label_ = new finn::ui::FinnLabel(
            {}, finn::ui::FinnLabel::Role::Caption,
            finn::ui::FinnLabel::Tone::Mute, page);
        detail_label_ = new finn::ui::FinnLabel(
            {}, finn::ui::FinnLabel::Role::Caption,
            finn::ui::FinnLabel::Tone::Mute, page);
        hidden_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Show Hidden"), finn::ui::FinnButton::Variant::Ghost,
            {}, page);
        hidden_btn_->setCheckable(true);
        hidden_btn_->setFixedHeight(24);

        status_row->addWidget(status_label_);
        status_row->addStretch(1);
        status_row->addWidget(detail_label_);
        status_row->addWidget(hidden_btn_);
        root->addLayout(status_row);

        setContentWidget(page);

        connect(back_btn_, &QAbstractButton::clicked, this, &FilesWindow::goBack);
        connect(up_btn_, &QAbstractButton::clicked, this, &FilesWindow::goUp);
        connect(breadcrumbs_, &finn::ui::FinnBreadcrumbs::segmentActivated, this,
                [this](const QString& path) { navigateTo(path, true); });
        connect(new_folder_btn, &QAbstractButton::clicked, this, &FilesWindow::createNewFolder);
        connect(term_btn, &QAbstractButton::clicked, this, [this]() {
            if (!QProcess::startDetached(QStringLiteral("finn-terminal"),
                                         {QStringLiteral("--cwd"), current_dir_})) {
                showToast(QStringLiteral("finn-terminal not installed in PATH"),
                          finn::ui::FinnToast::Severity::Warning);
            }
        });
        connect(view_toggle_btn_, &QAbstractButton::clicked, this, [this]() {
            grid_mode_ = !grid_mode_;
            view_toggle_btn_->setIconName(grid_mode_ ? QStringLiteral("list")
                                                     : QStringLiteral("grid"));
            view_toggle_btn_->setToolTip(grid_mode_ ? QStringLiteral("Switch to List View")
                                                    : QStringLiteral("Switch to Grid View"));
            refreshListing();
        });
        connect(open_btn_, &QAbstractButton::clicked, this, [this]() {
            if (!selected_path_.isEmpty()) openEntry(selected_path_);
        });
        connect(wine_btn_, &QAbstractButton::clicked, this, [this]() { launchSelectedWine(); });
        connect(rename_btn_, &QAbstractButton::clicked, this, &FilesWindow::renameSelected);
        connect(copy_btn_, &QAbstractButton::clicked, this, [this]() { stageClipboard(false); });
        connect(cut_btn_, &QAbstractButton::clicked, this, [this]() { stageClipboard(true); });
        connect(paste_btn_, &QAbstractButton::clicked, this, &FilesWindow::pasteClipboard);
        connect(delete_btn_, &QAbstractButton::clicked, this, &FilesWindow::deleteSelected);
        connect(hidden_btn_, &QAbstractButton::toggled, this, [this](bool on) {
            show_hidden_ = on;
            refreshListing();
        });

        new QShortcut(QKeySequence(QStringLiteral("Ctrl+H")), this,
                      [this]() { hidden_btn_->toggle(); });
        new QShortcut(QKeySequence::Delete, this,
                      [this]() { deleteSelected(); });
        new QShortcut(QKeySequence(QStringLiteral("F2")), this,
                      [this]() { renameSelected(); });
    }

    void navigateTo(const QString& path, bool record_history) {
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isDir()) {
            showToast(QStringLiteral("Folder unavailable: %1").arg(path),
                      finn::ui::FinnToast::Severity::Warning);
            return;
        }
        const QString clean = QDir(fi.absoluteFilePath()).canonicalPath();
        if (record_history && !current_dir_.isEmpty() && current_dir_ != clean) {
            history_.push_back(current_dir_);
        }
        current_dir_ = clean.isEmpty() ? fi.absoluteFilePath() : clean;
        back_btn_->setEnabled(!history_.isEmpty());
        up_btn_->setEnabled(current_dir_ != QStringLiteral("/"));

        const QString dir_name = QFileInfo(current_dir_).fileName();
        folder_title_->setText(dir_name.isEmpty() ? QStringLiteral("Root") : dir_name);

        bool matched_place = false;
        for (const auto& p : places_) {
            if (QDir(p.second).canonicalPath() == current_dir_) {
                sidebar_->setCurrentId(p.first);
                matched_place = true;
                break;
            }
        }
        if (!matched_place) {
            sidebar_->setCurrentId(QString());
        }

        // Update clickable breadcrumbs (collapse deep paths cleanly so header fits 800px)
        QVector<QPair<QString, QString>> crumbs;
        crumbs.push_back({QStringLiteral("/"), QStringLiteral("/")});
        const QStringList parts =
            current_dir_.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QStringList full_paths;
        full_paths.reserve(parts.size());
        QString accum;
        for (const QString& part : parts) {
            accum += QLatin1Char('/') + part;
            full_paths.push_back(accum);
        }
        if (parts.size() > 2) {
            const qsizetype tail_start = parts.size() - 2;
            crumbs.push_back({QStringLiteral("..."), full_paths[tail_start - 1]});
            for (qsizetype i = tail_start; i < parts.size(); ++i) {
                crumbs.push_back({parts[i], full_paths[i]});
            }
        } else {
            for (qsizetype i = 0; i < parts.size(); ++i) {
                crumbs.push_back({parts[i], full_paths[i]});
            }
        }
        breadcrumbs_->setSegments(crumbs);
        selected_path_.clear();
        refreshListing();
    }

    void goBack() {
        if (history_.isEmpty()) return;
        const QString prev = history_.takeLast();
        navigateTo(prev, false);
    }

    void goUp() {
        QDir d(current_dir_);
        if (d.cdUp()) navigateTo(d.absolutePath(), true);
    }

    void clearCardsGrid() {
        cards_.clear();
        while (QLayoutItem* item = cards_grid_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
    }

    void refreshListing() {
        clearCardsGrid();

        QDir dir(current_dir_);
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (show_hidden_) filters |= QDir::Hidden;

        const QFileInfoList entries =
            dir.entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        const QString query =
            (sidebar_ && sidebar_->searchField())
                ? sidebar_->searchField()->text().trimmed()
                : QString();

        const int cols = grid_mode_ ? 2 : 1;
        cards_grid_->setColumnStretch(0, 1);
        cards_grid_->setColumnStretch(1, grid_mode_ ? 1 : 0);

        int dirs = 0;
        int files = 0;
        qint64 total_bytes = 0;
        int row = 0;
        int col = 0;
        bool kept_selection = false;

        for (const QFileInfo& fi : entries) {
            if (!query.isEmpty() &&
                !fi.fileName().contains(query, Qt::CaseInsensitive)) {
                continue;
            }

            FileEntryData d;
            d.path = fi.absoluteFilePath();
            d.name = fi.fileName();
            d.is_dir = fi.isDir();
            d.size_text = fi.isDir() ? QStringLiteral("--") : format_bytes(fi.size());
            d.modified_text = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

            const QString ext = fi.suffix().toLower();
            if (fi.isDir()) {
                ++dirs;
                if (ext == QStringLiteral("app") &&
                    QFileInfo::exists(fi.absoluteFilePath() + QStringLiteral("/Contents/Info.plist"))) {
                    d.kind = QStringLiteral("macOS App");
                    d.icon_name = QStringLiteral("display");
                } else {
                    d.kind = QString();
                    d.icon_name = QStringLiteral("folder");
                }
            } else {
                ++files;
                total_bytes += fi.size();
                if (ext == QStringLiteral("exe") || ext == QStringLiteral("msi")) {
                    d.kind = QStringLiteral("Windows App");
                    d.icon_name = QStringLiteral("window");
                } else if (ext == QStringLiteral("flatpakref") ||
                           ext == QStringLiteral("flatpak") ||
                           ext == QStringLiteral("flatpakrepo")) {
                    d.kind = QString::fromStdString(finn::compat::flatpak_kind_label(
                        finn::compat::detect_flatpak_file_kind(fi.fileName().toStdString())));
                    d.icon_name = QStringLiteral("package");
                } else if (ext == QStringLiteral("dmg")) {
                    d.kind = QStringLiteral("macOS Disk Image");
                    d.icon_name = QStringLiteral("display");
                } else if (ext == QStringLiteral("pkg") || ext == QStringLiteral("mpkg")) {
                    d.kind = QStringLiteral("macOS Installer");
                    d.icon_name = QStringLiteral("display");
                } else if (ext == QStringLiteral("hap")) {
                    d.kind = QStringLiteral("OpenHarmony App");
                    d.icon_name = QStringLiteral("package");
                } else if (ext == QStringLiteral("hsp")) {
                    d.kind = QStringLiteral("OpenHarmony Shared");
                    d.icon_name = QStringLiteral("package");
                } else if (ext == QStringLiteral("app") &&
                           finn::compat::is_ohos_package_path(fi.absoluteFilePath().toStdString())) {
                    d.kind = QStringLiteral("OpenHarmony Pack");
                    d.icon_name = QStringLiteral("package");
                } else if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") ||
                           ext == QStringLiteral("jpeg") || ext == QStringLiteral("webp") ||
                           ext == QStringLiteral("svg")) {
                    d.kind = QStringLiteral("Image");
                    d.icon_name = QStringLiteral("image");
                } else if (fi.isExecutable()) {
                    d.kind = QStringLiteral("Executable");
                    d.icon_name = QStringLiteral("terminal");
                } else if (ext == QStringLiteral("md") || ext == QStringLiteral("txt") ||
                           ext == QStringLiteral("json") || ext == QStringLiteral("conf")) {
                    d.kind = QStringLiteral("Document");
                    d.icon_name = QStringLiteral("edit-01");
                } else {
                    d.kind = ext.isEmpty() ? QStringLiteral("File")
                                           : QStringLiteral("%1 File").arg(ext.toUpper());
                    d.icon_name = QStringLiteral("edit-01");
                }
            }

            auto* card = new FileEntryCard(d, grid_mode_, cards_host_);
            const QString entry_path = d.path;
            card->setCallbacks(
                [this, entry_path]() { selectEntry(entry_path); },
                [this, entry_path]() { openEntry(entry_path); });

            if (!selected_path_.isEmpty() && selected_path_ == entry_path) {
                card->setSelected(true);
                kept_selection = true;
            }

            cards_grid_->addWidget(card, row, col);
            cards_.push_back(card);
            if (++col >= cols) {
                col = 0;
                ++row;
            }
        }

        if (cards_.empty()) {
            auto* empty = new finn::ui::FinnEmptyState(
                QStringLiteral("folder"),
                query.isEmpty() ? QStringLiteral("Folder is empty")
                                : QStringLiteral("No matching files"),
                query.isEmpty()
                    ? QStringLiteral("Create a new folder or paste items here.")
                    : QStringLiteral("Try a different search query in the sidebar."),
                {}, cards_host_);
            cards_grid_->addWidget(empty, 0, 0, 1, cols);
        }

        if (!kept_selection) {
            selected_path_.clear();
        }
        status_label_->setText(
            QStringLiteral("%1 folders, %2 files (%3)")
                .arg(dirs)
                .arg(files)
                .arg(format_bytes(total_bytes)));
        updateSelectionState();
    }

    void selectEntry(const QString& path) {
        selected_path_ = path;
        for (FileEntryCard* c : cards_) {
            c->setSelected(c->data().path == path);
        }
        updateSelectionState();
    }

    void updateSelectionState() {
        const bool has_sel = !selected_path_.isEmpty() && QFileInfo::exists(selected_path_);
        const bool has_clip = !clip_path_.isEmpty();

        action_bar_->setVisible(has_sel || has_clip);
        open_btn_->setVisible(has_sel);
        rename_btn_->setVisible(has_sel);
        copy_btn_->setVisible(has_sel);
        cut_btn_->setVisible(has_sel);
        delete_btn_->setVisible(has_sel);
        paste_btn_->setVisible(has_clip);
        paste_btn_->setEnabled(has_clip);

        if (!has_sel) {
            wine_btn_->setVisible(false);
            if (has_clip) {
                selection_lbl_->setText(
                    QStringLiteral("Clipboard (%1): %2")
                        .arg(clip_cut_ ? QStringLiteral("Cut") : QStringLiteral("Copy"),
                             QFileInfo(clip_path_).fileName()));
            } else {
                selection_lbl_->setText({});
            }
            detail_label_->setText(current_dir_);
            return;
        }

        const QFileInfo fi(selected_path_);
        selection_lbl_->setText(fi.fileName());
        const QString ext = fi.suffix().toLower();
        const bool is_pe =
            fi.isFile() &&
            (ext == QStringLiteral("exe") || ext == QStringLiteral("msi"));
        wine_btn_->setVisible(is_pe);
        if (is_pe) {
            const auto rt = finn::compat::probe_wine_runtime();
            wine_btn_->setEnabled(rt.ready);
            wine_btn_->setToolTip(QString::fromStdString(rt.summary));
            const auto pe = finn::compat::inspect_pe_file(
                fi.absoluteFilePath().toStdString());
            if (pe.valid) {
                detail_label_->setText(
                    QStringLiteral("PE: %1 (%2)")
                        .arg(QString::fromStdString(finn::compat::pe_arch_name(pe.arch)),
                             pe.is_gui ? QStringLiteral("GUI") : QStringLiteral("Console")));
                return;
            }
        }
        if (fi.isFile() &&
            finn::compat::detect_flatpak_file_kind(fi.fileName().toStdString()) !=
                finn::compat::FlatpakFileKind::Unknown) {
            const auto fp = finn::compat::inspect_flatpak_file(
                fi.absoluteFilePath().toStdString());
            if (fp.valid) {
                detail_label_->setText(
                    QStringLiteral("%1: %2")
                        .arg(QString::fromStdString(finn::compat::flatpak_kind_label(fp.kind)),
                             QString::fromStdString(fp.title.empty() ? fp.name : fp.title)));
                return;
            }
        }
        if (fi.isDir() && ext == QStringLiteral("app")) {
            const auto app = finn::compat::inspect_macos_app_bundle(
                fi.absoluteFilePath().toStdString());
            if (app.valid) {
                detail_label_->setText(
                    QStringLiteral("macOS App: %1 (%2)")
                        .arg(QString::fromStdString(app.name),
                             QString::fromStdString(finn::compat::macho_arches_summary(app.binary))));
                return;
            }
        }
        if (fi.isFile() && (ext == QStringLiteral("dmg") || ext == QStringLiteral("pkg") ||
                            ext == QStringLiteral("mpkg"))) {
            const auto inst = finn::compat::inspect_macos_installer(
                fi.absoluteFilePath().toStdString());
            detail_label_->setText(
                inst.valid ? QString::fromStdString(inst.format_detail)
                           : QString::fromStdString(inst.error));
            return;
        }
        detail_label_->setText(fi.isDir() ? QStringLiteral("Directory")
                                          : format_bytes(fi.size()));
    }

    void applyTheme() {
        const auto& t = finn::ui::FinnThemeManager::instance().tokens();
        if (scroll_) {
            scroll_->setStyleSheet(
                QStringLiteral("QScrollArea { background: transparent; border: none; }%1")
                    .arg(finn::ui::scrollbar_qss(t)));
        }
    }

    QFileInfo selectedInfo() const {
        if (selected_path_.isEmpty()) return {};
        return QFileInfo(selected_path_);
    }

    void openEntry(const QString& path) {
        const QFileInfo fi(path);
        const QString ext = fi.suffix().toLower();
        if (fi.isDir()) {
            if (ext == QStringLiteral("app") &&
                QFileInfo::exists(fi.absoluteFilePath() + QStringLiteral("/Contents/Info.plist"))) {
                if (QProcess::startDetached(QStringLiteral("finn-store"),
                                            {QStringLiteral("--install"), fi.absoluteFilePath()})) {
                    return;
                }
            }
            navigateTo(fi.absoluteFilePath(), true);
            return;
        }
        if (ext == QStringLiteral("exe") || ext == QStringLiteral("msi")) {
            launchSelectedWine(path);
            return;
        }
        if (ext == QStringLiteral("flatpakref") || ext == QStringLiteral("flatpak") ||
            ext == QStringLiteral("flatpakrepo") || ext == QStringLiteral("dmg") ||
            ext == QStringLiteral("pkg") || ext == QStringLiteral("mpkg") ||
            ext == QStringLiteral("hap") || ext == QStringLiteral("hsp") ||
            (ext == QStringLiteral("app") &&
             finn::compat::is_ohos_package_path(fi.absoluteFilePath().toStdString()))) {
            if (QProcess::startDetached(QStringLiteral("finn-store"),
                                        {QStringLiteral("--install"), fi.absoluteFilePath()})) {
                return;
            }
        }
        if (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") ||
            ext == QStringLiteral("jpeg") || ext == QStringLiteral("webp") ||
            ext == QStringLiteral("svg")) {
            if (QProcess::startDetached(QStringLiteral("finn-viewer"),
                                        {fi.absoluteFilePath()})) {
                return;
            }
        }
        if (ext == QStringLiteral("txt") || ext == QStringLiteral("md") ||
            ext == QStringLiteral("cpp") || ext == QStringLiteral("hpp") ||
            ext == QStringLiteral("py") || ext == QStringLiteral("sh") ||
            ext == QStringLiteral("json") || ext == QStringLiteral("conf")) {
            if (QProcess::startDetached(QStringLiteral("finn-notes"),
                                        {fi.absoluteFilePath()})) {
                return;
            }
        }

        finn::SessionClient client;
        std::string err;
        if (!client.connect(&err) ||
            !client.open_target(fi.absoluteFilePath().toStdString(), &err)) {
            showToast(QString::fromStdString(err),
                      finn::ui::FinnToast::Severity::Warning);
        }
    }

    void launchSelectedWine(const QString& explicit_path = {}) {
        const QString target =
            explicit_path.isEmpty() ? selectedInfo().absoluteFilePath() : explicit_path;
        if (target.isEmpty()) return;
        const auto rt = finn::compat::probe_wine_runtime();
        if (!rt.ready) {
            showToast(QString::fromStdString(rt.summary),
                      finn::ui::FinnToast::Severity::Warning, 4500);
            return;
        }
        finn::SessionClient client;
        std::string err;
        if (!client.connect(&err) ||
            !client.run_windows(target.toStdString(), {}, &err)) {
            showToast(QString::fromStdString(err),
                      finn::ui::FinnToast::Severity::Error, 4500);
        } else {
            showToast(QStringLiteral("Launching Windows app via Wine"),
                      finn::ui::FinnToast::Severity::Success);
        }
    }

    void createNewFolder() {
        const QString name = finn::ui::FinnDialog::prompt(
            this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"),
            QStringLiteral("New Folder"));
        if (name.trimmed().isEmpty()) return;
        if (QDir(current_dir_).mkdir(name.trimmed())) {
            selected_path_ = QDir(current_dir_).absoluteFilePath(name.trimmed());
            refreshListing();
        } else {
            showToast(QStringLiteral("Could not create folder"),
                      finn::ui::FinnToast::Severity::Error);
        }
    }

    void renameSelected() {
        const QFileInfo fi = selectedInfo();
        if (!fi.exists()) return;
        const QString next = finn::ui::FinnDialog::prompt(
            this, QStringLiteral("Rename Item"), QStringLiteral("New name:"),
            fi.fileName());
        if (next.trimmed().isEmpty() || next.trimmed() == fi.fileName()) return;
        const QString dst = fi.dir().absoluteFilePath(next.trimmed());
        if (QFile::rename(fi.absoluteFilePath(), dst)) {
            selected_path_ = dst;
            refreshListing();
        } else {
            showToast(QStringLiteral("Rename failed"),
                      finn::ui::FinnToast::Severity::Error);
        }
    }

    void stageClipboard(bool cut) {
        const QFileInfo fi = selectedInfo();
        if (!fi.exists()) return;
        clip_path_ = fi.absoluteFilePath();
        clip_cut_ = cut;
        updateSelectionState();
        showToast(QStringLiteral("%1: %2").arg(
            cut ? QStringLiteral("Cut") : QStringLiteral("Copied"), fi.fileName()));
    }

    void pasteClipboard() {
        if (clip_path_.isEmpty()) return;
        const QFileInfo src(clip_path_);
        if (!src.exists()) {
            showToast(QStringLiteral("Source item no longer exists"),
                      finn::ui::FinnToast::Severity::Warning);
            return;
        }
        QString dst = QDir(current_dir_).absoluteFilePath(src.fileName());
        if (src.isDir() && !src.isSymLink() &&
            is_same_or_descendant(current_dir_, src.absoluteFilePath()) &&
            dst != src.absoluteFilePath()) {
            showToast(QStringLiteral("Cannot paste a folder into itself"),
                      finn::ui::FinnToast::Severity::Warning);
            return;
        }
        if (dst == src.absoluteFilePath()) {
            dst = QDir(current_dir_)
                      .absoluteFilePath(src.fileName() + QStringLiteral(" copy"));
        }
        bool ok = false;
        if (clip_cut_) {
            ok = QFile::rename(src.absoluteFilePath(), dst);
            if (!ok && copy_recursive(src, dst)) {
                std::error_code ec;
                fs::remove_all(src.absoluteFilePath().toStdString(), ec);
                ok = !ec;
            }
            if (ok) {
                clip_path_.clear();
            }
        } else {
            ok = copy_recursive(src, dst);
        }
        if (ok) {
            selected_path_ = dst;
            refreshListing();
            showToast(QStringLiteral("Paste complete"),
                      finn::ui::FinnToast::Severity::Success);
        } else {
            showToast(QStringLiteral("Paste failed"),
                      finn::ui::FinnToast::Severity::Error);
        }
    }

    void deleteSelected() {
        const QFileInfo fi = selectedInfo();
        if (!fi.exists()) return;
        if (!finn::ui::FinnDialog::confirm(
                this, QStringLiteral("Delete Item"),
                QStringLiteral("Permanently remove '%1'?").arg(fi.fileName()),
                QStringLiteral("Delete"), true)) {
            return;
        }
        std::error_code ec;
        fs::remove_all(fi.absoluteFilePath().toStdString(), ec);
        if (!ec) {
            selected_path_.clear();
            refreshListing();
            showToast(QStringLiteral("Removed %1").arg(fi.fileName()));
        } else {
            showToast(QString::fromStdString(ec.message()),
                      finn::ui::FinnToast::Severity::Error);
        }
    }

    finn::ui::FinnSidebar* sidebar_ = nullptr;
    QVector<QPair<QString, QString>> places_;
    finn::ui::FinnIconButton* back_btn_ = nullptr;
    finn::ui::FinnIconButton* up_btn_ = nullptr;
    finn::ui::FinnLabel* folder_title_ = nullptr;
    finn::ui::FinnBreadcrumbs* breadcrumbs_ = nullptr;
    finn::ui::FinnButton* view_toggle_btn_ = nullptr;

    finn::ui::FinnCard* action_bar_ = nullptr;
    finn::ui::FinnLabel* selection_lbl_ = nullptr;
    finn::ui::FinnButton* open_btn_ = nullptr;
    finn::ui::FinnButton* wine_btn_ = nullptr;
    finn::ui::FinnButton* rename_btn_ = nullptr;
    finn::ui::FinnButton* copy_btn_ = nullptr;
    finn::ui::FinnButton* cut_btn_ = nullptr;
    finn::ui::FinnButton* paste_btn_ = nullptr;
    finn::ui::FinnButton* delete_btn_ = nullptr;
    finn::ui::FinnButton* hidden_btn_ = nullptr;

    QScrollArea* scroll_ = nullptr;
    QWidget* cards_host_ = nullptr;
    QVBoxLayout* cards_vbox_ = nullptr;
    QGridLayout* cards_grid_ = nullptr;
    std::vector<FileEntryCard*> cards_;

    finn::ui::FinnLabel* status_label_ = nullptr;
    finn::ui::FinnLabel* detail_label_ = nullptr;

    QString current_dir_;
    QString selected_path_;
    QStringList history_;
    bool grid_mode_ = true;
    bool show_hidden_ = false;
    QString clip_path_;
    bool clip_cut_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-files 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-files [directory]");
            return 0;
        }
    }
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("files"),
                                  QStringLiteral("Finn Files"));
    const QString start = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    FilesWindow win(start);
    win.show();
    return app.exec();
}
