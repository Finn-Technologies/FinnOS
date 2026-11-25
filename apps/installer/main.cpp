// FinnOS Recovery & USB Installer (finn-installer / finn-recovery).
// Pixel-aligned with FinnOS OOBE (apps/oobe/main.cpp):
//   - Full-bleed bone canvas (zero top menu bar, zero outer glass box)
//   - Outer gutter 80px; left column (80px inset, 46x46 Hugeicons glyph + 40px gap
//     + 24px Regular prompt); 132px center gap; right 350px column at y=195
//   - 350x37 OobeListRow pills (10px radius, 14px Regular, white pill when selected)
//   - 56x56 circular < / > chevrons centered at y = height - 201 (20px spacing)
//
// Handles storage, real GPT partitioning (create, delete, format, first aid, wipe),
// OS detection, clean/A-B/dual-boot/manual installation, reinstall preserving data,
// bootloader repair, and system logs.

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

constexpr int kOobeGutter = 80;
constexpr int kOobeContentWidth = 350;
constexpr int kOobeRowHeight = 37;

QFont rec_font(int px, QFont::Weight w = QFont::Normal) {
    return finn_font_px(px, w);
}

// Exact OOBE selectable 350x37 row (matching OobeOptionRow in apps/oobe/main.cpp).
class OobeListRow final : public QAbstractButton {
    Q_OBJECT
public:
    OobeListRow(QString label, QString meta = {}, QWidget* parent = nullptr)
        : QAbstractButton(parent), label_(std::move(label)), meta_(std::move(meta)) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(kOobeContentWidth, kOobeRowHeight);
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
            QColor fill = t.dark ? t.tile_on : QColor(0xFF, 0xFF, 0xFF);
            if (!isChecked() && hovered_) fill.setAlpha(t.dark ? 140 : 165);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(rect(), 10.0, 10.0);
        }

        const QRect text_rect = rect().adjusted(14, 0, -14, 0);
        if (meta_.isEmpty()) {
            p.setFont(rec_font(14, QFont::Normal));
            p.setPen(isEnabled() ? t.ink : t.mute);
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(label_, Qt::ElideRight, text_rect.width()));
        } else {
            p.setFont(rec_font(12, QFont::Normal));
            const int meta_w = p.fontMetrics().horizontalAdvance(meta_) + 10;
            p.setPen(t.mute);
            p.drawText(text_rect, Qt::AlignVCenter | Qt::AlignRight, meta_);

            p.setFont(rec_font(14, QFont::Normal));
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

// Exact OOBE 56x56 circular chevron navigation button (< or >).
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
            p.drawEllipse(rect().adjusted(4, 4, -4, -4));
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

// Quiet OOBE text-only inline action link.
class QuietLink final : public QAbstractButton {
    Q_OBJECT
public:
    explicit QuietLink(QString text, bool destructive = false, QWidget* parent = nullptr)
        : QAbstractButton(parent), text_(std::move(text)), destructive_(destructive) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(26);
        setFont(rec_font(12, QFont::Normal));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void setLabelText(const QString& t) {
        text_ = t;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(text_) + 10, 26);
    }
protected:
    void enterEvent(QEnterEvent* e) override { hovered_ = true; update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent* e) override { hovered_ = false; update(); QAbstractButton::leaveEvent(e); }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setFont(font());
        QColor col = destructive_ ? (hovered_ ? t.danger : t.mute) : (hovered_ ? t.ink : t.mute);
        if (!isEnabled()) col = t.mute;
        p.setPen(col);
        p.drawText(rect(), Qt::AlignCenter, text_);
    }
private:
    QString text_;
    bool destructive_ = false;
    bool hovered_ = false;
};

