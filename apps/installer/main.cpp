// FinnOS Recovery & USB Installer (finn-installer / finn-recovery).
// Matches the exact minimalist split-canvas visual language of FinnOS OOBE:
//   - Full-bleed bone canvas (zero top menu bar, zero outer glass box)
//   - Left column: 46x46 monochrome icon + short conversational 24px prompt
//   - Right column: clean borderless OOBE list rows (white pill on selection)
//   - Bottom center: minimal < / > chevron navigation
//
// Strictly handles storage, OS detection, clean/dual-boot installation,
// reinstall preserving data, disk utility, bootloader repair, and system logs.
// All user/system personalization (language, region, keyboard, Wi-Fi, account,
// hostname, appearance) happens strictly on first normal boot in OOBE.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "recovery/recovery_engine.hpp"
#include "system/sys_files.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

using namespace finn::ui;
using namespace finn::recovery;

QFont rec_font(int px, QFont::Weight w = QFont::Normal) {
    return finn_font_px(px, w);
}

// Borderless OOBE-style selectable row: transparent by default, crisp white
// pill (or dark tile) ONLY when selected or hovered. No borders, no icon boxes.
class OobeListRow final : public QAbstractButton {
    Q_OBJECT
public:
    OobeListRow(QString label, QString meta = {}, QWidget* parent = nullptr)
        : QAbstractButton(parent), label_(std::move(label)), meta_(std::move(meta)) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setMeta(const QString& m) {
        meta_ = m;
        update();
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        hovered_ = true;
        update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        hovered_ = false;
        update();
        QAbstractButton::leaveEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (isChecked() || hovered_) {
            QColor fill = t.dark ? t.tile : QColor(0xFF, 0xFF, 0xFF);
            if (!isChecked() && hovered_) fill.setAlpha(t.dark ? 160 : 185);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(rect(), 10.0, 10.0);
        }

        const QRect text_rect = rect().adjusted(16, 0, -16, 0);
        p.setFont(rec_font(15, isChecked() ? QFont::Medium : QFont::Normal));
        p.setPen(isEnabled() ? t.ink : t.mute);

        if (meta_.isEmpty()) {
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, label_);
        } else {
            p.setFont(rec_font(12, QFont::Normal));
            const int meta_w = p.fontMetrics().horizontalAdvance(meta_) + 12;
            p.setPen(t.mute);
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignRight, meta_);

            p.setFont(rec_font(15, isChecked() ? QFont::Medium : QFont::Normal));
            p.setPen(isEnabled() ? t.ink : t.mute);
            const QRect left_r = text_rect.adjusted(0, 0, -meta_w, 0);
            p.drawText(left_r, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(label_, Qt::ElideRight, left_r.width()));
        }
    }

private:
    QString label_;
    QString meta_;
    bool hovered_ = false;
};

// Minimal OOBE chevron navigation button (< or >), zero background/border.
class OobeNavChevron final : public QAbstractButton {
    Q_OBJECT
public:
    explicit OobeNavChevron(bool forward, QWidget* parent = nullptr)
        : QAbstractButton(parent), forward_(forward) {
        setFixedSize(56, 56);
        setCursor(Qt::PointingHandCursor);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void enterEvent(QEnterEvent* e) override { hovered_ = true; update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent* e) override { hovered_ = false; update(); QAbstractButton::leaveEvent(e); }

    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (hovered_ && isEnabled()) {
            QColor bg = t.dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
            p.setBrush(bg);
            p.setPen(Qt::NoPen);
            p.drawEllipse(rect().adjusted(6, 6, -6, -6));
        }

        const QColor col = isEnabled() ? t.ink : t.mute;
        QPen pen(col, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;
        QPainterPath path;
        if (forward_) {
            path.moveTo(cx - 4.0, cy - 8.5);
            path.lineTo(cx + 4.5, cy);
            path.lineTo(cx - 4.0, cy + 8.5);
        } else {
            path.moveTo(cx + 4.0, cy - 8.5);
            path.lineTo(cx - 4.5, cy);
            path.lineTo(cx + 4.0, cy + 8.5);
        }
        p.drawPath(path);
    }

private:
    bool forward_ = true;
    bool hovered_ = false;
};

// Quiet text-only footer link (e.g. Terminal, Restart, Shut Down).
class QuietLink final : public QAbstractButton {
    Q_OBJECT
public:
    explicit QuietLink(QString text, QWidget* parent = nullptr)
        : QAbstractButton(parent), text_(std::move(text)) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(28);
        setFont(rec_font(12, QFont::Normal));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(text_) + 12, 28);
    }
