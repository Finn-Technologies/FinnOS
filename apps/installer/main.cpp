// FinnOS Bare-Metal Recovery & GUI Installer (finn-installer).
// A full-screen FinnOS surface: OOBE's centred brand lockup and quiet space,
// one hairline card, and no borrowed operating system's visual identity.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "system/sys_files.hpp"

#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

using namespace finn::ui;

class ThemeIcon final : public QWidget {
    Q_OBJECT

public:
    ThemeIcon(QString icon_name, int box_size, int icon_size, QWidget* parent = nullptr)
        : QWidget(parent),
          icon_name_(std::move(icon_name)),
          box_size_(box_size),
          icon_size_(icon_size) {
        setFixedSize(box_size_, box_size_);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF ir((width() - icon_size_) / 2.0,
                        (height() - icon_size_) / 2.0,
                        icon_size_, icon_size_);
        FinnThemeManager::instance().paintIcon(&p, ir, icon_name_, t.ink);
    }

private:
    QString icon_name_;
    int box_size_;
    int icon_size_;
};

class NeutralIconBadge final : public QWidget {
    Q_OBJECT

public:
    NeutralIconBadge(QString icon_name, int box_size, int icon_size, qreal radius,
                     QWidget* parent = nullptr)
        : QWidget(parent),
          icon_name_(std::move(icon_name)),
          box_size_(box_size),
          icon_size_(icon_size),
          radius_(radius) {
        setFixedSize(box_size_, box_size_);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor bg = t.dark ? t.tile : t.surface_canvas;
        p.setBrush(bg);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(r, radius_, radius_);

        const QRectF ir((width() - icon_size_) / 2.0,
                        (height() - icon_size_) / 2.0,
                        icon_size_, icon_size_);
        FinnThemeManager::instance().paintIcon(&p, ir, icon_name_, t.ink);
    }

private:
    QString icon_name_;
    int box_size_;
    int icon_size_;
    qreal radius_;
};

// OOBE's brand lockup: the four-diamond mark, the wordmark, and a quiet
// suffix naming the context. Used in the top bar and in both main pages.
QWidget* make_brand_lockup(const QString& suffix, QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(new ThemeIcon(QStringLiteral("boot-mark"), 20, 20, box));
    row->addWidget(new FinnLabel(QStringLiteral("FinnOS"),
                                 FinnLabel::Role::Title,
                                 FinnLabel::Tone::Ink, box));
    if (!suffix.isEmpty()) {
        auto* context = new FinnLabel(suffix, FinnLabel::Role::Caption,
                                      FinnLabel::Tone::Mute, box);
        context->setContentsMargins(2, 0, 0, 0);
        row->addWidget(context);
    }
    return box;
}

// Four quiet segments, one in the accent: the wizard's place in the sequence,
// borrowed from OOBE's fixed step navigation without the moving circles.
class StepRail final : public QWidget {
    Q_OBJECT

public:
    explicit StepRail(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(160, 8);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setStep(int step, int total) {
        step_ = step;
        total_ = std::max(1, total);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        constexpr int kSeg = 28;
        constexpr int kGap = 8;
        const int x0 = (width() - (total_ * kSeg + (total_ - 1) * kGap)) / 2;
        for (int i = 0; i < total_; ++i) {
            const QRectF seg(x0 + i * (kSeg + kGap), (height() - 3) / 2.0, kSeg, 3);
            p.setBrush(i == step_ ? t.accent : t.track);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(seg, 1.5, 1.5);
        }
    }

private:
    int step_ = 0;
    int total_ = 4;
};

QWidget* wrap_log_view(QPlainTextEdit* edit, QWidget* parent) {
    auto* box = new FinnSurface(FinnSurface::Role::Tile, kRadiusLg, true, parent);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(10, 8, 10, 8);
    edit->setParent(box);
    edit->setReadOnly(true);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setFont(finn_mono_font(9.5, QFont::Normal));
    edit->viewport()->setAutoFillBackground(false);
    auto sync_palette = [edit]() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = edit->palette();
        pal.setColor(QPalette::Base, Qt::transparent);
        pal.setColor(QPalette::Window, Qt::transparent);
        pal.setColor(QPalette::Text, t.ink);
        pal.setColor(QPalette::PlaceholderText, t.mute);
        edit->setPalette(pal);
    };
    sync_palette();
    QObject::connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                     edit, sync_palette);
    lay->addWidget(edit);
    return box;
}