// Minimalist thin proportional disk partition bar (zero extra chrome).
class SlimPartitionBar final : public QWidget {
    Q_OBJECT
public:
    explicit SlimPartitionBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(kOobeContentWidth, 8);
    }
    void setDisk(const DiskInfo& d) { disk_ = d; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF full = QRectF(rect());
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

// Exact pixel-for-pixel OOBE split page (matching createSplitStepPage in apps/oobe/main.cpp):
//   - Outer gutter: 80px left/right, 0 top/bottom
//   - Left column: 80px left / 20px right inset, 49/51 stretch, 46x46 icon + 40px gap + 24px prompt (w=380)
//   - Center spacing: 132px
//   - Right column: 350px wide at top=195px, right=45px
QWidget* make_oobe_split_page(const QString& icon_name,
                              const QString& prompt_text,
                              const QString& sub_caption,
                              QWidget* right_content,
                              QLabel** out_sub = nullptr) {
    auto* page = new QWidget();
    page->setAutoFillBackground(false);

    auto* main_col = new QVBoxLayout(page);
    main_col->setContentsMargins(kOobeGutter, 0, kOobeGutter, 0);
    main_col->setSpacing(0);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(32);

    auto* left_host = new QWidget(page);
    auto* left = new QVBoxLayout(left_host);
    left->setContentsMargins(80, 0, 20, 0);
    left->setSpacing(0);
    left->addStretch(49);

    auto* icon_lbl = new QLabel(left_host);
    icon_lbl->setFixedSize(46, 46);
    icon_lbl->setAlignment(Qt::AlignCenter);
    left->addWidget(icon_lbl, 0, Qt::AlignHCenter);
    left->addSpacing(40);

    auto* title_lbl = new QLabel(prompt_text, left_host);
    title_lbl->setFont(rec_font(24, QFont::Normal));
    title_lbl->setAlignment(Qt::AlignCenter);
    title_lbl->setWordWrap(true);
    title_lbl->setFixedWidth(380);
    title_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    left->addWidget(title_lbl, 0, Qt::AlignHCenter);

    QLabel* sub_lbl = nullptr;
    if (!sub_caption.isEmpty() || out_sub != nullptr) {
        left->addSpacing(10);
        sub_lbl = new QLabel(sub_caption, left_host);
        sub_lbl->setFont(rec_font(14, QFont::Normal));
        sub_lbl->setAlignment(Qt::AlignCenter);
        sub_lbl->setWordWrap(true);
        sub_lbl->setFixedWidth(280);
        sub_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        sub_lbl->setVisible(!sub_caption.isEmpty());
        left->addWidget(sub_lbl, 0, Qt::AlignHCenter);
        if (out_sub) *out_sub = sub_lbl;
    }
    left->addStretch(51);

    auto sync_colors = [icon_lbl, icon_name, title_lbl, sub_lbl]() {
        const auto& t = FinnThemeManager::instance().tokens();
        icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
            icon_name, QSize(46, 46), t.ink, icon_lbl->devicePixelRatioF()));
        QPalette p1 = title_lbl->palette();
        p1.setColor(QPalette::WindowText, t.ink);
        title_lbl->setPalette(p1);
        if (sub_lbl) {
            QPalette p2 = sub_lbl->palette();
            p2.setColor(QPalette::WindowText, t.dim);
            sub_lbl->setPalette(p2);
        }
    };
    QObject::connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                     page, sync_colors);
    sync_colors();

    auto* right_host = new QWidget(page);
    auto* right_col = new QVBoxLayout(right_host);
    right_col->setContentsMargins(0, 195, 45, 160);
    right_col->setSpacing(0);
    right_content->setParent(right_host);
    right_content->setFixedWidth(kOobeContentWidth);
    right_col->addWidget(right_content, 1, Qt::AlignRight | Qt::AlignTop);

    row->addWidget(left_host, 1);
    row->addSpacing(132);
    row->addWidget(right_host, 1, Qt::AlignRight);
    main_col->addLayout(row, 1);
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

        // Exact OOBE floating chevron navigation bar (56x56, 20px gap, y = height - 201)
        nav_back_ = new OobeNavChevron(false, this);
        nav_next_ = new OobeNavChevron(true, this);
        connect(nav_back_, &QAbstractButton::clicked, this, &RecoveryWindow::goBack);
        connect(nav_next_, &QAbstractButton::clicked, this, &RecoveryWindow::goNext);

        // Quiet bottom-left utility links (visible on Hub only)
        footer_links_ = new QWidget(this);
        auto* fl = new QHBoxLayout(footer_links_);
        fl->setContentsMargins(0, 0, 0, 0);
        fl->setSpacing(14);
        auto* l_term = new QuietLink(QStringLiteral("Terminal"), false, footer_links_);
        auto* l_reb = new QuietLink(QStringLiteral("Restart"), false, footer_links_);
        auto* l_off = new QuietLink(QStringLiteral("Shut Down"), false, footer_links_);
        connect(l_term, &QAbstractButton::clicked, this, []() {
            const QString plat = QGuiApplication::platformName();
            if (plat == QLatin1String("eglfs") || plat == QLatin1String("linuxfb")) {
                QCoreApplication::exit(42);
                return;
            }
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
        } else if (p == QLatin1String("disk") || p == QLatin1String("disks") || p == QLatin1String("partition")) {
            du_return_to_target_ = false;
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
        // Exact OOBE chevron position: 145px above bottom edge (y = height - 145 - 56)
        const int y = std::max(16, height() - 145 - 56);
        const int cx = width() / 2;
        nav_back_->move(cx - 56 - 10, y);
        nav_next_->move(cx + 10, y);
        footer_links_->adjustSize();
        footer_links_->move(kOobeGutter, height() - 48);
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
        if (idx == PageDiskUtility && du_return_to_target_) {
            du_return_to_target_ = false;
            disks_ = probe_system_disks();
            refreshTargets();
            goTo(PageInstallTarget);
            return;
        }
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
                du_return_to_target_ = false;
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

    // ── Page 0: Recovery Hub (exact OOBE Language step geometry) ─────────────
    QWidget* buildHubPage() {
        auto* list = new QWidget();
        auto* col = new QVBoxLayout(list);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);

        const QStringList items = {
            QStringLiteral("Install FinnOS"),
            QStringLiteral("Reinstall FinnOS"),
            QStringLiteral("Partition & Disks"),
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
        col->addStretch(1);

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
        col->setSpacing(4);

        struct Opt { InstallMode mode; const char* title; const char* meta; };
        const Opt opts[] = {
            {InstallMode::CleanErase, "Erase disk", "Clean"},
            {InstallMode::CleanEraseAB, "Erase disk (A/B updates)", "Rollback"},
            {InstallMode::DualBootResize, "Install alongside existing OS", "Dual-boot"},
            {InstallMode::TargetPartition, "Use a specific partition", "Manual"},
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
        col->addStretch(1);

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
        target_list_lay_->setSpacing(4);
        col->addWidget(target_list_host_);

        target_bar_ = new SlimPartitionBar(host);
        col->addWidget(target_bar_);

        resize_host_ = new QWidget(host);
        auto* rl = new QVBoxLayout(resize_host_);
        rl->setContentsMargins(2, 6, 2, 0);
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

        auto* edit_parts = new QuietLink(QStringLiteral("Partition drive..."), false, host);
        connect(edit_parts, &QAbstractButton::clicked, this, [this]() {
            du_return_to_target_ = true;
            refreshDiskUtility();
            goTo(PageDiskUtility);
        });
        col->addWidget(edit_parts, 0, Qt::AlignLeft);
        col->addStretch(1);

        refreshTargets();
        return make_oobe_split_page(QStringLiteral("hard-drive"),
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
        target_part_paths_.clear();

        if (disks_.empty()) {
            auto* empty = new QLabel(QStringLiteral("No internal drives found."), target_list_host_);
            empty->setFont(rec_font(14, QFont::Normal));
            target_list_lay_->addWidget(empty);
            resize_host_->setVisible(false);
            updateChrome();
            return;
        }

        const bool pick_partition =
            (selected_mode_ == InstallMode::TargetPartition ||
             selected_mode_ == InstallMode::ReinstallPreserveData);

        if (pick_partition) {
            int row_idx = 0;
            for (int di = 0; di < static_cast<int>(disks_.size()); ++di) {
                const auto& d = disks_[static_cast<std::size_t>(di)];
                for (const auto& part : d.partitions) {
                    if (part.is_esp) continue;
                    const int idx = row_idx++;
                    const QString lbl = QString::fromStdString(part.path + (part.fstype.empty() ? "" : "  " + part.fstype));
                    const QString meta = QString::fromStdString(format_bytes(part.size_bytes));
                    auto* row = new OobeListRow(lbl, meta, target_list_host_);
                    row->setChecked(idx == 0);
                    const std::string ppath = part.path;
                    connect(row, &QAbstractButton::clicked, this, [this, idx, di, ppath]() {
                        selected_disk_idx_ = di;
                        selected_partition_path_ = ppath;
                        for (int j = 0; j < static_cast<int>(target_rows_.size()); ++j) {
                            target_rows_[static_cast<std::size_t>(j)]->setChecked(j == idx);
                        }
                        syncTargetDetails();
                    });
                    if (idx == 0) {
                        selected_disk_idx_ = di;
                        selected_partition_path_ = ppath;
                    }
                    target_part_paths_.push_back(ppath);
                    target_rows_.push_back(row);
                    target_list_lay_->addWidget(row);
                }
            }
            if (target_rows_.empty()) {
                auto* empty = new QLabel(QStringLiteral("No partitions found. Use Partition drive first."), target_list_host_);
                empty->setFont(rec_font(13, QFont::Normal));
                empty->setWordWrap(true);
                target_list_lay_->addWidget(empty);
            }
        } else {
            selected_partition_path_.clear();
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
        col->addStretch(1);

        return make_oobe_split_page(QStringLiteral("shield"),
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
            }
            if (req.mode == InstallMode::TargetPartition || req.mode == InstallMode::ReinstallPreserveData) {
                req.target_partition = selected_partition_path_;
                if (req.target_partition.empty()) {
                    for (const auto& p : d.partitions) {
                        if (!p.is_esp) {
                            req.target_partition = p.path;
                            break;
                        }
                    }
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
        install_log_->setFixedHeight(280);
        install_log_->setFont(finn_mono_font(11.0, QFont::Normal));
        col->addWidget(install_log_);
        col->addStretch(1);

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

    // ── Page 5: Partition & Disk Utility (100% OOBE-aligned, real partitioning) ─
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

        // Quiet OOBE-style inline partitioning action bar
        auto* act_row = new QHBoxLayout();
        act_row->setSpacing(10);
        auto* b_new = new QuietLink(QStringLiteral("New"), false, host);
        auto* b_del = new QuietLink(QStringLiteral("Delete"), true, host);
        auto* b_fmt = new QuietLink(QStringLiteral("Format"), false, host);
        auto* b_chk = new QuietLink(QStringLiteral("First Aid"), false, host);
        auto* b_gpt = new QuietLink(QStringLiteral("New GPT"), true, host);
        auto* b_ref = new QuietLink(QStringLiteral("Refresh"), false, host);
        connect(b_new, &QAbstractButton::clicked, this, [this]() {
            du_create_panel_->setVisible(!du_create_panel_->isVisible());
        });
        connect(b_del, &QAbstractButton::clicked, this, &RecoveryWindow::deleteSelectedPartition);
        connect(b_fmt, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FormatExt4); });
        connect(b_chk, &QAbstractButton::clicked, this, [this]() { runDiskAction(DiskOpKind::FirstAidCheck); });
        connect(b_gpt, &QAbstractButton::clicked, this, &RecoveryWindow::wipeDiskGpt);
        connect(b_ref, &QAbstractButton::clicked, this, [this]() {
            disks_ = probe_system_disks();
            refreshDiskUtility();
            du_status_->setText(QStringLiteral("Refreshed disk table."));
        });
        act_row->addWidget(b_new);
        act_row->addWidget(b_del);
        act_row->addWidget(b_fmt);
        act_row->addWidget(b_chk);
        act_row->addWidget(b_gpt);
        act_row->addWidget(b_ref);
        act_row->addStretch(1);
        col->addLayout(act_row);

        // Minimal OOBE inline New Partition panel (hidden until "New" is clicked)
        du_create_panel_ = new QWidget(host);
        auto* cp = new QVBoxLayout(du_create_panel_);
        cp->setContentsMargins(0, 4, 0, 4);
        cp->setSpacing(6);

        du_new_size_lbl_ = new QLabel(QStringLiteral("New partition: 32 GiB (ext4)"), du_create_panel_);
        du_new_size_lbl_->setFont(rec_font(12, QFont::Normal));
        du_new_size_slider_ = new QSlider(Qt::Horizontal, du_create_panel_);
        du_new_size_slider_->setRange(1, 512);
        du_new_size_slider_->setValue(32);
        connect(du_new_size_slider_, &QSlider::valueChanged, this, [this](int gb) {
            du_new_size_gib_ = gb;
            du_new_size_lbl_->setText(
                QStringLiteral("New partition: %1 GiB (%2)").arg(gb).arg(du_new_fs_));
        });

        auto* cp_row = new QHBoxLayout();
        cp_row->setSpacing(12);
        auto* b_fs = new QuietLink(QStringLiteral("Filesystem: ext4"), false, du_create_panel_);
        connect(b_fs, &QAbstractButton::clicked, this, [this, b_fs]() {
            du_new_fs_ = (du_new_fs_ == QLatin1String("ext4")) ? QStringLiteral("btrfs")
                       : (du_new_fs_ == QLatin1String("btrfs")) ? QStringLiteral("fat32")
                       : QStringLiteral("ext4");
            b_fs->setLabelText(QStringLiteral("Filesystem: %1").arg(du_new_fs_));
            du_new_size_lbl_->setText(
                QStringLiteral("New partition: %1 GiB (%2)").arg(du_new_size_gib_).arg(du_new_fs_));
        });
        auto* b_apply = new QuietLink(QStringLiteral("Create partition"), false, du_create_panel_);
        connect(b_apply, &QAbstractButton::clicked, this, &RecoveryWindow::createNewPartition);
        cp_row->addWidget(b_fs);
        cp_row->addWidget(b_apply);
        cp_row->addStretch(1);

        cp->addWidget(du_new_size_lbl_);
        cp->addWidget(du_new_size_slider_);
        cp->addLayout(cp_row);
        du_create_panel_->setVisible(false);
        col->addWidget(du_create_panel_);

        du_status_ = new QLabel(host);
        du_status_->setFont(rec_font(12, QFont::Normal));
        du_status_->setWordWrap(true);
        col->addWidget(du_status_);
        col->addStretch(1);

        refreshDiskUtility();
        return make_oobe_split_page(QStringLiteral("hard-drive"),
                                    QStringLiteral("Disks"),
                                    QStringLiteral("Create, delete, or format partitions."),
                                    host);
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
        if (du_selected_disk_ >= static_cast<int>(disks_.size())) du_selected_disk_ = 0;
        du_bar_->setDisk(disks_[static_cast<std::size_t>(du_selected_disk_)]);

        for (int di = 0; di < static_cast<int>(disks_.size()); ++di) {
            const auto& d = disks_[static_cast<std::size_t>(di)];
            for (const auto& part : d.partitions) {
                const int idx = static_cast<int>(du_parts_.size());
                du_parts_.push_back(part);
                const QString lbl = QString::fromStdString(
                    part.path + (part.fstype.empty() ? "" : "  " + part.fstype));
                const QString meta = QString::fromStdString(format_bytes(part.size_bytes));
                auto* row = new OobeListRow(lbl, meta, du_list_host_);
                row->setChecked(idx == du_selected_part_);
                connect(row, &QAbstractButton::clicked, this, [this, idx, di]() {
                    du_selected_part_ = idx;
                    du_selected_disk_ = di;
                    du_bar_->setDisk(disks_[static_cast<std::size_t>(di)]);
                    for (int j = 0; j < static_cast<int>(du_part_rows_.size()); ++j) {
                        du_part_rows_[static_cast<std::size_t>(j)]->setChecked(j == idx);
                    }
                });
                du_part_rows_.push_back(row);
                du_list_lay_->addWidget(row);
            }
        }
        if (du_parts_.empty()) {
            // Disk exists but has no partitions yet (unpartitioned/blank drive)
            const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
            auto* row = new OobeListRow(
                QString::fromStdString(d.path + "  (unpartitioned)"),
                QString::fromStdString(format_bytes(d.size_bytes)),
                du_list_host_);
            row->setChecked(true);
            du_part_rows_.push_back(row);
            du_list_lay_->addWidget(row);
        }
    }

    void runDiskCommand(const QString& program, const QStringList& args, const QString& ok_msg) {
        if (QStandardPaths::findExecutable(program).isEmpty()) {
            du_status_->setText(QStringLiteral("%1 is not available on this host.").arg(program));
            return;
        }
        du_status_->setText(QStringLiteral("Running %1...").arg(program));
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc, ok_msg](int code, QProcess::ExitStatus st) {
                    const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        QProcess::execute(QStringLiteral("partprobe"), {});
                        disks_ = probe_system_disks();
                        detected_oses_ = detect_oses(disks_);
                        refreshDiskUtility();
                        du_status_->setText(ok_msg);
                    } else {
                        du_status_->setText(err.isEmpty()
                            ? QStringLiteral("Operation failed (exit %1).").arg(code)
                            : err.section(QLatin1Char('\n'), -1));
                    }
                });
        proc->start(program, args);
    }

    void createNewPartition() {
        if (disks_.empty()) {
            du_status_->setText(QStringLiteral("No target disk available."));
            return;
        }
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        const QString disk_path = QString::fromStdString(d.path);
        const QString type_guid = (du_new_fs_ == QLatin1String("fat32"))
            ? QStringLiteral("0700")
            : QStringLiteral("8300");
        const QString size_arg = QStringLiteral("0:0:+%1G").arg(du_new_size_gib_);
        du_create_panel_->setVisible(false);
        runDiskCommand(QStringLiteral("sgdisk"),
                       {QStringLiteral("-n"), size_arg, QStringLiteral("-t"),
                        QStringLiteral("0:") + type_guid, disk_path},
                       QStringLiteral("Created %1 GiB partition on %2.").arg(du_new_size_gib_).arg(disk_path));
    }

    void deleteSelectedPartition() {
        if (du_parts_.empty() || du_selected_part_ >= static_cast<int>(du_parts_.size())) {
            du_status_->setText(QStringLiteral("Select a partition to delete."));
            return;
        }
        const auto& part = du_parts_[static_cast<std::size_t>(du_selected_part_)];
        if (part.part_number <= 0 || part.parent_disk.empty()) {
            du_status_->setText(QStringLiteral("Cannot determine partition number."));
            return;
        }
        runDiskCommand(QStringLiteral("parted"),
                       {QStringLiteral("-s"), QString::fromStdString(part.parent_disk),
                        QStringLiteral("rm"), QString::number(part.part_number)},
                       QStringLiteral("Deleted %1.").arg(QString::fromStdString(part.path)));
    }

    void wipeDiskGpt() {
        if (disks_.empty()) return;
        const auto& d = disks_[static_cast<std::size_t>(du_selected_disk_)];
        const QString disk_path = QString::fromStdString(d.path);
        runDiskCommand(QStringLiteral("sgdisk"),
                       {QStringLiteral("--zap-all"), disk_path},
                       QStringLiteral("Created clean GPT table on %1.").arg(disk_path));
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
        QStringList args;
        for (const auto& a : plan.args) args << QString::fromStdString(a);
        runDiskCommand(QString::fromStdString(plan.program), args, QString::fromStdString(plan.summary));
    }

    // ── Page 6: Bootloader Repair ────────────────────────────────────────────
    QWidget* buildBootRepairPage() {
        auto* host = new QWidget();
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);

        const QStringList opts = {
            QStringLiteral("Reinstall UEFI bootloader"),
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
        col->addStretch(1);

        return make_oobe_split_page(QStringLiteral("shield"),
                                    QStringLiteral("Repair startup"),
                                    QStringLiteral("Press > to run."),
                                    host);
    }

    void runBootRepair() {
        const QString bootctl = QStandardPaths::findExecutable(QStringLiteral("bootctl"));
        if (bootctl.isEmpty()) {
            boot_status_->setText(QStringLiteral("bootctl is not available on this host."));
            return;
        }
        QStringList args = {QStringLiteral("status")};
        if (boot_choice_ == 0) args = {QStringLiteral("install")};
        else if (boot_choice_ == 1) args = {QStringLiteral("update")};
        boot_status_->setText(QStringLiteral("Running bootctl..."));
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus st) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    proc->deleteLater();
                    if (st == QProcess::NormalExit && code == 0) {
                        boot_status_->setText(out.isEmpty() ? QStringLiteral("Startup repair complete.")
                                                            : out.section(QLatin1Char('\n'), 0, 0));
                    } else {
                        boot_status_->setText(err.isEmpty() ? QStringLiteral("Repair failed (exit %1).").arg(code)
                                                            : err.section(QLatin1Char('\n'), -1));
                    }
                });
        proc->start(bootctl, args);
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
        col->addStretch(1);

        refreshLogs();
        return make_oobe_split_page(QStringLiteral("terminal-square"),
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
    std::string selected_partition_path_;
    int resize_gib_ = 40;
    QWidget* target_list_host_ = nullptr;
    QVBoxLayout* target_list_lay_ = nullptr;
    std::vector<OobeListRow*> target_rows_;
    std::vector<std::string> target_part_paths_;
    SlimPartitionBar* target_bar_ = nullptr;
    QWidget* resize_host_ = nullptr;
    QLabel* resize_label_ = nullptr;
    QSlider* resize_slider_ = nullptr;
    QLabel* target_sub_lbl_ = nullptr;

    InstallPlan current_plan_;
    QLabel* review_label_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;

    bool du_return_to_target_ = false;
    int du_selected_disk_ = 0;
    int du_selected_part_ = 0;
    int du_new_size_gib_ = 32;
    QString du_new_fs_ = QStringLiteral("ext4");
    QWidget* du_list_host_ = nullptr;
    QVBoxLayout* du_list_lay_ = nullptr;
    std::vector<OobeListRow*> du_part_rows_;
    std::vector<PartitionInfo> du_parts_;
    SlimPartitionBar* du_bar_ = nullptr;
    QWidget* du_create_panel_ = nullptr;
    QLabel* du_new_size_lbl_ = nullptr;
    QSlider* du_new_size_slider_ = nullptr;
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
    if (!initial_page.isEmpty()) w.openPage(initial_page);
    const QString plat = QGuiApplication::platformName();
    if (screenshot_path.isEmpty() &&
        (plat == QLatin1String("eglfs") || plat == QLatin1String("linuxfb"))) {
        w.showFullScreen();
    } else {
        w.resize(1280, 832);
        w.show();
    }

    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(250, &w, [&w, screenshot_path]() {
            w.grab().save(screenshot_path);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}

#include "main.moc"