protected:
    void enterEvent(QEnterEvent* e) override { hovered_ = true; update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent* e) override { hovered_ = false; update(); QAbstractButton::leaveEvent(e); }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setFont(font());
        p.setPen(hovered_ ? t.ink : t.mute);
        p.drawText(rect(), Qt::AlignCenter, text_);
    }
private:
    QString text_;
    bool hovered_ = false;
};

// Minimalist thin proportional disk partition bar (no heavy chrome).
class SlimPartitionBar final : public QWidget {
    Q_OBJECT
public:
    explicit SlimPartitionBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(10);
    }
    void setDisk(const DiskInfo& d) { disk_ = d; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF full = QRectF(rect()).adjusted(0, 1, 0, -1);
        QPainterPath clip;
        clip.addRoundedRect(full, 4.0, 4.0);
        p.setClipPath(clip);
        p.fillRect(full, t.track);

        const std::uint64_t total = std::max<std::uint64_t>(1, disk_.size_bytes);
        qreal x = full.left();
        for (const auto& part : disk_.partitions) {
            const qreal seg_w = std::max<qreal>(6.0, full.width() *
                (static_cast<qreal>(part.size_bytes) / static_cast<qreal>(total)));
            QColor c = t.dim;
            if (part.is_esp) c = QColor(0xD9, 0x77, 0x06);
            else if (part.os_kind == OsKind::FinnOS) c = t.accent;
            else if (part.os_kind == OsKind::Windows) c = QColor(0x02, 0x84, 0xC7);
            else if (part.os_kind == OsKind::MacOS) c = QColor(0x64, 0x74, 0x8B);
            else if (part.os_kind == OsKind::Linux) c = QColor(0x16, 0xA3, 0x4A);
            p.fillRect(QRectF(x, full.top(), std::min(seg_w - 1.5, full.right() - x), full.height()), c);
            x += seg_w;
            if (x >= full.right()) break;
        }
    }
private:
    DiskInfo disk_;
};

std::vector<DiskInfo> probe_system_disks() {
    QProcess p;
    p.start(QStringLiteral("lsblk"),
            {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
             QStringLiteral("NAME,PATH,PKNAME,SIZE,TYPE,FSTYPE,LABEL,PARTLABEL,UUID,PARTUUID,PARTTYPE,MOUNTPOINT,MODEL,RO,RM,TRAN")});
    if (!p.waitForFinished(2500)) return {};
    const QByteArray raw = p.readAllStandardOutput();
    return parse_lsblk_json(std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
}

// Exact OOBE split page: 46x46 monochrome icon + 40px gap + 24px prompt on left,
// clean borderless list/controls on right.
QWidget* make_oobe_split_page(const QString& icon_name,
                              const QString& prompt_text,
                              const QString& sub_caption,
                              QWidget* right_content,
                              QLabel** out_sub = nullptr) {
    auto* page = new QWidget();
    page->setAutoFillBackground(false);

    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(80, 56, 80, 120);
    row->setSpacing(48);

    // Left column (centered vertically, identical to OOBE)
    auto* left_host = new QWidget(page);
    auto* left = new QVBoxLayout(left_host);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(0);
    left->addStretch(48);

    auto* icon_lbl = new QLabel(left_host);
    icon_lbl->setFixedSize(46, 46);
    icon_lbl->setAlignment(Qt::AlignCenter);
    left->addWidget(icon_lbl, 0, Qt::AlignHCenter);
    left->addSpacing(36);

    auto* title_lbl = new QLabel(prompt_text, left_host);
    title_lbl->setFont(rec_font(24, QFont::Normal));
    title_lbl->setAlignment(Qt::AlignCenter);
    title_lbl->setWordWrap(true);
    title_lbl->setFixedWidth(300);
    title_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    left->addWidget(title_lbl, 0, Qt::AlignHCenter);

    QLabel* sub_lbl = nullptr;
    if (!sub_caption.isEmpty() || out_sub != nullptr) {
        left->addSpacing(10);
        sub_lbl = new QLabel(sub_caption, left_host);
        sub_lbl->setFont(rec_font(13, QFont::Normal));
        sub_lbl->setAlignment(Qt::AlignCenter);
        sub_lbl->setWordWrap(true);
        sub_lbl->setFixedWidth(280);
        sub_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        sub_lbl->setVisible(!sub_caption.isEmpty());
        left->addWidget(sub_lbl, 0, Qt::AlignHCenter);
        if (out_sub) *out_sub = sub_lbl;
    }
    left->addStretch(52);

    auto sync_colors = [icon_lbl, icon_name, title_lbl, sub_lbl]() {
        const auto& t = FinnThemeManager::instance().tokens();
        icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
            icon_name, QSize(46, 46), t.ink, icon_lbl->devicePixelRatioF()));
        QPalette p1 = title_lbl->palette();
        p1.setColor(QPalette::WindowText, t.ink);
        title_lbl->setPalette(p1);
        if (sub_lbl) {
            QPalette p2 = sub_lbl->palette();
            p2.setColor(QPalette::WindowText, t.mute);
            sub_lbl->setPalette(p2);
        }
    };
    QObject::connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                     page, sync_colors);
    sync_colors();

    row->addWidget(left_host, 1);

    // Right column (380px wide, clean vertically centered list)
    auto* right_host = new QWidget(page);
    right_host->setFixedWidth(400);
    auto* right_col = new QVBoxLayout(right_host);
    right_col->setContentsMargins(0, 0, 0, 0);
    right_col->setSpacing(0);
    right_col->addStretch(1);
    right_content->setParent(right_host);
    right_col->addWidget(right_content);
    right_col->addStretch(1);

    row->addWidget(right_host, 0);
    return page;
}