QString read_cpu_summary() {
    QString raw_model;
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines =
            QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("model name")) ||
                line.startsWith(QStringLiteral("Model")) ||
                line.startsWith(QStringLiteral("Hardware"))) {
                const QString val = line.section(QLatin1Char(':'), 1).trimmed();
                if (!val.isEmpty()) {
                    raw_model = val;
                    break;
                }
            }
        }
    }
    if (!raw_model.isEmpty()) return raw_model;

    QProcess p;
    p.start(QStringLiteral("lscpu"), {});
    if (p.waitForFinished(1200)) {
        QString vendor_id;
        QString arch;
        const QStringList lines =
            QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.startsWith(QStringLiteral("Model name:"))) {
                const QString val = line.section(QLatin1Char(':'), 1).trimmed();
                if (!val.isEmpty() && val != QLatin1String("-")) return val;
            } else if (line.startsWith(QStringLiteral("Vendor ID:"))) {
                const QString val = line.section(QLatin1Char(':'), 1).trimmed();
                if (!val.isEmpty() && val != QLatin1String("-")) vendor_id = val;
            } else if (line.startsWith(QStringLiteral("Architecture:"))) {
                arch = line.section(QLatin1Char(':'), 1).trimmed();
            }
        }
        if (!vendor_id.isEmpty()) {
            return QStringLiteral("%1 %2").arg(
                vendor_id, !arch.isEmpty() ? arch : QSysInfo::currentCpuArchitecture());
        }
    }
    return QSysInfo::currentCpuArchitecture();
}

QStringList available_timezones() {
    QStringList zones;
    const QString root = QStringLiteral("/usr/share/zoneinfo");
    if (QFileInfo::exists(root)) {
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString full = it.next();
            const QString rel = QDir(root).relativeFilePath(full);
            if (rel.startsWith(QLatin1String("posix/")) ||
                rel.startsWith(QLatin1String("right/")) ||
                rel.startsWith(QLatin1String("Etc/")) ||
                rel.startsWith(QLatin1String("SystemV/"))) {
                continue;
            }
            if (rel.endsWith(QLatin1String(".tab")) ||
                rel.endsWith(QLatin1String(".list")) ||
                rel == QLatin1String("leapseconds") ||
                rel == QLatin1String("tzdata.zi") ||
                rel == QLatin1String("posixrules") ||
                rel == QLatin1String("localtime") ||
                rel == QLatin1String("SECURITY")) {
                continue;
            }
            if (rel.contains(QLatin1Char('/')) || rel == QLatin1String("UTC")) {
                zones.push_back(rel);
            }
        }
    }
    if (zones.isEmpty()) {
        return {
            QStringLiteral("UTC"),
            QStringLiteral("America/New_York"),
            QStringLiteral("America/Chicago"),
            QStringLiteral("America/Los_Angeles"),
            QStringLiteral("Europe/London"),
            QStringLiteral("Europe/Berlin"),
            QStringLiteral("Asia/Tokyo"),
        };
    }
    zones.removeDuplicates();
    zones.sort(Qt::CaseInsensitive);
    zones.removeAll(QStringLiteral("UTC"));
    zones.prepend(QStringLiteral("UTC"));
    return zones;
}

QString current_timezone_or_utc() {
    const QFileInfo lt(QStringLiteral("/etc/localtime"));
    if (lt.isSymLink()) {
        const QString target = lt.symLinkTarget();
        const QString marker = QStringLiteral("/usr/share/zoneinfo/");
        const int idx = target.indexOf(marker);
        if (idx >= 0) {
            const QString rel = target.mid(idx + marker.size());
            if (!rel.isEmpty()) return rel;
        }
    }
    return QStringLiteral("UTC");
}

struct DiskInfo {
    QString device;
    QString size;
    QString model;
};

QVector<DiskInfo> probe_disks() {
    QVector<DiskInfo> disks;
    QProcess p;
    p.start(QStringLiteral("lsblk"),
            {QStringLiteral("-J"), QStringLiteral("-d"),
             QStringLiteral("-o"), QStringLiteral("NAME,SIZE,MODEL,TYPE")});
    if (!p.waitForFinished(2500)) return disks;
    const auto doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    const auto arr = doc.object().value(QStringLiteral("blockdevices")).toArray();
    for (const auto& v : arr) {
        const auto obj = v.toObject();
        if (obj.value(QStringLiteral("type")).toString() != QLatin1String("disk")) continue;
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (name.startsWith(QLatin1String("loop")) || name.startsWith(QLatin1String("zram"))) {
            continue;
        }
        disks.push_back(DiskInfo{
            QStringLiteral("/dev/") + name,
            obj.value(QStringLiteral("size")).toString(),
            obj.value(QStringLiteral("model")).toString().trimmed(),
        });
    }
    return disks;
}

