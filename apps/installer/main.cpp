// FinnOS Bare-Metal Recovery & GUI Installer (finn-installer).
// Full-screen macOS Big Sur Recovery Mode-inspired experience built with FinnUI.

#include <FinnSDK/FinnSDK.hpp>

#include "common/paths.hpp"
#include "system/sys_files.hpp"

#include <QComboBox>
#include <QDir>
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
#include <QProcess>
#include <QStackedWidget>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>
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

        // macOS Big Sur Recovery-style translucent top menu bar
        auto* menu_bar = new FinnGlassPanel(0, this);
        menu_bar->setFixedHeight(34);
        auto* mb_lay = new QHBoxLayout(menu_bar);
        mb_lay->setContentsMargins(16, 0, 16, 0);
        mb_lay->setSpacing(12);

        auto* brand_ico = new ThemeIcon(QStringLiteral("installer"), 16, 16, menu_bar);
        mb_lay->addWidget(brand_ico);

        auto* title = new FinnLabel(QStringLiteral("FinnOS Recovery"),
                                    FinnLabel::Role::Headline, FinnLabel::Tone::Ink, menu_bar);
        mb_lay->addWidget(title);

        auto* util_btn = new FinnButton(QStringLiteral("Utilities"),
                                        FinnButton::Variant::Ghost, {}, menu_bar);
        util_btn->setFixedHeight(26);
        connect(util_btn, &QAbstractButton::clicked, this, [this]() {
            stack_->setCurrentIndex(0);
        });
        mb_lay->addWidget(util_btn);
        mb_lay->addStretch(1);

        auto* restart_btn = new FinnButton(QStringLiteral("Restart"),
                                           FinnButton::Variant::Ghost, {}, menu_bar);
        restart_btn->setFixedHeight(26);
        connect(restart_btn, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        });

        auto* exit_btn = new FinnButton(QStringLiteral("Exit Recovery"),
                                        FinnButton::Variant::Ghost, {}, menu_bar);
        exit_btn->setFixedHeight(26);
        connect(exit_btn, &QAbstractButton::clicked, this, [this]() { close(); });

        mb_lay->addWidget(restart_btn);
        mb_lay->addWidget(exit_btn);
        root->addWidget(menu_bar);

        // Centered Recovery Content Stage
        auto* stage = new QWidget(this);
        auto* stage_lay = new QVBoxLayout(stage);
        stage_lay->setAlignment(Qt::AlignCenter);
        stage_lay->setContentsMargins(32, 24, 32, 24);

        card_ = new FinnGlassPanel(18, stage);
        card_->setOpacityAlpha(242);
        card_->setFixedSize(760, 520);

        auto* card_lay = new QVBoxLayout(card_);
        card_lay->setContentsMargins(36, 28, 36, 24);
        card_lay->setSpacing(14);

        stack_ = new QStackedWidget(card_);
        finn::ui::install_stack_animator(stack_);
        stack_->addWidget(buildRecoveryHubPage());   // 0: macOS Big Sur Recovery Utilities
        stack_->addWidget(buildInstallWizardPage()); // 1: Big Sur Install FinnOS Wizard
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
        QLinearGradient g(0, 0, 0, height());
        if (t.dark) {
            // Classic Big Sur Recovery deep charcoal-blue gradient backdrop
            g.setColorAt(0.0, QColor(0x16, 0x19, 0x22));
            g.setColorAt(0.55, QColor(0x1D, 0x22, 0x2E));
            g.setColorAt(1.0, QColor(0x12, 0x15, 0x1D));
        } else {
            // Crisp Light Recovery mist gradient backdrop
            g.setColorAt(0.0, QColor(0xEE, 0xF2, 0xF7));
            g.setColorAt(0.55, QColor(0xE2, 0xE8, 0xF1));
            g.setColorAt(1.0, QColor(0xD5, 0xDD, 0xE8));
        }
        p.fillRect(rect(), g);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
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
        col->setContentsMargins(24, 10, 24, 10);
        col->setSpacing(14);

        auto* hdr = new FinnLabel(QStringLiteral("FinnOS Recovery"),
                                  FinnLabel::Role::Title, FinnLabel::Tone::Ink, page);
        hdr->setFont(finn_font(22.0, QFont::DemiBold));
        hdr->setAlignment(Qt::AlignCenter);
        col->addWidget(hdr);

        auto* sub = new FinnLabel(
            QStringLiteral("Select a recovery utility to install FinnOS, inspect disks, or open a terminal."),
            FinnLabel::Role::Body, FinnLabel::Tone::Dim, page);
        sub->setAlignment(Qt::AlignCenter);
        col->addWidget(sub);

        auto make_util_row = [&](const QString& icon, const QString& title,
                                 const QString& desc, std::function<void()> on_click) {
            auto* c = new FinnCard(kRadiusLg, true, page);
            c->setFixedHeight(78);
            auto* rl = new QHBoxLayout(c);
            rl->setContentsMargins(16, 12, 16, 12);
            rl->setSpacing(14);

            rl->addWidget(new NeutralIconBadge(icon, 44, 24, 10.0, c));

            auto* tc = new QVBoxLayout();
            tc->setSpacing(2);
            auto* t = new FinnLabel(title, FinnLabel::Role::Headline, FinnLabel::Tone::Ink, c);
            auto* d = new FinnLabel(desc, FinnLabel::Role::Caption, FinnLabel::Tone::Dim, c);
            tc->addWidget(t);
            tc->addWidget(d);
            rl->addLayout(tc, 1);

            auto* open_btn = new FinnButton(QStringLiteral("Open"),
                                            FinnButton::Variant::Secondary, {}, c);
            connect(open_btn, &QAbstractButton::clicked, this, on_click);
            rl->addWidget(open_btn);
            connect(c, &FinnCard::clicked, this, on_click);
            return c;
        };

        col->addWidget(make_util_row(
            QStringLiteral("installer"), QStringLiteral("Install FinnOS"),
            QStringLiteral("Install a fresh copy of FinnOS One Preview onto internal or external storage."),
            [this]() {
                setWizardStep(0);
                stack_->setCurrentIndex(1);
            }));

        col->addWidget(make_util_row(
            QStringLiteral("folder"), QStringLiteral("Disk Utility"),
            QStringLiteral("Inspect block storage devices, partitions, and filesystems via lsblk."),
            [this]() {
                refreshDiskUtility();
                stack_->setCurrentIndex(2);
            }));

        col->addWidget(make_util_row(
            QStringLiteral("terminal"), QStringLiteral("Terminal"),
            QStringLiteral("Open Finn Terminal for partitioning, rescue, or manual Arch diagnostics."),
            [this]() {
                QProcess::startDetached(QStringLiteral("finn-terminal"), {});
                close();
            }));

        col->addWidget(make_util_row(
            QStringLiteral("info"), QStringLiteral("System Information & Firmware"),
            QStringLiteral("Verify UEFI/BIOS status, CPU architecture, and kernel logs."),
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

        // Centered neutral hero badge so the installer adapts cleanly to Light & Dark modes.
        auto* hero_badge = new NeutralIconBadge(QStringLiteral("installer"), 52, 26, 26.0, page);
        col->addWidget(hero_badge, 0, Qt::AlignHCenter);

        wiz_title_ = new FinnLabel(QStringLiteral("Install FinnOS One Preview"),
                                   FinnLabel::Role::TitleLarge, FinnLabel::Tone::Ink, page);
        wiz_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(wiz_title_);

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
                                      QStringLiteral("btrfs + FAT32 ESP"), w0), 1, 0);
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
        hostname_edit_->setText(QStringLiteral("finnos"));
        hostname_edit_->setFixedWidth(220);
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("Computer Hostname"), QStringLiteral("Local network identifier"),
            QStringLiteral("internet"), hostname_edit_, w2));

        tz_combo_ = new QComboBox(w2);
        tz_combo_->addItems({QStringLiteral("UTC"), QStringLiteral("America/New_York"),
                             QStringLiteral("Europe/London"), QStringLiteral("Europe/Berlin")});
        w2l->addWidget(new FinnSettingRow(
            QStringLiteral("Time Zone"), QStringLiteral("System clock region"),
            QStringLiteral("info"), tz_combo_, w2));

        erase_confirm_ = new FinnToggle(false, w2);
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
        install_log_->setPlaceholderText(QStringLiteral("Installer output appears here..."));
        w3l->addWidget(wrap_log_view(install_log_, w3), 1);
        wiz_stack_->addWidget(w3);

        col->addWidget(wiz_stack_, 1);
        col->addWidget(new FinnDivider(Qt::Horizontal, page));

        auto* nav = new QHBoxLayout();
        auto* hub_btn = new FinnButton(QStringLiteral("Utilities"),
                                       FinnButton::Variant::Ghost, {}, page);
        connect(hub_btn, &QAbstractButton::clicked, this, [this]() {
            stack_->setCurrentIndex(0);
        });
        wiz_back_ = new FinnButton(QStringLiteral("Back"), FinnButton::Variant::Secondary,
                                   QStringLiteral("chevron-left"), page);
        wiz_next_ = new FinnButton(QStringLiteral("Continue"), FinnButton::Variant::Primary,
                                   {}, page);
        connect(wiz_back_, &QAbstractButton::clicked, this, [this]() {
            if (wiz_stack_->currentIndex() > 0) setWizardStep(wiz_stack_->currentIndex() - 1);
        });
        connect(wiz_next_, &QAbstractButton::clicked, this, [this]() {
            const int cur = wiz_stack_->currentIndex();
            if (cur < 3) {
                setWizardStep(cur + 1);
            } else {
                startInstall();
            }
        });
        nav->addWidget(hub_btn);
        nav->addWidget(wiz_back_);
        nav->addStretch(1);
        nav->addWidget(wiz_next_);
        col->addLayout(nav);
        return page;
    }

    QWidget* buildDiskUtilityPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);
        auto* col = new QVBoxLayout(page);
        col->setSpacing(10);
        col->addWidget(new FinnLabel(QStringLiteral("Disk Utility"),
                                     FinnLabel::Role::Title, FinnLabel::Tone::Ink, page));
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
        col->setSpacing(10);
        col->addWidget(new FinnLabel(QStringLiteral("System Information"),
                                     FinnLabel::Role::Title, FinnLabel::Tone::Ink, page));
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
            "Install FinnOS One Preview",
            "Select Target Disk",
            "Configure Installation",
            "Install FinnOS",
        };
        wiz_title_->setText(QString::fromUtf8(kTitles[idx]));
        wiz_next_->setText(idx == 3 ? QStringLiteral("Install") : QStringLiteral("Continue"));
    }

    void startInstall() {
        if (selected_disk_.isEmpty() || !erase_confirm_->isChecked()) {
            install_log_->setPlainText(QStringLiteral(
                "Please select a target disk and enable 'Confirm Erase Target Disk' in Step 3."));
            return;
        }
        const QString installer = QStringLiteral("/usr/local/bin/finn-install");
        if (!QFileInfo::exists(installer)) {
            install_log_->setPlainText(QStringLiteral(
                "Install medium backend (/usr/local/bin/finn-install) is only active on the live ISO.\n"
                "Verified target: %1 (host=%2, tz=%3)")
                                           .arg(selected_disk_, hostname_edit_->text(), tz_combo_->currentText()));
            return;
        }
        auto* proc = new QProcess(this);
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            install_log_->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [proc](int, QProcess::ExitStatus) { proc->deleteLater(); });
        proc->start(QStringLiteral("pkexec"),
                    {installer, selected_disk_, hostname_edit_->text().trimmed(),
                     tz_combo_->currentText()});
    }

    FinnGlassPanel* card_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    FinnLabel* wiz_title_ = nullptr;
    QStackedWidget* wiz_stack_ = nullptr;
    FinnButton* wiz_back_ = nullptr;
    FinnButton* wiz_next_ = nullptr;
    QVector<FinnCard*> disk_cards_;
    QString selected_disk_;
    FinnTextField* hostname_edit_ = nullptr;
    QComboBox* tz_combo_ = nullptr;
    FinnToggle* erase_confirm_ = nullptr;
    QPlainTextEdit* install_log_ = nullptr;
    QPlainTextEdit* disk_util_log_ = nullptr;
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