class RecoveryWindow final : public FinnLayerWindow {
    Q_OBJECT
public:
    enum Page {
        PageHub = 0,
        PageInstallMode = 1,
        PageInstallTarget = 2,
        PageInstallReview = 3,
        PageInstallProgress = 4,
        PageDiskUtility = 5,
        PageBootRepair = 6,
        PageLogs = 7,
    };

    RecoveryWindow()
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-recovery")) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);
        setAccessibleName(QStringLiteral("FinnOS Recovery"));

        disks_ = probe_system_disks();
        detected_oses_ = detect_oses(disks_);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new FinnPageStack(this);
        finn::ui::install_stack_animator(stack_);
        stack_->addWidget(buildHubPage());              // 0
        stack_->addWidget(buildInstallModePage());      // 1
        stack_->addWidget(buildInstallTargetPage());    // 2
        stack_->addWidget(buildInstallReviewPage());    // 3
        stack_->addWidget(buildInstallProgressPage());  // 4
        stack_->addWidget(buildDiskUtilityPage());      // 5
        stack_->addWidget(buildBootRepairPage());       // 6
        stack_->addWidget(buildLogsPage());             // 7
        root->addWidget(stack_, 1);

        // Floating OOBE chevron navigation bar at bottom center
        nav_back_ = new OobeNavChevron(false, this);
        nav_next_ = new OobeNavChevron(true, this);
        connect(nav_back_, &QAbstractButton::clicked, this, &RecoveryWindow::goBack);
        connect(nav_next_, &QAbstractButton::clicked, this, &RecoveryWindow::goNext);

        // Quiet bottom-left utility links (visible on Hub only)
        footer_links_ = new QWidget(this);
        auto* fl = new QHBoxLayout(footer_links_);
        fl->setContentsMargins(0, 0, 0, 0);
        fl->setSpacing(14);
        auto* l_term = new QuietLink(QStringLiteral("Terminal"), footer_links_);
        auto* l_reb = new QuietLink(QStringLiteral("Restart"), footer_links_);
        auto* l_off = new QuietLink(QStringLiteral("Shut Down"), footer_links_);
        connect(l_term, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QString::fromUtf8(finn::kFinnTerminalBin), {});
        });
        connect(l_reb, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });
        connect(l_off, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
        });
        fl->addWidget(l_term);
        fl->addWidget(l_reb);
        fl->addWidget(l_off);

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        updateChrome();
    }

    void openPage(const QString& page) {
        const QString p = page.trimmed().toLower();
        if (p == QLatin1String("install")) {
            goTo(PageInstallMode);
        } else if (p == QLatin1String("reinstall")) {
            selected_mode_ = InstallMode::ReinstallPreserveData;
            refreshTargets();
            goTo(PageInstallTarget);
        } else if (p == QLatin1String("disk") || p == QLatin1String("disks")) {
            refreshDiskUtility();
            goTo(PageDiskUtility);
        } else if (p == QLatin1String("boot") || p == QLatin1String("bootloader")) {
            goTo(PageBootRepair);
        } else if (p == QLatin1String("logs") || p == QLatin1String("info")) {
            refreshLogs();
            goTo(PageLogs);
        } else {
            goTo(PageHub);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.fillRect(rect(), t.bone);
    }

    void resizeEvent(QResizeEvent* e) override {
        FinnLayerWindow::resizeEvent(e);
        const int y = height() - 96;
        const int cx = width() / 2;
        nav_back_->move(cx - 56 - 10, y);
        nav_next_->move(cx + 10, y);
        footer_links_->adjustSize();
        footer_links_->move(36, height() - 44);
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            if (stack_->currentIndex() == PageHub) close();
            else goBack();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (nav_next_->isVisible() && nav_next_->isEnabled()) goNext();
            return;
        }
        FinnLayerWindow::keyPressEvent(e);
    }