class InstallerWindow final : public FinnLayerWindow {
    Q_OBJECT

public:
    InstallerWindow()
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-installer")) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // A quiet hairline bar: brand on the left, the two ways out on the
        // right. No borrowed operating system's translucent menu bar.
        auto* menu_bar = new QWidget(this);
        menu_bar->setFixedHeight(44);
        auto* mb_lay = new QHBoxLayout(menu_bar);
        mb_lay->setContentsMargins(24, 0, 24, 0);
        mb_lay->setSpacing(12);
        mb_lay->addWidget(make_brand_lockup(QStringLiteral("Recovery"), menu_bar));
        mb_lay->addStretch(1);

        auto* restart_btn = new FinnButton(QStringLiteral("Restart"),
                                           FinnButton::Variant::Ghost, {}, menu_bar);
        restart_btn->setAccessibleName(QStringLiteral("Restart"));
        restart_btn->setFixedHeight(28);
        connect(restart_btn, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });

        auto* exit_btn = new FinnButton(QStringLiteral("Exit"),
                                        FinnButton::Variant::Ghost, {}, menu_bar);
        exit_btn->setAccessibleName(QStringLiteral("Exit"));
        exit_btn->setFixedHeight(28);
        connect(exit_btn, &QAbstractButton::clicked, this, [this]() { close(); });

        mb_lay->addWidget(restart_btn);
        mb_lay->addWidget(exit_btn);
        root->addWidget(menu_bar);
        root->addWidget(new FinnDivider(Qt::Horizontal, this));

        // Centered Recovery Content Stage
        auto* stage = new QWidget(this);
        auto* stage_lay = new QVBoxLayout(stage);
        stage_lay->setAlignment(Qt::AlignCenter);
        stage_lay->setContentsMargins(32, 24, 32, 24);

        card_ = new FinnGlassPanel(20, stage);
        card_->setOpacityAlpha(242);
        card_->setFixedSize(760, 600);

        auto* card_lay = new QVBoxLayout(card_);
        card_lay->setContentsMargins(36, 28, 36, 24);
        card_lay->setSpacing(14);

        stack_ = new QStackedWidget(card_);
        finn::ui::install_stack_animator(stack_);
        stack_->addWidget(buildRecoveryHubPage());   // 0: recovery utilities
        stack_->addWidget(buildInstallWizardPage()); // 1: install wizard
        stack_->addWidget(buildDiskUtilityPage());   // 2: Disk Utility
        stack_->addWidget(buildSystemInfoPage());    // 3: Hardware & Logs
        card_lay->addWidget(stack_, 1);

        stage_lay->addWidget(card_, 0, Qt::AlignCenter);
        root->addWidget(stage, 1);

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void openPage(const QString& page_name) {
        const QString p = page_name.trimmed().toLower();
        if (p == QLatin1String("install")) {
            setWizardStep(0);
            stack_->setCurrentIndex(1);
        } else if (p == QLatin1String("disk")) {
            refreshDiskUtility();
            stack_->setCurrentIndex(2);
        } else if (p == QLatin1String("info")) {
            stack_->setCurrentIndex(3);
        } else {
            stack_->setCurrentIndex(0);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        // The same canvas OOBE uses: pure black in dark, warm bone in light.
        // One neutral surface — no gradient, no borrowed dusk backdrop.
        p.fillRect(rect(), t.surface_canvas);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            if (install_running_) return;
            if (stack_->currentIndex() != 0) {
                stack_->setCurrentIndex(0);
            } else {
                close();
            }
            return;
        }
        FinnLayerWindow::keyPressEvent(event);
    }

private:
    QWidget* buildRecoveryHubPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(24, 16, 24, 16);
        col->setSpacing(12);

        auto* lockup = make_brand_lockup(QString(), page);
        col->addWidget(lockup, 0, Qt::AlignHCenter);

        auto* hdr = new FinnLabel(QStringLiteral("Recover or install FinnOS"),
                                  FinnLabel::Role::TitleLarge,
                                  FinnLabel::Tone::Ink, page);
        hdr->setAlignment(Qt::AlignCenter);
        col->addWidget(hdr);

        auto* sub = new FinnLabel(
            QStringLiteral("Install FinnOS, inspect disks, or open a terminal."),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, page);
        sub->setAlignment(Qt::AlignCenter);
        col->addWidget(sub);
        col->addSpacing(12);

        // One chevron per row, not a repeated "Open" button: the whole row is
        // the control, and the OOBE-style circle is its only affordance.
        auto make_util_row = [&](const QString& icon, const QString& title,
                                 const QString& desc, std::function<void()> on_click) {
            auto* c = new FinnCard(kRadiusLg, true, page);
            c->setFixedHeight(72);
            auto* rl = new QHBoxLayout(c);
            rl->setContentsMargins(16, 12, 16, 12);
            rl->setSpacing(16);

            rl->addWidget(new NeutralIconBadge(icon, 40, 22, 10.0, c));

            auto* tc = new QVBoxLayout();
            tc->setSpacing(2);
            tc->addWidget(new FinnLabel(title, FinnLabel::Role::Headline,
                                        FinnLabel::Tone::Ink, c));
            tc->addWidget(new FinnLabel(desc, FinnLabel::Role::Caption,
                                        FinnLabel::Tone::Dim, c));
            rl->addLayout(tc, 1);

            auto* chevron = new FinnIconButton(QStringLiteral("chevron-right"), 32, 14, c);
            chevron->setCornerRadius(16.0);
            chevron->setEnabled(false);
            rl->addWidget(chevron);

            connect(c, &FinnCard::clicked, this, on_click);
            return c;
        };

        col->addWidget(make_util_row(
            QStringLiteral("installer"), QStringLiteral("Install FinnOS"),
            QStringLiteral("Install FinnOS on internal or external storage"),
            [this]() {
                setWizardStep(0);
                stack_->setCurrentIndex(1);
            }));

        col->addWidget(make_util_row(
            QStringLiteral("folder"), QStringLiteral("Disk Utility"),
            QStringLiteral("Inspect disks, partitions and filesystems"),
            [this]() {
                refreshDiskUtility();
                stack_->setCurrentIndex(2);
            }));

        col->addWidget(make_util_row(
            QStringLiteral("terminal"), QStringLiteral("Terminal"),
            QStringLiteral("Partition, repair and inspect the system"),
            [this]() {
                QProcess::startDetached(QString::fromUtf8(finn::kFinnTerminalBin), {});
                close();
            }));

        col->addWidget(make_util_row(
            QStringLiteral("info"), QStringLiteral("System Information"),
            QStringLiteral("Firmware, hardware and kernel logs"),
            [this]() { stack_->setCurrentIndex(3); }));

        col->addStretch(1);
        return page;
    }

    QWidget* buildInstallWizardPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);
        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);

        // OOBE's brand lockup and a quiet step rail instead of a circular
        // badge: the wizard reads as one journey, not four separate screens.
        col->addWidget(make_brand_lockup(QStringLiteral("Install"), page),
                       0, Qt::AlignHCenter);

        wiz_title_ = new FinnLabel(QStringLiteral("Install FinnOS"),
                                   FinnLabel::Role::Title,
                                   FinnLabel::Tone::Ink, page);
        wiz_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(wiz_title_);

        step_rail_ = new StepRail(page);
        col->addWidget(step_rail_, 0, Qt::AlignHCenter);

        wiz_stack_ = new QStackedWidget(page);
        wiz_stack_->setAutoFillBackground(false);
        finn::ui::install_stack_animator(wiz_stack_);

        // Step 0: Overview
        auto* w0 = new QWidget();
        w0->setAutoFillBackground(false);
        auto* w0l = new QVBoxLayout(w0);
        w0l->setSpacing(10);
        const bool is_efi = QFileInfo::exists(QStringLiteral("/sys/firmware/efi"));
        auto* g = new QGridLayout();
        g->setSpacing(10);
        g->addWidget(new FinnStatCard(QStringLiteral("Firmware"),
                                      is_efi ? QStringLiteral("UEFI (64-bit)") : QStringLiteral("Legacy BIOS"), w0), 0, 0);
        g->addWidget(new FinnStatCard(QStringLiteral("Architecture"),
                                      QSysInfo::currentCpuArchitecture(), w0), 0, 1);
        g->addWidget(new FinnStatCard(QStringLiteral("Filesystem"),
                                      QStringLiteral("ext4 + FAT32 ESP"), w0), 1, 0);
        g->addWidget(new FinnStatCard(QStringLiteral("Bootloader"),
                                      is_efi ? QStringLiteral("GRUB EFI") : QStringLiteral("GRUB MBR"), w0), 1, 1);
        w0l->addLayout(g);
        w0l->addStretch(1);
        wiz_stack_->addWidget(w0);

        // Step 1: Big Sur horizontal clickable disk drive picker
        auto* w1 = new QWidget();
        w1->setAutoFillBackground(false);
        auto* w1l = new QVBoxLayout(w1);
        w1l->setSpacing(12);
        w1l->addWidget(new FinnLabel(
            QStringLiteral("Select the disk where you want to install FinnOS:"),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, w1), 0, Qt::AlignCenter);

        auto* disks_row = new QHBoxLayout();
        disks_row->setSpacing(14);
        disks_row->setAlignment(Qt::AlignCenter);
        const auto disks = probe_disks();
        if (disks.isEmpty()) {
            disks_row->addWidget(new FinnEmptyState(
                QStringLiteral("installer"), QStringLiteral("No Target Disks Found"),
                QStringLiteral("Attach a disk to /dev/nvme0n1, /dev/sda, or /dev/vda."),
                {}, w1));
        } else {
            for (int i = 0; i < disks.size(); ++i) {
                const auto& d = disks[i];
                auto* dc = new FinnCard(kRadiusLg, true, w1);
                dc->setFixedSize(168, 136);
                dc->setSelected(i == 0);
                if (i == 0) selected_disk_ = d.device;
                disk_cards_.push_back(dc);

                auto* dl = new QVBoxLayout(dc);
                dl->setAlignment(Qt::AlignCenter);
                dl->setSpacing(6);
                auto* ico = new ThemeIcon(QStringLiteral("installer"), 44, 36, dc);
                dl->addWidget(ico, 0, Qt::AlignCenter);
                dl->addWidget(new FinnLabel(d.device, FinnLabel::Role::Headline,
                                            FinnLabel::Tone::Ink, dc), 0, Qt::AlignCenter);
                dl->addWidget(new FinnLabel(d.size, FinnLabel::Role::Caption,
                                            FinnLabel::Tone::Dim, dc), 0, Qt::AlignCenter);

                connect(dc, &FinnCard::clicked, this, [this, i, dev = d.device]() {
                    selected_disk_ = dev;
                    for (int j = 0; j < disk_cards_.size(); ++j) {
                        disk_cards_[j]->setSelected(j == i);
                    }
                });
                disks_row->addWidget(dc);
            }
        }
        w1l->addLayout(disks_row);
        w1l->addStretch(1);
        wiz_stack_->addWidget(w1);

        // Step 2: Hostname & Erase confirmation
        auto* w2 = new QWidget();
        w2->setAutoFillBackground(false);
        auto* w2l = new QVBoxLayout(w2);
        w2l->setSpacing(10);
        hostname_edit_ = new FinnTextField(QStringLiteral("finnos"), w2);
        hostname_edit_->setAccessibleName(QStringLiteral("Computer Hostname"));
        hostname_edit_->setText(QStringLiteral("finnos"));
        hostname_edit_->setFixedWidth(220);
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("Computer Hostname"), QStringLiteral("Local network identifier"),
            QStringLiteral("internet"), hostname_edit_, w2));

        tz_combo_ = new QComboBox(w2);
        tz_combo_->setAccessibleName(QStringLiteral("Time Zone"));
        tz_combo_->addItems(available_timezones());
        const QString cur_tz = current_timezone_or_utc();
        const int tz_idx = tz_combo_->findText(cur_tz);
        if (tz_idx >= 0) tz_combo_->setCurrentIndex(tz_idx);
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("Time Zone"), QStringLiteral("System clock region"),
            QStringLiteral("info"), tz_combo_, w2));

        ab_slots_toggle_ = new FinnToggle(true, w2);
        ab_slots_toggle_->setAccessibleName(QStringLiteral("A/B OTA Root Partitions"));
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("A/B OTA Root Partitions"),
            QStringLiteral("Dual FINNOS_A/FINNOS_B system slots with rollback (requires >= 14 GiB)"),
            QStringLiteral("update"), ab_slots_toggle_, w2));

        erase_confirm_ = new FinnToggle(false, w2);
        erase_confirm_->setAccessibleName(QStringLiteral("Confirm Erase Target Disk"));
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("Confirm Erase Target Disk"),
            QStringLiteral("Required before formatting and installing FinnOS"),
            QStringLiteral("shield"), erase_confirm_, w2));
        w2l->addStretch(1);
        wiz_stack_->addWidget(w2);

        // Step 3: Live Progress
        auto* w3 = new QWidget();
        w3->setAutoFillBackground(false);
        auto* w3l = new QVBoxLayout(w3);
        w3l->setSpacing(8);
        install_log_ = new QPlainTextEdit();
        install_log_->setReadOnly(true);
        install_log_->setPlaceholderText(QStringLiteral("Installer output"));
        w3l->addWidget(wrap_log_view(install_log_, w3), 1);
        wiz_stack_->addWidget(w3);

        col->addWidget(wiz_stack_, 1);
        col->addWidget(new FinnDivider(Qt::Horizontal, page));

        auto* nav = new QHBoxLayout();
        hub_btn_ = new FinnButton(QStringLiteral("Utilities"),
                                  FinnButton::Variant::Ghost, {}, page);
        hub_btn_->setAccessibleName(QStringLiteral("Utilities"));
        connect(hub_btn_, &QAbstractButton::clicked, this, [this]() {
            if (!install_running_) stack_->setCurrentIndex(0);
        });
        wiz_back_ = new FinnButton(QStringLiteral("Back"), FinnButton::Variant::Secondary,
                                   QStringLiteral("chevron-left"), page);
        wiz_back_->setAccessibleName(QStringLiteral("Back"));
        auto advance = [this]() {
            if (install_running_) return;
            const int cur = wiz_stack_->currentIndex();
            if (cur < 3) {
                setWizardStep(cur + 1);
            } else {
                startInstall();
            }
        };
        // OOBE's circular next control, with a quiet label so the installer
        // stays explicit about what the arrow will do.
        wiz_next_ = new FinnIconButton(QStringLiteral("chevron-right"), 48, 20, page);
        wiz_next_->setAccessibleName(QStringLiteral("Continue or Install"));
        wiz_next_->setCornerRadius(24.0);
        connect(wiz_back_, &QAbstractButton::clicked, this, [this]() {
            if (!install_running_ && wiz_stack_->currentIndex() > 0) {
                setWizardStep(wiz_stack_->currentIndex() - 1);
            }
        });
        connect(wiz_next_, &QAbstractButton::clicked, this, advance);
        wiz_next_label_ = new FinnLabel(QStringLiteral("Continue"),
                                        FinnLabel::Role::Caption,
                                        FinnLabel::Tone::Dim, page);
        nav->addWidget(hub_btn_);
        nav->addWidget(wiz_back_);
        nav->addStretch(1);
        nav->addWidget(wiz_next_label_);
        nav->addWidget(wiz_next_);
        col->addLayout(nav);
        return page;
    }

    QWidget* buildDiskUtilityPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);
        auto* col = new QVBoxLayout(page);
        col->setSpacing(12);
        col->addWidget(make_brand_lockup(QStringLiteral("Disks"), page),
                       0, Qt::AlignHCenter);
        auto* title = new FinnLabel(QStringLiteral("Disk Utility"),
                                    FinnLabel::Role::Title,
                                    FinnLabel::Tone::Ink, page);
        title->setAlignment(Qt::AlignCenter);
        col->addWidget(title);
        col->addWidget(new FinnLabel(
            QStringLiteral("Read-only view of the block devices on this machine"),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, page), 0, Qt::AlignHCenter);
        disk_util_log_ = new QPlainTextEdit();
        disk_util_log_->setReadOnly(true);
        col->addWidget(wrap_log_view(disk_util_log_, page), 1);

        auto* actions = new QHBoxLayout();
        actions->setSpacing(10);
        auto* back = new FinnButton(QStringLiteral("Back to Utilities"),
                                    FinnButton::Variant::Secondary,
                                    QStringLiteral("chevron-left"), page);
        connect(back, &QAbstractButton::clicked, this, [this]() { stack_->setCurrentIndex(0); });
        auto* refresh = new FinnButton(QStringLiteral("Refresh"),
                                       FinnButton::Variant::Ghost,
                                       QStringLiteral("update"), page);
        connect(refresh, &QAbstractButton::clicked, this, [this]() { refreshDiskUtility(); });
        actions->addWidget(back);
        actions->addWidget(refresh);
        actions->addStretch(1);
        col->addLayout(actions);
        return page;
    }

    void refreshDiskUtility() {
        QProcess p;
        p.start(QStringLiteral("lsblk"), {QStringLiteral("-o"), QStringLiteral("NAME,SIZE,FSTYPE,MOUNTPOINT,MODEL")});
        if (!p.waitForFinished(2000)) {
            disk_util_log_->setPlainText(QStringLiteral("Timed out querying block devices (lsblk)."));
            return;
        }
        const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        disk_util_log_->setPlainText(out.isEmpty() ? QStringLiteral("No block devices reported.") : out);
    }

    QWidget* buildSystemInfoPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);
        auto* col = new QVBoxLayout(page);
        col->setSpacing(12);
        col->addWidget(make_brand_lockup(QStringLiteral("System"), page),
                       0, Qt::AlignHCenter);
        auto* title = new FinnLabel(QStringLiteral("System Information"),
                                    FinnLabel::Role::Title,
                                    FinnLabel::Tone::Ink, page);
        title->setAlignment(Qt::AlignCenter);
        col->addWidget(title);
        col->addWidget(new FinnLabel(
            QStringLiteral("Firmware, hardware and kernel logs"),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, page), 0, Qt::AlignHCenter);
        const auto mem = finn::sys::read_meminfo();
        const double ram_gb = mem.total_kib / (1024.0 * 1024.0);
        const int cores = qMax(1, QThread::idealThreadCount());
        auto* log = new QPlainTextEdit();
        log->setReadOnly(true);
        log->setPlainText(
            QStringLiteral("Architecture: %1\nProcessor: %2 (%3 logical cores)\nMemory: %4 GB\nKernel: %5\nUEFI Firmware: %6\n")
                .arg(QSysInfo::currentCpuArchitecture(),
                     read_cpu_summary(),
                     QString::number(cores),
                     ram_gb > 0.0 ? QString::number(ram_gb, 'f', 1) : QStringLiteral("Unknown"),
                     QSysInfo::kernelVersion(),
                     QFileInfo::exists(QStringLiteral("/sys/firmware/efi")) ? QStringLiteral("Yes") : QStringLiteral("No")));
        col->addWidget(wrap_log_view(log, page), 1);
        auto* back = new FinnButton(QStringLiteral("Back to Utilities"),
                                    FinnButton::Variant::Secondary,
                                    QStringLiteral("chevron-left"), page);
        connect(back, &QAbstractButton::clicked, this, [this]() { stack_->setCurrentIndex(0); });
        col->addWidget(back, 0, Qt::AlignLeft);
        return page;
    }

    void setWizardStep(int idx) {
        wiz_stack_->setCurrentIndex(idx);
        wiz_back_->setEnabled(idx > 0);
        static const char* kTitles[] = {
            "Install FinnOS",
            "Select Target Disk",
            "Configure Installation",
            "Install FinnOS",
        };
        wiz_title_->setText(QString::fromUtf8(kTitles[idx]));
        step_rail_->setStep(idx, 4);
        const QString action = idx == 3 ? QStringLiteral("Install") : QStringLiteral("Continue");
        wiz_next_label_->setText(action);
        wiz_next_->setToolTip(action);
    }

    void setInstallRunning(bool running) {
        install_running_ = running;
        if (hub_btn_) hub_btn_->setEnabled(!running);
        if (wiz_back_) wiz_back_->setEnabled(!running && wiz_stack_->currentIndex() > 0);
        if (wiz_next_) wiz_next_->setEnabled(!running);
        if (wiz_next_label_) {
            const QString action = running
                ? QStringLiteral("Installing...")
                : (wiz_stack_->currentIndex() == 3 ? QStringLiteral("Install") : QStringLiteral("Continue"));
            wiz_next_label_->setText(action);
            if (wiz_next_) wiz_next_->setToolTip(action);
        }
    }

    void startInstall() {
        if (install_running_) return;
        if (selected_disk_.isEmpty() || !erase_confirm_->isChecked()) {
            install_log_->setPlainText(QStringLiteral(
                "Select a target disk and enable Confirm Erase Target Disk in Configure Installation."));
            return;
        }
        QString hostname = hostname_edit_->text().trimmed();
        if (hostname.isEmpty()) hostname = QStringLiteral("finnos");
        static const QRegularExpression kHostnameRe(
            QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
        if (!kHostnameRe.match(hostname).hasMatch()) {
            install_log_->setPlainText(QStringLiteral(
                "Invalid hostname '%1'. Use 1-63 ASCII letters, digits, or hyphens.").arg(hostname));
            return;
        }
        const QString tz = tz_combo_->currentText().trimmed().isEmpty()
            ? QStringLiteral("UTC")
            : tz_combo_->currentText().trimmed();
        const QString installer = QString::fromUtf8(finn::kFinnInstallBin);
        if (!QFileInfo::exists(installer)) {
            install_log_->setPlainText(QStringLiteral(
                "Install medium backend (%1) is only active on the live ISO.\n"
                "Verified target: %2 (host=%3, tz=%4, ab_slots=%5)")
                                           .arg(installer, selected_disk_, hostname, tz,
                                                (ab_slots_toggle_ && ab_slots_toggle_->isChecked())
                                                    ? QStringLiteral("yes")
                                                    : QStringLiteral("no")));
            return;
        }

        install_log_->clear();
        QStringList args = {
            installer,
            QStringLiteral("--disk"),
            selected_disk_,
            QStringLiteral("--hostname"),
            hostname,
            QStringLiteral("--timezone"),
            tz,
        };
        if (ab_slots_toggle_ && ab_slots_toggle_->isChecked()) {
            args << QStringLiteral("--ab-slots");
        }
        args << QStringLiteral("--yes");

        setInstallRunning(true);
        install_log_->appendPlainText(QStringLiteral("Starting installer on %1...").arg(selected_disk_));

        auto* proc = new QProcess(this);
        active_proc_ = proc;
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            const QString chunk = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            if (!chunk.isEmpty()) install_log_->appendPlainText(chunk);
        });
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
            const QString chunk = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (!chunk.isEmpty()) install_log_->appendPlainText(QStringLiteral("[stderr] ") + chunk);
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
            install_log_->appendPlainText(
                QStringLiteral("Failed to run installer: %1").arg(proc->errorString()));
            setInstallRunning(false);
            proc->deleteLater();
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, proc](int exit_code, QProcess::ExitStatus exit_status) {
                    const QString rem_out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    if (!rem_out.isEmpty()) install_log_->appendPlainText(rem_out);
                    const QString rem_err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    if (!rem_err.isEmpty()) install_log_->appendPlainText(QStringLiteral("[stderr] ") + rem_err);

                    if (exit_status == QProcess::NormalExit && exit_code == 0) {
                        install_log_->appendPlainText(
                            QStringLiteral("\nInstallation completed successfully. You may restart into FinnOS."));
                    } else if (exit_status == QProcess::CrashExit) {
                        install_log_->appendPlainText(
                            QStringLiteral("\nInstallation failed: installer process terminated abnormally."));
                    } else {
                        install_log_->appendPlainText(
                            QStringLiteral("\nInstallation failed with exit code %1.").arg(exit_code));
                    }
                    setInstallRunning(false);
                    proc->deleteLater();
                });
        proc->start(QStringLiteral("pkexec"), args);
    }

    FinnGlassPanel* card_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    FinnLabel* wiz_title_ = nullptr;
    QStackedWidget* wiz_stack_ = nullptr;
    StepRail* step_rail_ = nullptr;
    FinnButton* hub_btn_ = nullptr;
    FinnButton* wiz_back_ = nullptr;
    FinnIconButton* wiz_next_ = nullptr;
    FinnLabel* wiz_next_label_ = nullptr;
    QVector<FinnCard*> disk_cards_;
    QString selected_disk_;
    FinnTextField* hostname_edit_ = nullptr;
    QComboBox* tz_combo_ = nullptr;
    FinnToggle* ab_slots_toggle_ = nullptr;
    FinnToggle* erase_confirm_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;
    QPlainTextEdit* disk_util_log_ = nullptr;
    QPointer<QProcess> active_proc_;
    bool install_running_ = false;
};

} // namespace

int main(int argc, char** argv) {
    QString initial_page;
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromUtf8(argv[i]) == QLatin1String("--page")) {
            initial_page = QString::fromUtf8(argv[++i]);
        }
    }
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("installer"));
    InstallerWindow w;
    if (!initial_page.isEmpty()) w.openPage(initial_page);
    w.show();
    return app.exec();
}

#include "main.moc"