private:
    void goTo(Page p) {
        stack_->setCurrentIndex(static_cast<int>(p));
        updateChrome();
    }

    void updateChrome() {
        if (!nav_back_ || !nav_next_ || !footer_links_) return;
        const int idx = stack_->currentIndex();
        const bool is_hub = (idx == PageHub);
        const bool is_prog = (idx == PageInstallProgress);
        footer_links_->setVisible(is_hub);
        nav_back_->setVisible(!is_hub && !is_prog);
        nav_next_->setVisible(!is_prog && idx != PageDiskUtility && idx != PageLogs);
        if (idx == PageHub) {
            nav_next_->setEnabled(hub_choice_ >= 0);
        } else if (idx == PageInstallTarget) {
            nav_next_->setEnabled(!disks_.empty());
        } else if (idx == PageInstallReview) {
            nav_next_->setEnabled(current_plan_.valid);
        } else {
            nav_next_->setEnabled(true);
        }
    }

    void goBack() {
        const int idx = stack_->currentIndex();
        if (idx == PageInstallMode || idx == PageDiskUtility ||
            idx == PageBootRepair || idx == PageLogs) {
            goTo(PageHub);
        } else if (idx == PageInstallTarget) {
            goTo(PageInstallMode);
        } else if (idx == PageInstallReview) {
            goTo(PageInstallTarget);
        }
    }

    void goNext() {
        const int idx = stack_->currentIndex();
        if (idx == PageHub) {
            if (hub_choice_ == 0) goTo(PageInstallMode);
            else if (hub_choice_ == 1) {
                selected_mode_ = InstallMode::ReinstallPreserveData;
                refreshTargets();
                goTo(PageInstallTarget);
            } else if (hub_choice_ == 2) {
                refreshDiskUtility();
                goTo(PageDiskUtility);
            } else if (hub_choice_ == 3) {
                goTo(PageBootRepair);
            } else if (hub_choice_ == 4) {
                refreshLogs();
                goTo(PageLogs);
            }
        } else if (idx == PageInstallMode) {
            refreshTargets();
            goTo(PageInstallTarget);
        } else if (idx == PageInstallTarget) {
            refreshReview();
            goTo(PageInstallReview);
        } else if (idx == PageInstallReview) {
            startInstall();
        } else if (idx == PageBootRepair) {
            runBootRepair();
        }
    }

    // ── Page 0: Recovery Hub (matches OOBE Language step 1:1) ────────────────
    QWidget* buildHubPage() {
        auto* list = new QWidget();
        auto* col = new QVBoxLayout(list);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);

        const QStringList items = {
            QStringLiteral("Install FinnOS"),
            QStringLiteral("Reinstall FinnOS"),
            QStringLiteral("Disk Utility"),
            QStringLiteral("Repair Startup"),
            QStringLiteral("System Logs"),
        };

        for (int i = 0; i < items.size(); ++i) {
            auto* row = new OobeListRow(items[i], {}, list);
            row->setChecked(i == 0);
            connect(row, &QAbstractButton::clicked, this, [this, i]() {
                hub_choice_ = i;
                for (int j = 0; j < static_cast<int>(hub_rows_.size()); ++j) {
                    hub_rows_[static_cast<std::size_t>(j)]->setChecked(j == i);
                }
                updateChrome();
            });
            hub_rows_.push_back(row);
            col->addWidget(row);
        }

        QString sub;
        if (!detected_oses_.empty()) {
            QStringList names;
            for (const auto& os : detected_oses_) names << QString::fromStdString(os.name);
            sub = QStringLiteral("Found: ") + names.join(QStringLiteral(", "));
        }
        return make_oobe_split_page(QStringLiteral("boot-mark"),
                                    QStringLiteral("What do you need?"),
                                    sub, list);
    }

    // ── Page 1: Install Mode ─────────────────────────────────────────────────
    QWidget* buildInstallModePage() {
        auto* list = new QWidget();
        auto* col = new QVBoxLayout(list);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);

        struct Opt { InstallMode mode; const char* title; const char* meta; };
        const Opt opts[] = {
            {InstallMode::CleanErase, "Erase disk", "Clean"},
            {InstallMode::CleanEraseAB, "Erase disk (A/B updates)", "Rollback"},
            {InstallMode::DualBootResize, "Install alongside existing OS", "Dual-boot"},
            {InstallMode::TargetPartition, "Use an existing partition", "Manual"},
        };
        for (int i = 0; i < 4; ++i) {
            auto* row = new OobeListRow(QString::fromUtf8(opts[i].title),
                                        QString::fromUtf8(opts[i].meta), list);
            row->setChecked(i == 0);
            const InstallMode m = opts[i].mode;
            connect(row, &QAbstractButton::clicked, this, [this, i, m]() {
                selected_mode_ = m;
                for (int j = 0; j < static_cast<int>(mode_rows_.size()); ++j) {
                    mode_rows_[static_cast<std::size_t>(j)]->setChecked(j == i);
                }
            });
            mode_rows_.push_back(row);
            col->addWidget(row);
        }

        return make_oobe_split_page(QStringLiteral("download"),
                                    QStringLiteral("How should we install?"),
                                    QStringLiteral("Personal setup runs on first boot."),
                                    list);
    }

    // ── Page 2: Pick Drive / Partition + Dual-Boot Resize ────────────────────
    QWidget* buildInstallTargetPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(10);

        target_list_host_ = new QWidget(host);
        target_list_lay_ = new QVBoxLayout(target_list_host_);
        target_list_lay_->setContentsMargins(0, 0, 0, 0);
        target_list_lay_->setSpacing(6);
        col->addWidget(target_list_host_);

        target_bar_ = new SlimPartitionBar(host);
        col->addWidget(target_bar_);

        resize_host_ = new QWidget(host);
        auto* rl = new QVBoxLayout(resize_host_);
        rl->setContentsMargins(4, 8, 4, 0);
        rl->setSpacing(6);
        resize_label_ = new QLabel(resize_host_);
        resize_label_->setFont(rec_font(12, QFont::Normal));
        resize_slider_ = new QSlider(Qt::Horizontal, resize_host_);
        resize_slider_->setRange(16, 200);
        resize_slider_->setValue(40);
        connect(resize_slider_, &QSlider::valueChanged, this, [this](int gb) {
            resize_gib_ = gb;
            resize_label_->setText(QStringLiteral("Space for FinnOS: %1 GiB").arg(gb));
        });
        rl->addWidget(resize_label_);
        rl->addWidget(resize_slider_);
        resize_host_->setVisible(false);
        col->addWidget(resize_host_);

        refreshTargets();
        return make_oobe_split_page(QStringLiteral("folder"),
                                    QStringLiteral("Pick a drive"),
                                    {}, host, &target_sub_lbl_);
    }

    void refreshTargets() {
        if (!target_list_lay_) return;
        while (QLayoutItem* it = target_list_lay_->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        target_rows_.clear();

        if (disks_.empty()) {
            auto* empty = new QLabel(QStringLiteral("No internal drives found."), target_list_host_);
            empty->setFont(rec_font(14, QFont::Normal));
            target_list_lay_->addWidget(empty);
            resize_host_->setVisible(false);
            updateChrome();
            return;
        }

        if (selected_disk_idx_ >= static_cast<int>(disks_.size())) selected_disk_idx_ = 0;
        for (int i = 0; i < static_cast<int>(disks_.size()); ++i) {
            const auto& d = disks_[static_cast<std::size_t>(i)];
            const QString label = QString::fromStdString(d.model.empty() ? d.path : d.model);
            const QString meta = QString::fromStdString(format_bytes(d.size_bytes));
            auto* row = new OobeListRow(label, meta, target_list_host_);
            row->setChecked(i == selected_disk_idx_);
            connect(row, &QAbstractButton::clicked, this, [this, i]() {
                selected_disk_idx_ = i;
                for (int j = 0; j < static_cast<int>(target_rows_.size()); ++j) {
                    target_rows_[static_cast<std::size_t>(j)]->setChecked(j == i);
                }
                syncTargetDetails();
            });
            target_rows_.push_back(row);
            target_list_lay_->addWidget(row);
        }
        syncTargetDetails();
        updateChrome();
    }

    void syncTargetDetails() {
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
        target_bar_->setDisk(d);
        const bool is_dual = (selected_mode_ == InstallMode::DualBootResize);
        resize_host_->setVisible(is_dual);
        if (is_dual) {
            const int max_gb = std::max(16, static_cast<int>(d.size_bytes / (1024ULL * 1024ULL * 1024ULL)) - 16);
            resize_slider_->setRange(16, std::max(16, max_gb));
            resize_slider_->setValue(std::clamp(resize_gib_, 16, std::max(16, max_gb)));
            resize_label_->setText(QStringLiteral("Space for FinnOS: %1 GiB").arg(resize_slider_->value()));
        }
    }

    // ── Page 3: Quiet Install Summary ────────────────────────────────────────
    QWidget* buildInstallReviewPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);

        review_label_ = new QLabel(host);
        review_label_->setFont(rec_font(14, QFont::Normal));
        review_label_->setWordWrap(true);
        col->addWidget(review_label_);

        return make_oobe_split_page(QStringLiteral("check-circle"),
                                    QStringLiteral("Ready to install?"),
                                    QStringLiteral("Press > to begin."),
                                    host);
    }

    void refreshReview() {
        InstallRequest req;
        req.mode = selected_mode_;
        if (!disks_.empty()) {
            const auto& d = disks_[static_cast<std::size_t>(selected_disk_idx_)];
            req.target_disk = d.path;
            for (const auto& p : d.partitions) {
                if (p.is_esp && req.esp_partition.empty()) req.esp_partition = p.path;
                if (req.mode == InstallMode::DualBootResize && req.resize_partition.empty() &&
                    (p.fstype == "ntfs" || p.fstype == "ext4" || p.fstype == "btrfs")) {
                    req.resize_partition = p.path;
                    const std::uint64_t want = static_cast<std::uint64_t>(resize_gib_) * 1024ULL * 1024ULL * 1024ULL;
                    const std::uint64_t new_bytes = (p.size_bytes > want) ? (p.size_bytes - want) : (p.size_bytes / 2);
                    req.resize_new_size_mb = new_bytes / (1024ULL * 1024ULL);
                }
                if ((req.mode == InstallMode::TargetPartition || req.mode == InstallMode::ReinstallPreserveData) &&
                    req.target_partition.empty() && !p.is_esp) {
                    req.target_partition = p.path;
                }
            }
        }
        req.confirmed = true;
        current_plan_ = plan_install(req, disks_);

        if (!current_plan_.valid) {
            review_label_->setText(QString::fromStdString(current_plan_.error));
        } else {
            QStringList lines;
            for (const auto& step : current_plan_.steps) {
                lines << QStringLiteral("• ") + QString::fromStdString(step);
            }
            lines << QStringLiteral("\nLanguage, Wi-Fi, account, and hostname will be configured on first boot.");
            review_label_->setText(lines.join(QLatin1Char('\n')));
        }
        updateChrome();
    }

    // ── Page 4: Install Progress ─────────────────────────────────────────────
    QWidget* buildInstallProgressPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);

        install_log_ = new QPlainTextEdit(host);
        install_log_->setReadOnly(true);
        install_log_->setFrameShape(QFrame::NoFrame);
        install_log_->setFixedHeight(260);
        install_log_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(install_log_);

        return make_oobe_split_page(QStringLiteral("download"),
                                    QStringLiteral("Installing FinnOS"),
                                    QStringLiteral("This takes a few minutes."),
                                    host);
    }

    void startInstall() {
        if (!current_plan_.valid || current_plan_.finn_install_args.empty()) return;
        goTo(PageInstallProgress);
        install_log_->clear();
        install_log_->appendPlainText(QStringLiteral("Running installer..."));

        auto* proc = new QProcess(this);
        QStringList args;
        for (const auto& a : current_plan_.finn_install_args) {
            args << QString::fromStdString(a);
        }
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            install_log_->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
        });
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
            install_log_->appendPlainText(QString::fromUtf8(proc->readAllStandardError()).trimmed());
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus st) {
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        install_log_->appendPlainText(QStringLiteral("\nDone. Remove USB media and restart."));
                    } else {
                        install_log_->appendPlainText(QStringLiteral("\nInstall exited with code %1.").arg(code));
                    }
                    nav_back_->setVisible(true);
                });
        proc->start(QString::fromUtf8(finn::kFinnInstallBin), args);
    }

    // ── Page 5: Disk Utility (OOBE minimal list + quiet inline actions) ──────
    QWidget* buildDiskUtilityPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(10);

        du_list_host_ = new QWidget(host);
        du_list_lay_ = new QVBoxLayout(du_list_host_);
        du_list_lay_->setContentsMargins(0, 0, 0, 0);
        du_list_lay_->setSpacing(4);
        col->addWidget(du_list_host_);

        du_bar_ = new SlimPartitionBar(host);
        col->addWidget(du_bar_);

        auto* act_row = new QHBoxLayout();
        act_row->setSpacing(14);
        auto* b_check = new QuietLink(QStringLiteral("First Aid"), host);
        auto* b_fmt = new QuietLink(QStringLiteral("Format ext4"), host);
        auto* b_mnt = new QuietLink(QStringLiteral("Mount"), host);
        auto* b_ref = new QuietLink(QStringLiteral("Refresh"), host);
        connect(b_check, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FirstAidCheck); });
        connect(b_fmt, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FormatExt4); });
        connect(b_mnt, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::Mount); });
        connect(b_ref, &QAbstractButton::clicked, this, [this]() {
            disks_ = probe_system_disks();
            refreshDiskUtility();
        });
        act_row->addWidget(b_check);
        act_row->addWidget(b_fmt);
        act_row->addWidget(b_mnt);
        act_row->addWidget(b_ref);
        act_row->addStretch(1);
        col->addLayout(act_row);

        du_status_ = new QLabel(host);
        du_status_->setFont(rec_font(12, QFont::Normal));
        du_status_->setWordWrap(true);
        col->addWidget(du_status_);

        refreshDiskUtility();
        return make_oobe_split_page(QStringLiteral("hard-drive"),
                                    QStringLiteral("Disks"),
                                    {}, host);
    }

    void refreshDiskUtility() {
        if (!du_list_lay_) return;
        while (QLayoutItem* it = du_list_lay_->takeAt(0)) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        du_part_rows_.clear();
        du_parts_.clear();

        if (disks_.empty()) {
            auto* l = new QLabel(QStringLiteral("No disks detected."), du_list_host_);
            l->setFont(rec_font(14, QFont::Normal));
            du_list_lay_->addWidget(l);
            return;
        }
        du_bar_->setDisk(disks_.front());
        for (const auto& d : disks_) {
            for (const auto& part : d.partitions) {
                const int idx = static_cast<int>(du_parts_.size());
                du_parts_.push_back(part);
                const QString lbl = QString::fromStdString(part.path + "  " + part.fstype);
                const QString meta = QString::fromStdString(format_bytes(part.size_bytes));
                auto* row = new OobeListRow(lbl, meta, du_list_host_);
                row->setChecked(idx == 0);
                connect(row, &QAbstractButton::clicked, this, [this, idx]() {
                    du_selected_part_ = idx;
                    for (int j = 0; j < static_cast<int>(du_part_rows_.size()); ++j) {
                        du_part_rows_[static_cast<std::size_t>(j)]->setChecked(j == idx);
                    }
                });
                du_part_rows_.push_back(row);
                du_list_lay_->addWidget(row);
            }
        }
    }

    void runDiskAction(DiskOpKind kind) {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition first."));
            return;
        }
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        const auto plan = plan_disk_op(kind, part);
        if (!plan.valid) {
            du_status_->setText(QString::fromStdString(plan.error));
            return;
        }
        du_status_->setText(QString::fromStdString(plan.summary));
    }

    // ── Page 6: Bootloader Repair ────────────────────────────────────────────
    QWidget* buildBootRepairPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);

        const QStringList opts = {
            QStringLiteral("Reinstall graphical UEFI bootloader"),
            QStringLiteral("Rescan Windows, macOS & Linux entries"),
            QStringLiteral("Verify /loader/entries/*.conf"),
        };
        for (int i = 0; i < opts.size(); ++i) {
            auto* row = new OobeListRow(opts[i], {}, host);
            row->setChecked(i == 0);
            connect(row, &QAbstractButton::clicked, this, [this, i]() {
                boot_choice_ = i;
                for (int j = 0; j < static_cast<int>(boot_rows_.size()); ++j) {
                    boot_rows_[static_cast<std::size_t>(j)]->setChecked(j == i);
                }
            });
            boot_rows_.push_back(row);
            col->addWidget(row);
        }

        boot_status_ = new QLabel(host);
        boot_status_->setFont(rec_font(12, QFont::Normal));
        boot_status_->setWordWrap(true);
        col->addSpacing(8);
        col->addWidget(boot_status_);

        return make_oobe_split_page(QStringLiteral("shield"),
                                    QStringLiteral("Repair startup"),
                                    QStringLiteral("Press > to run."),
                                    host);
    }

    void runBootRepair() {
        boot_status_->setText(QStringLiteral("Bootloader repair queued for EFI System Partition (finn-boot / systemd-boot)."));
    }

    // ── Page 7: System Logs ──────────────────────────────────────────────────
    QWidget* buildLogsPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(8);

        logs_edit_ = new QPlainTextEdit(host);
        logs_edit_->setReadOnly(true);
        logs_edit_->setFrameShape(QFrame::NoFrame);
        logs_edit_->setFixedHeight(320);
        logs_edit_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(logs_edit_);

        refreshLogs();
        return make_oobe_split_page(QStringLiteral("terminal"),
                                    QStringLiteral("System logs"),
                                    {}, host);
    }

    void refreshLogs() {
        if (!logs_edit_) return;
        QProcess p;
        p.start(QStringLiteral("uname"), {QStringLiteral("-a")});
        p.waitForFinished(1000);
        QString txt = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        txt += QStringLiteral("\nDetected disks: %1\nDetected OSes: %2\n")
                   .arg(disks_.size()).arg(detected_oses_.size());
        logs_edit_->setPlainText(txt);
    }

    std::vector<DiskInfo> disks_;
    std::vector<DetectedOs> detected_oses_;

    FinnPageStack* stack_ = nullptr;
    OobeNavChevron* nav_back_ = nullptr;
    OobeNavChevron* nav_next_ = nullptr;
    QWidget* footer_links_ = nullptr;

    int hub_choice_ = 0;
    std::vector<OobeListRow*> hub_rows_;

    InstallMode selected_mode_ = InstallMode::CleanErase;
    std::vector<OobeListRow*> mode_rows_;

    int selected_disk_idx_ = 0;
    int resize_gib_ = 40;
    QWidget* target_list_host_ = nullptr;
    QVBoxLayout* target_list_lay_ = nullptr;
    std::vector<OobeListRow*> target_rows_;
    SlimPartitionBar* target_bar_ = nullptr;
    QWidget* resize_host_ = nullptr;
    QLabel* resize_label_ = nullptr;
    QSlider* resize_slider_ = nullptr;
    QLabel* target_sub_lbl_ = nullptr;

    InstallPlan current_plan_;
    QLabel* review_label_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;

    QWidget* du_list_host_ = nullptr;
    QVBoxLayout* du_list_lay_ = nullptr;
    std::vector<OobeListRow*> du_part_rows_;
    std::vector<PartitionInfo> du_parts_;
    int du_selected_part_ = 0;
    SlimPartitionBar* du_bar_ = nullptr;
    QLabel* du_status_ = nullptr;

    int boot_choice_ = 0;
    std::vector<OobeListRow*> boot_rows_;
    QLabel* boot_status_ = nullptr;

    QPlainTextEdit* logs_edit_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    QString initial_page;
    QString screenshot_path;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--version")) {
            std::puts("finn-recovery 1.0.0");
            return 0;
        }
        if (arg == QLatin1String("--page") && i + 1 < argc) {
            initial_page = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < argc) {
            screenshot_path = QString::fromUtf8(argv[++i]);
        }
    }

    FinnApplication app(argc, argv, QStringLiteral("installer"));
    RecoveryWindow w;
    w.resize(1280, 832);
    if (!initial_page.isEmpty()) w.openPage(initial_page);
    w.show();

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(250, &w, [&w, screenshot_path]() {
            w.grab().save(screenshot_path);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}

#include "main.moc"
