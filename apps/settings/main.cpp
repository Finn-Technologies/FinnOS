// Settings: complete native FinnOS System Settings & Accessibility suite.
// Uses FinnUI FigmaSplit chrome matching Figma Settings (20:98) 1:1.

#include <FinnSDK/FinnSDK.hpp>

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QProcess>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>
#include <cstdio>
#include <cstring>

#include "compat/desktop_entry.hpp"
#include "compat/wine_runner.hpp"
#include "system/desktop_services.hpp"
#include "system/sys_files.hpp"

namespace {

struct StatParts {
    QString primary;
    QString suffix;
};

QString normalize_page_id(const QString& raw) {
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("system")) return QStringLiteral("general");
    if (s == QLatin1String("display")) return QStringLiteral("displays");
    if (s == QLatin1String("wi-fi")) return QStringLiteral("wifi");
    if (s == QLatin1String("compatibility")) return QStringLiteral("compat");
    if (s == QLatin1String("privacy & security")) return QStringLiteral("privacy");
    return s;
}

StatParts read_cpu_model() {
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

    QString vendor_id;
    QString arch;
    QString lscpu_cores;
    if (raw_model.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("lscpu"), {});
        if (p.waitForFinished(1200)) {
            const QStringList lines =
                QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                if (line.startsWith(QStringLiteral("Model name:"))) {
                    const QString val = line.section(QLatin1Char(':'), 1).trimmed();
                    if (!val.isEmpty() && val != QLatin1String("-")) {
                        raw_model = val;
                    }
                } else if (line.startsWith(QStringLiteral("Vendor ID:"))) {
                    const QString val = line.section(QLatin1Char(':'), 1).trimmed();
                    if (!val.isEmpty() && val != QLatin1String("-")) {
                        vendor_id = val;
                    }
                } else if (line.startsWith(QStringLiteral("Architecture:"))) {
                    arch = line.section(QLatin1Char(':'), 1).trimmed();
                } else if (line.startsWith(QStringLiteral("CPU(s):"))) {
                    lscpu_cores = line.section(QLatin1Char(':'), 1).trimmed();
                }
            }
        }
    }

    QString primary = raw_model;
    QString suffix;
    if (raw_model.contains(QStringLiteral(" @ "))) {
        primary = raw_model.section(QStringLiteral(" @ "), 0, 0).trimmed();
        suffix = raw_model.section(QStringLiteral(" @ "), 1).trimmed();
    }
    primary.remove(QStringLiteral("(R)"));
    primary.remove(QStringLiteral("(TM)"));
    primary.replace(QStringLiteral("  "), QStringLiteral(" "));
    primary = primary.trimmed();
    if (primary.isEmpty()) {
        const QString effective_arch =
            !arch.isEmpty() ? arch : QSysInfo::currentCpuArchitecture();
        if (!vendor_id.isEmpty()) {
            primary = QStringLiteral("%1 %2").arg(vendor_id, effective_arch);
        } else {
            primary = effective_arch;
        }
    }

    if (suffix.isEmpty()) {
        QFile max_f(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"));
        if (max_f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            bool ok = false;
            const double khz = QString::fromUtf8(max_f.readAll()).trimmed().toDouble(&ok);
            if (ok && khz > 0.0) {
                suffix = QStringLiteral("%1 GHz").arg(khz / 1000000.0, 0, 'f', 2);
            }
        }
    }
    if (suffix.isEmpty()) {
        bool ok = false;
        const int parsed = lscpu_cores.toInt(&ok);
        const int cores = (ok && parsed > 0) ? parsed : qMax(1, QThread::idealThreadCount());
        suffix = cores == 1 ? QStringLiteral("1 core")
                            : QStringLiteral("%1 cores").arg(cores);
    }
    return {primary, suffix};
}

QString vendor_name_for_pci(const QString& vendor_hex) {
    const QString v = vendor_hex.toLower();
    if (v == QLatin1String("8086") || v == QLatin1String("0x8086")) return QStringLiteral("Intel");
    if (v == QLatin1String("1002") || v == QLatin1String("0x1002")) return QStringLiteral("AMD");
    if (v == QLatin1String("10de") || v == QLatin1String("0x10de")) return QStringLiteral("NVIDIA");
    if (v == QLatin1String("1af4") || v == QLatin1String("0x1af4")) return QStringLiteral("VirtIO");
    if (v == QLatin1String("1234") || v == QLatin1String("0x1234")) return QStringLiteral("QEMU");
    if (v == QLatin1String("15ad") || v == QLatin1String("0x15ad")) return QStringLiteral("VMware");
    return {};
}

StatParts read_gpu_summary() {
    QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList cards = drm.entryList({QStringLiteral("card*")}, QDir::Dirs, QDir::Name);
    for (const QString& c : cards) {
        if (c.contains(QLatin1Char('-'))) continue;
        QString driver;
        QString pci_id;
        QFile uevent(drm.filePath(c + QStringLiteral("/device/uevent")));
        if (uevent.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines =
                QString::fromUtf8(uevent.readAll()).split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                const QString trimmed = line.trimmed();
                if (trimmed.startsWith(QStringLiteral("DRIVER="))) {
                    driver = trimmed.mid(7).trimmed();
                } else if (trimmed.startsWith(QStringLiteral("PCI_ID="))) {
                    pci_id = trimmed.mid(7).trimmed();
                }
            }
        }
        QString vendor_hex;
        for (const QString& rel : {QStringLiteral("/device/vendor"),
                                   QStringLiteral("/device/../vendor")}) {
            QFile vf(drm.filePath(c + rel));
            if (vf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                vendor_hex = QString::fromUtf8(vf.readAll()).trimmed();
                if (!vendor_hex.isEmpty()) break;
            }
        }
        if (vendor_hex.isEmpty() && pci_id.contains(QLatin1Char(':'))) {
            vendor_hex = pci_id.section(QLatin1Char(':'), 0, 0);
        }
        const QString vendor = vendor_name_for_pci(vendor_hex);
        if (!vendor.isEmpty() || !driver.isEmpty()) {
            const QString primary = !vendor.isEmpty()
                                        ? QStringLiteral("%1 Graphics").arg(vendor)
                                        : QStringLiteral("DRM Graphics");
            const QString suffix = !driver.isEmpty() ? driver : pci_id;
            return {primary, suffix};
        }
    }
    return {QStringLiteral("Wayland DRM"), QStringLiteral("Mesa")};
}

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

class AccentSwatchButton final : public QAbstractButton {
public:
    AccentSwatchButton(const QString& hex, QWidget* parent = nullptr)
        : QAbstractButton(parent), hex_(hex), color_(hex) {
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        setToolTip(hex_);
        setAttribute(Qt::WA_Hover, true);
        connect(&finn::ui::FinnThemeManager::instance(), &finn::ui::FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    const QString& hex() const { return hex_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto& tm = finn::ui::FinnThemeManager::instance();
        const auto& t = tm.tokens();
        const bool active =
            QString::fromStdString(tm.settings().accent).compare(hex_, Qt::CaseInsensitive) == 0;

        const QRectF outer = rect().adjusted(2.0, 2.0, -2.0, -2.0);
        if (active) {
            p.setPen(QPen(t.ink, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        } else if (underMouse()) {
            p.setPen(QPen(t.dim, 1.25));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        }

        const QRectF swatch = rect().adjusted(5.0, 5.0, -5.0, -5.0);
        p.setPen(QPen(t.hair, 1.0));
        p.setBrush(color_);
        p.drawEllipse(swatch);
    }

private:
    QString hex_;
    QColor color_;
};

QWidget* wrap_page(QWidget* inner, QWidget* parent) {
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    inner->setAutoFillBackground(false);
    scroll->setWidget(inner);
    return scroll;
}

class SettingsWindow final : public finn::ui::FinnWindow {
public:
    explicit SettingsWindow(const QString& initial_tab = {})
        : FinnWindow(QStringLiteral("Settings"), QStringLiteral("settings-app")) {
        // FinnWindow uses kShadowMargin = 10 on each side, so 820x520 renders a 800x500 frame.
        setMinimumSize(800, 500);
        resize(820, 520);
        setChromeMode(ChromeMode::FigmaSplit);

        const QColor blue(0x00, 0x7B, 0xFF);
        const QColor gray(0xAB, 0xAB, 0xAB);
        const QColor black(0x00, 0x00, 0x00);
        const QColor slate_dark(0x3A, 0x40, 0x50);
        const QColor slate_mid(0x5B, 0x64, 0x72);
        const QColor emerald(0x2E, 0x8B, 0x57);
        const QColor amber(0xC9, 0x7A, 0x16);

        sidebar_ = new finn::ui::FinnSidebar(250, this);
        sidebar_->setHeaderTitle(QStringLiteral("Settings"));
        apply_header_16px_regular(sidebar_->headerLabel());
        sidebar_->setSearchEnabled(true, QStringLiteral("Search anything"));

        sidebar_->addItem(QStringLiteral("wifi"), QStringLiteral("wifi-02"),
                          QStringLiteral("Wi-Fi"), blue);
        sidebar_->addItem(QStringLiteral("bluetooth"), QStringLiteral("bluetooth"),
                          QStringLiteral("Bluetooth"), blue);
        sidebar_->addItem(QStringLiteral("network"), QStringLiteral("internet"),
                          QStringLiteral("Network"), blue);
        sidebar_->addSectionSpacer(20);

        sidebar_->addItem(QStringLiteral("general"), QStringLiteral("settings-02"),
                          QStringLiteral("General"), gray, slate_mid);
        sidebar_->addItem(QStringLiteral("appearance"), QStringLiteral("paint-brush"),
                          QStringLiteral("Appearance"), black, slate_dark);
        sidebar_->addItem(QStringLiteral("displays"), QStringLiteral("display"),
                          QStringLiteral("Display"), blue);
        sidebar_->addItem(QStringLiteral("sound"), QStringLiteral("volume-02"),
                          QStringLiteral("Sound"), blue);
        sidebar_->addSectionSpacer(12);

        sidebar_->addItem(QStringLiteral("accessibility"), QStringLiteral("accessibility"),
                          QStringLiteral("Accessibility"), slate_mid);
        sidebar_->addItem(QStringLiteral("accounts"), QStringLiteral("user"),
                          QStringLiteral("Accounts"), slate_dark);
        sidebar_->addItem(QStringLiteral("privacy"), QStringLiteral("shield"),
                          QStringLiteral("Privacy & Security"), emerald);
        sidebar_->addItem(QStringLiteral("updates"), QStringLiteral("update"),
                          QStringLiteral("Updates"), amber);
        sidebar_->addItem(QStringLiteral("compat"), QStringLiteral("windows"),
                          QStringLiteral("Compatibility"), gray, slate_mid);
        sidebar_->addStretch();

        // Pin About at the bottom of the sidebar (addFooterWidget) so it stays visible
        // below the scroll area and keeps Wi-Fi at the top matching Figma 20:64.
        about_item_ = new finn::ui::FinnNavItem(
            QStringLiteral("info"), QStringLiteral("About"), sidebar_);
        about_item_->setBadgeColor(blue);
        connect(about_item_, &QAbstractButton::clicked, this, [this]() {
            selectPage(QStringLiteral("about"));
        });
        sidebar_->addFooterWidget(about_item_);
        setSidebarWidget(sidebar_);

        stack_ = new QStackedWidget(this);
        stack_->setAutoFillBackground(false);
        finn::ui::install_stack_animator(stack_);
        const QVector<QPair<QString, QWidget*>> pages = {
            {QStringLiteral("wifi"), buildWifiPage()},
            {QStringLiteral("bluetooth"), buildBluetoothPage()},
            {QStringLiteral("network"), buildNetworkPage()},
            {QStringLiteral("general"), buildGeneralPage()},
            {QStringLiteral("appearance"), buildAppearancePage()},
            {QStringLiteral("displays"), buildDisplaysPage()},
            {QStringLiteral("sound"), buildSoundPage()},
            {QStringLiteral("accessibility"), buildAccessibilityPage()},
            {QStringLiteral("accounts"), buildAccountsPage()},
            {QStringLiteral("privacy"), buildPrivacyPage()},
            {QStringLiteral("updates"), buildUpdatesPage()},
            {QStringLiteral("compat"), buildCompatPage()},
            {QStringLiteral("about"), buildAboutPage()},
        };
        for (const auto& p : pages) {
            page_indices_[p.first] = stack_->addWidget(wrap_page(p.second, stack_));
        }
        setContentWidget(stack_);

        connect(sidebar_, &finn::ui::FinnSidebar::selected, this,
                [this](const QString& id) {
                    if (about_item_) about_item_->setSelected(id == QLatin1String("about"));
                    if (page_indices_.contains(id)) {
                        stack_->setCurrentIndex(page_indices_.value(id));
                    }
                });

        const QString normalized = normalize_page_id(initial_tab);
        const QString start_tab = (!normalized.isEmpty() && page_indices_.contains(normalized))
                                      ? normalized
                                      : QStringLiteral("about");
        selectPage(start_tab);

        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::stateUpdated, this,
                &SettingsWindow::refreshLiveHardware);
        refreshLiveHardware();
    }

private:
    void selectPage(const QString& id) {
        if (!page_indices_.contains(id)) return;
        if (id == QLatin1String("about")) {
            sidebar_->setCurrentId(QString());
            if (about_item_) about_item_->setSelected(true);
        } else {
            if (about_item_) about_item_->setSelected(false);
            sidebar_->setCurrentId(id);
        }
        stack_->setCurrentIndex(page_indices_.value(id));
    }

    QWidget* makePageHeader(const QString& title, QWidget* parent) {
        auto* lbl = new finn::ui::FinnLabel(
            title, finn::ui::FinnLabel::Role::Title,
            finn::ui::FinnLabel::Tone::Ink, parent);
        apply_header_16px_regular(lbl);
        lbl->setContentsMargins(0, 0, 96, 10);
        return lbl;
    }

    QWidget* buildWifiPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Wi-Fi"), page));

        wifi_toggle_ = new finn::ui::FinnToggle(false, page);
        connect(wifi_toggle_, &QAbstractButton::toggled, this, [](bool on) {
            finn::sys::FinnDesktopServices::instance().setWifiEnabled(on);
        });
        wifi_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Wi-Fi Radio"), QStringLiteral("Checking NetworkManager..."),
            QStringLiteral("wifi-02"), wifi_toggle_, page);
        root->addWidget(wifi_row_);

        auto* scan_btn = new finn::ui::FinnButton(
            QStringLiteral("Scan Networks"), finn::ui::FinnButton::Variant::Secondary,
            QStringLiteral("refresh"), page);
        connect(scan_btn, &QAbstractButton::clicked, this, []() {
            finn::sys::FinnDesktopServices::instance().refreshAll();
        });
        root->addWidget(scan_btn, 0, Qt::AlignLeft);

        wifi_list_layout_ = new QVBoxLayout();
        wifi_list_layout_->setSpacing(8);
        root->addLayout(wifi_list_layout_);
        root->addStretch(1);
        return page;
    }

    QWidget* buildBluetoothPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Bluetooth"), page));

        bt_toggle_ = new finn::ui::FinnToggle(false, page);
        connect(bt_toggle_, &QAbstractButton::toggled, this, [](bool on) {
            finn::sys::FinnDesktopServices::instance().setBluetoothPowered(on);
        });
        bt_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Bluetooth Adapter"), QStringLiteral("Checking BlueZ..."),
            QStringLiteral("bluetooth"), bt_toggle_, page);
        root->addWidget(bt_row_);

        bt_list_layout_ = new QVBoxLayout();
        bt_list_layout_->setSpacing(8);
        root->addLayout(bt_list_layout_);
        root->addStretch(1);
        return page;
    }

    QWidget* buildNetworkPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Network"), page));

        net_detail_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Active Interfaces"), QStringLiteral("Checking..."),
            QStringLiteral("internet"), nullptr, page);
        root->addWidget(net_detail_row_);

        auto* nm_btn = new finn::ui::FinnButton(
            QStringLiteral("Refresh Network State"),
            finn::ui::FinnButton::Variant::Secondary, QStringLiteral("refresh"), page);
        connect(nm_btn, &QAbstractButton::clicked, this, [this]() {
            finn::sys::FinnDesktopServices::instance().refreshAll();
            showToast(QStringLiteral("Network state refreshed"));
        });
        root->addWidget(nm_btn, 0, Qt::AlignLeft);
        root->addStretch(1);
        return page;
    }

    QWidget* buildGeneralPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("General"), page));

        battery_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Power Source"), QStringLiteral("Checking kernel power_supply..."),
            QStringLiteral("battery"), nullptr, page);
        root->addWidget(battery_row_);

        auto* lock_sleep = new finn::ui::FinnToggle(
            finn::ui::FinnThemeManager::instance().settings().lock_on_sleep, page);
        connect(lock_sleep, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setLockOnSleep(on);
            showToast(on ? QStringLiteral("Lock on sleep enabled")
                         : QStringLiteral("Lock on sleep disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Lock Screen on Sleep"),
            QStringLiteral("Require authentication when resuming from suspend"),
            QStringLiteral("lock"), lock_sleep, page));

        auto* timeout_box = new QComboBox(page);
        timeout_box->addItem(QStringLiteral("Never"), 0);
        timeout_box->addItem(QStringLiteral("1 minute"), 1);
        timeout_box->addItem(QStringLiteral("5 minutes"), 5);
        timeout_box->addItem(QStringLiteral("15 minutes"), 15);
        timeout_box->addItem(QStringLiteral("30 minutes"), 30);
        const int cur_alm = finn::ui::FinnThemeManager::instance().settings().auto_lock_minutes;
        const int idx = timeout_box->findData(cur_alm);
        if (idx >= 0) timeout_box->setCurrentIndex(idx);
        connect(timeout_box, &QComboBox::currentIndexChanged, this, [this, timeout_box](int i) {
            finn::ui::FinnThemeManager::instance().setAutoLockMinutes(timeout_box->itemData(i).toInt());
            showToast(QStringLiteral("Auto-lock timeout updated"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Auto-Lock Timeout"),
            QStringLiteral("Automatically lock session after idle period"),
            QStringLiteral("lock"), timeout_box, page));

        auto* suspend_btn = new finn::ui::FinnButton(
            QStringLiteral("Sleep"), finn::ui::FinnButton::Variant::Secondary, {}, page);
        connect(suspend_btn, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Suspend System"),
            QStringLiteral("Put hardware into low-power S3/S0ix sleep state"),
            QStringLiteral("moon"), suspend_btn, page));
        root->addStretch(1);
        return page;
    }

    QWidget* buildAppearancePage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Appearance"), page));

        auto& theme = finn::ui::FinnThemeManager::instance();

        auto* dark_toggle = new finn::ui::FinnToggle(theme.isDark(), page);
        connect(dark_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setDark(on);
            showToast(on ? QStringLiteral("Dark Mode enabled")
                         : QStringLiteral("Light Mode enabled"));
        });
        connect(&theme, &finn::ui::FinnThemeManager::themeChanged, dark_toggle,
                [dark_toggle]() {
                    QSignalBlocker b(dark_toggle);
                    dark_toggle->setChecked(finn::ui::FinnThemeManager::instance().isDark());
                });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Dark Mode"),
            QStringLiteral("Switch system surface palette and shell glass"),
            QStringLiteral("moon"), dark_toggle, page));

        auto* accent_row = new QWidget(page);
        accent_row->setAutoFillBackground(false);
        auto* alay = new QHBoxLayout(accent_row);
        alay->setContentsMargins(0, 0, 0, 0);
        alay->setSpacing(6);
        for (const QString& hex : {QStringLiteral("#007BFF"), QStringLiteral("#7C3AED"),
                                   QStringLiteral("#059669"), QStringLiteral("#D97706"),
                                   QStringLiteral("#DC2626")}) {
            auto* sw = new AccentSwatchButton(hex, accent_row);
            connect(sw, &QAbstractButton::clicked, this, [this, hex]() {
                finn::ui::FinnThemeManager::instance().setAccent(hex);
                showToast(QStringLiteral("Accent updated to %1").arg(hex));
            });
            alay->addWidget(sw);
        }
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Accent Color"),
            QStringLiteral("Highlight color across FinnUI controls and focus rings"),
            QStringLiteral("paint-brush"), accent_row, page));

        auto* wp_btns = new QWidget(page);
        auto* wlay = new QHBoxLayout(wp_btns);
        wlay->setContentsMargins(0, 0, 0, 0);
        wlay->setSpacing(6);
        auto* choose_wp = new finn::ui::FinnButton(
            QStringLiteral("Choose Image..."), finn::ui::FinnButton::Variant::Secondary, {}, wp_btns);
        connect(choose_wp, &QAbstractButton::clicked, this, [this]() {
            const QString file = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select Wallpaper"), QDir::homePath(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
            if (!file.isEmpty()) {
                finn::ui::FinnThemeManager::instance().setWallpaper(file);
                showToast(QStringLiteral("Wallpaper updated"));
            }
        });
        auto* reset_wp = new finn::ui::FinnButton(
            QStringLiteral("Default"), finn::ui::FinnButton::Variant::Ghost, {}, wp_btns);
        connect(reset_wp, &QAbstractButton::clicked, this, [this]() {
            finn::ui::FinnThemeManager::instance().setWallpaper({});
            showToast(QStringLiteral("Restored default FinnOS wallpaper"));
        });
        wlay->addWidget(choose_wp);
        wlay->addWidget(reset_wp);
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Desktop Wallpaper"),
            QStringLiteral("Displayed on the background layer behind FinnUX"),
            QStringLiteral("display"), wp_btns, page));

        root->addStretch(1);
        return page;
    }

    QWidget* buildDisplaysPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Display"), page));

        brightness_slider_ = new finn::ui::FinnSlider(5, 100, 100, page);
        brightness_slider_->setFixedWidth(180);
        connect(brightness_slider_, &finn::ui::FinnSlider::valueChanged, this, [](int v) {
            finn::sys::FinnDesktopServices::instance().setBrightnessPercent(v);
        });
        brightness_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Brightness"), QStringLiteral("Checking /sys/class/backlight..."),
            QStringLiteral("sun-01"), brightness_slider_, page);
        root->addWidget(brightness_row_);

        auto* scale_box = new QComboBox(page);
        for (double s : {1.0, 1.25, 1.5, 2.0}) {
            scale_box->addItem(QStringLiteral("%1%").arg(qRound(s * 100)), s);
        }
        const int sidx = scale_box->findData(finn::ui::FinnThemeManager::instance().tokens().scale);
        if (sidx >= 0) scale_box->setCurrentIndex(sidx);
        connect(scale_box, &QComboBox::currentIndexChanged, this, [this, scale_box](int i) {
            finn::ui::FinnThemeManager::instance().setScale(scale_box->itemData(i).toDouble());
            showToast(QStringLiteral("Display scale updated"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Display Scale"),
            QStringLiteral("Interface scaling factor for HiDPI panels"),
            QStringLiteral("display"), scale_box, page));

        for (QScreen* scr : QGuiApplication::screens()) {
            const QSize res = scr->geometry().size();
            root->addWidget(new finn::ui::FinnSettingRow(
                QStringLiteral("Output: %1").arg(scr->name()),
                QStringLiteral("%1×%2 @ %3 Hz (DPR %4)")
                    .arg(res.width())
                    .arg(res.height())
                    .arg(qRound(scr->refreshRate()))
                    .arg(scr->devicePixelRatio(), 0, 'f', 1),
                QStringLiteral("display"), nullptr, page));
        }
        root->addStretch(1);
        return page;
    }

    QWidget* buildSoundPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Sound"), page));

        auto* vol_controls = new QWidget(page);
        auto* vlay = new QHBoxLayout(vol_controls);
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->setSpacing(8);
        mute_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Mute"), finn::ui::FinnButton::Variant::Secondary, {}, vol_controls);
        mute_btn_->setCheckable(true);
        connect(mute_btn_, &QAbstractButton::toggled, this, [](bool muted) {
            auto& svc = finn::sys::FinnDesktopServices::instance();
            if (svc.audio().muted != muted) svc.toggleMute();
        });
        volume_slider_ = new finn::ui::FinnSlider(0, 100, 75, vol_controls);
        volume_slider_->setFixedWidth(160);
        connect(volume_slider_, &finn::ui::FinnSlider::valueChanged, this, [](int v) {
            finn::sys::FinnDesktopServices::instance().setVolumePercent(v);
        });
        vlay->addWidget(volume_slider_);
        vlay->addWidget(mute_btn_);

        sound_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("Master Output Volume"), QStringLiteral("Checking PipeWire..."),
            QStringLiteral("volume-02"), vol_controls, page);
        root->addWidget(sound_row_);
        root->addStretch(1);
        return page;
    }

    QWidget* buildAccessibilityPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Accessibility"), page));

        const auto& s = finn::ui::FinnThemeManager::instance().settings();

        auto* sr_toggle = new finn::ui::FinnToggle(s.screen_reader, page);
        connect(sr_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setScreenReader(on);
            if (on) {
                QProcess::startDetached(QStringLiteral("orca"), {});
            } else {
                QProcess::startDetached(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("orca")});
            }
            showToast(on ? QStringLiteral("Screen Reader enabled")
                         : QStringLiteral("Screen Reader disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Screen Reader"),
            QStringLiteral("Spoken UI navigation via Orca & AT-SPI2"),
            QStringLiteral("volume-02"), sr_toggle, page));

        auto* hc_toggle = new finn::ui::FinnToggle(s.high_contrast, page);
        connect(hc_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setHighContrast(on);
            showToast(on ? QStringLiteral("High Contrast enabled")
                         : QStringLiteral("High Contrast disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("High Contrast"),
            QStringLiteral("Sharpen borders, opaque panels, and maximum ink contrast"),
            QStringLiteral("accessibility"), hc_toggle, page));

        auto* cf_box = new QComboBox(page);
        const QVector<QPair<QString, QString>> filters = {
            {QStringLiteral("None"), QStringLiteral("none")},
            {QStringLiteral("Grayscale"), QStringLiteral("grayscale")},
            {QStringLiteral("Protanopia (Red-Weak)"), QStringLiteral("protanopia")},
            {QStringLiteral("Deuteranopia (Green-Weak)"), QStringLiteral("deuteranopia")},
            {QStringLiteral("Tritanopia (Blue-Weak)"), QStringLiteral("tritanopia")},
            {QStringLiteral("Inverted Colors"), QStringLiteral("inverted")},
        };
        for (const auto& f : filters) cf_box->addItem(f.first, f.second);
        const int fidx = cf_box->findData(QString::fromStdString(s.color_filter));
        if (fidx >= 0) cf_box->setCurrentIndex(fidx);
        connect(cf_box, &QComboBox::currentIndexChanged, this, [this, cf_box](int i) {
            finn::ui::FinnThemeManager::instance().setColorFilter(cf_box->itemData(i).toString());
            showToast(QStringLiteral("Color filter: %1").arg(cf_box->currentText()));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Color Filters"),
            QStringLiteral("Assistive color-blindness & luminance compensation"),
            QStringLiteral("paint-brush"), cf_box, page));

        auto* ts_slider = new finn::ui::FinnSlider(85, 175, qRound(s.text_scale * 100), page);
        ts_slider->setFixedWidth(160);
        auto* ts_row = new finn::ui::FinnSettingRow(
            QStringLiteral("Text Scaling"),
            QStringLiteral("Current: %1%").arg(qRound(s.text_scale * 100)),
            QStringLiteral("accessibility"), ts_slider, page);
        connect(ts_slider, &finn::ui::FinnSlider::valueChanged, this, [ts_row](int pct) {
            ts_row->setSubtitle(QStringLiteral("Current: %1%").arg(pct));
            finn::ui::FinnThemeManager::instance().setTextScale(pct / 100.0);
        });
        root->addWidget(ts_row);

        auto* motion_toggle = new finn::ui::FinnToggle(s.reduced_motion, page);
        connect(motion_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setReducedMotion(on);
            showToast(on ? QStringLiteral("Reduced Motion enabled")
                         : QStringLiteral("Standard spring animations restored"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Reduced Motion"),
            QStringLiteral("Replace spring & slide transitions with instant fades"),
            QStringLiteral("accessibility"), motion_toggle, page));

        auto* sticky_toggle = new finn::ui::FinnToggle(s.sticky_keys, page);
        connect(sticky_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setStickyKeys(on);
            showToast(on ? QStringLiteral("Sticky Keys enabled")
                         : QStringLiteral("Sticky Keys disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Sticky Keys"),
            QStringLiteral("Press modifier keys sequentially instead of simultaneously"),
            QStringLiteral("keyboard"), sticky_toggle, page));

        auto* filter_toggle = new finn::ui::FinnToggle(s.filter_keys, page);
        connect(filter_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setFilterKeys(on);
            showToast(on ? QStringLiteral("Filter Keys enabled")
                         : QStringLiteral("Filter Keys disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Filter Keys"),
            QStringLiteral("Ignore brief or repeated accidental keystrokes"),
            QStringLiteral("keyboard"), filter_toggle, page));

        auto* cur_box = new QComboBox(page);
        for (int sz : {28, 36, 48, 64}) {
            cur_box->addItem(QStringLiteral("%1 px").arg(sz), sz);
        }
        const int cidx = cur_box->findData(s.cursor_size);
        if (cidx >= 0) cur_box->setCurrentIndex(cidx);
        connect(cur_box, &QComboBox::currentIndexChanged, this, [this, cur_box](int i) {
            finn::ui::FinnThemeManager::instance().setCursorSize(cur_box->itemData(i).toInt());
            showToast(QStringLiteral("Cursor size updated"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Pointer Size"),
            QStringLiteral("Scale Figma vector cursor across compositor & apps"),
            QStringLiteral("cursor"), cur_box, page));

        root->addStretch(1);
        return page;
    }

    QWidget* buildAccountsPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Accounts & Multi-User"), page));

        QFile passwd(QStringLiteral("/etc/passwd"));
        if (passwd.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines =
                QString::fromUtf8(passwd.readAll()).split(QLatin1Char('\n'));
            for (const QString& raw_line : lines) {
                const QStringList parts = raw_line.trimmed().split(QLatin1Char(':'));
                if (parts.size() < 7) continue;
                bool ok = false;
                const int uid = parts[2].toInt(&ok);
                if (ok && uid >= 1000 && uid < 65534) {
                    const bool active = (parts[0] == qEnvironmentVariable("USER"));
                    auto* badge = new finn::ui::FinnBadge(
                        active ? QStringLiteral("Active") : QStringLiteral("Local User"),
                        active ? finn::ui::FinnBadge::Tone::Accent
                               : finn::ui::FinnBadge::Tone::Neutral,
                        page);
                    root->addWidget(new finn::ui::FinnSettingRow(
                        parts[4].section(QLatin1Char(','), 0, 0).isEmpty()
                            ? parts[0]
                            : parts[4].section(QLatin1Char(','), 0, 0),
                        QStringLiteral("Username: %1  |  UID %2  |  Shell: %3")
                            .arg(parts[0]).arg(uid).arg(parts[6]),
                        QStringLiteral("user"), badge, page));
                }
            }
        }

        auto* lock_btn = new finn::ui::FinnButton(
            QStringLiteral("Lock & Switch User"), finn::ui::FinnButton::Variant::Primary,
            QStringLiteral("lock"), page);
        connect(lock_btn, &QAbstractButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
        });
        root->addWidget(lock_btn, 0, Qt::AlignLeft);
        root->addStretch(1);
        return page;
    }

    QWidget* buildPrivacyPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Privacy & Security"), page));

        const bool has_flatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Application Sandboxing"),
            has_flatpak ? QStringLiteral("Flatpak XDG Desktop Portals available for sandboxed apps")
                        : QStringLiteral("Native apps run with user session privileges; install flatpak for portals"),
            QStringLiteral("shield"),
            new finn::ui::FinnBadge(has_flatpak ? QStringLiteral("Ready") : QStringLiteral("Native"),
                                    finn::ui::FinnBadge::Tone::Success, page),
            page));

        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Wayland Display Isolation"),
            QStringLiteral("Compositor enforces per-client surface isolation on /run/finn/wayland-0"),
            QStringLiteral("lock"),
            new finn::ui::FinnBadge(QStringLiteral("Active"), finn::ui::FinnBadge::Tone::Accent, page),
            page));
        root->addStretch(1);
        return page;
    }

    QWidget* buildUpdatesPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Updates"), page));

        auto* status_row = new finn::ui::FinnSettingRow(
            QStringLiteral("Arch Linux & FinnOS Packages"),
            QStringLiteral("Click Check Now to query pacman & Flatpak remotes"),
            QStringLiteral("update"), nullptr, page);
        check_updates_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Check Now"), finn::ui::FinnButton::Variant::Primary,
            QStringLiteral("update"), page);
        connect(check_updates_btn_, &QAbstractButton::clicked, this, [this, status_row]() {
            QProcess p;
            p.start(QStringLiteral("pacman"), {QStringLiteral("-Qu")});
            if (!p.waitForFinished(4000)) {
                status_row->setSubtitle(QStringLiteral("Timed out querying pacman package updates"));
                showToast(QStringLiteral("Update check timed out"));
                return;
            }
            const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            const int count = out.isEmpty() ? 0 : out.split(QLatin1Char('\n')).size();
            status_row->setSubtitle(
                count == 0 ? QStringLiteral("System is up to date")
                           : QStringLiteral("%1 package update(s) available — run sudo pacman -Syu in Finn Terminal").arg(count));
            showToast(count == 0 ? QStringLiteral("Up to date")
                                 : QStringLiteral("%1 updates found").arg(count));
        });
        status_row->setTrailingWidget(check_updates_btn_);
        root->addWidget(status_row);
        root->addStretch(1);
        return page;
    }

    QWidget* buildCompatPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Compatibility"), page));

        const auto wine = finn::compat::probe_wine_runtime();
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Windows Applications (Wine / Proton)"),
            wine.ready ? QStringLiteral("Runtime: %1 (%2)")
                             .arg(QString::fromStdString(wine.wine_bin),
                                  QString::fromStdString(wine.summary))
                       : QString::fromStdString(wine.summary),
            QStringLiteral("windows"),
            new finn::ui::FinnBadge(
                wine.ready ? QStringLiteral("Ready") : QStringLiteral("Unavailable"),
                wine.ready ? finn::ui::FinnBadge::Tone::Success
                           : finn::ui::FinnBadge::Tone::Neutral,
                page),
            page));

        const bool has_waydroid = !QStandardPaths::findExecutable(QStringLiteral("waydroid")).isEmpty();
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Android Applications (Waydroid)"),
            has_waydroid ? QStringLiteral("Waydroid container runtime installed for windowed Android apps")
                         : QStringLiteral("Waydroid not installed — install waydroid + binder kernel module"),
            QStringLiteral("android"),
            new finn::ui::FinnBadge(
                has_waydroid ? QStringLiteral("Ready") : QStringLiteral("Not Installed"),
                has_waydroid ? finn::ui::FinnBadge::Tone::Success : finn::ui::FinnBadge::Tone::Neutral,
                page),
            page));

        const bool has_flatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Linux Applications (Arch / Flatpak / AppImage)"),
            has_flatpak ? QStringLiteral("Native Wayland/Xwayland + Flatpak runtime active")
                        : QStringLiteral("Native Wayland/Xwayland active"),
            QStringLiteral("linux"),
            new finn::ui::FinnBadge(QStringLiteral("Active"), finn::ui::FinnBadge::Tone::Accent, page),
            page));

        root->addStretch(1);
        return page;
    }

    QWidget* buildAboutPage() {
        // 1:1 Figma Settings - About (20:64, 550x500 content pane)
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 24, 12, 20);
        root->setSpacing(0);

        auto* header = new finn::ui::FinnLabel(
            QStringLiteral("About this device"), finn::ui::FinnLabel::Role::Title,
            finn::ui::FinnLabel::Tone::Ink, page);
        apply_header_16px_regular(header);
        root->addWidget(header);
        root->addSpacing(20);

        auto* grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);

        auto* hero = new finn::ui::FinnHeroCard(
            QStringLiteral("FinnOS "), QString(), QStringLiteral("boot-mark"), page);
        hero->setInlineSuffix(QStringLiteral("One Preview"));
        hero->setFixedHeight(150);
        grid->addWidget(hero, 0, 0, 2, 1);

        const auto mem = finn::sys::read_meminfo();
        QString ram_primary = QStringLiteral("Unknown");
        QString ram_suffix;
        if (mem.total_kib > 0) {
            const double ram_gb = mem.total_kib / (1024.0 * 1024.0);
            const double avail_gb = mem.available_kib / (1024.0 * 1024.0);
            ram_primary = QStringLiteral("%1 GB").arg(ram_gb, 0, 'f', 1);
            ram_suffix = QStringLiteral("%1 GB free").arg(avail_gb, 0, 'f', 1);
        }
        auto* ram_card = new finn::ui::FinnStatCard(
            QStringLiteral("RAM"), ram_primary, ram_suffix, page);
        ram_card->setFixedHeight(70);
        grid->addWidget(ram_card, 0, 1);

        const QStorageInfo root_vol = QStorageInfo::root();
        QString disk_primary = QStringLiteral("Unavailable");
        QString disk_suffix;
        if (root_vol.isValid() && root_vol.bytesTotal() > 0) {
            const double disk_gb = root_vol.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
            const double used_gb =
                (root_vol.bytesTotal() - root_vol.bytesAvailable()) / (1024.0 * 1024.0 * 1024.0);
            const QString fs = QString::fromUtf8(root_vol.fileSystemType()).trimmed();
            disk_primary = QStringLiteral("%1 GB").arg(qRound(disk_gb));
            disk_suffix = fs.isEmpty()
                              ? QStringLiteral("%1 GB used").arg(qRound(used_gb))
                              : QStringLiteral("%1 GB used · %2").arg(qRound(used_gb)).arg(fs);
        }
        auto* mem_card = new finn::ui::FinnStatCard(
            QStringLiteral("Memory"), disk_primary, disk_suffix, page);
        mem_card->setFixedHeight(70);
        grid->addWidget(mem_card, 1, 1);

        const auto cpu = read_cpu_model();
        auto* cpu_card = new finn::ui::FinnStatCard(
            QStringLiteral("CPU"), cpu.primary, cpu.suffix, page);
        cpu_card->setFixedHeight(70);
        grid->addWidget(cpu_card, 2, 0);

        const auto gpu = read_gpu_summary();
        auto* gpu_card = new finn::ui::FinnStatCard(
            QStringLiteral("GPU"), gpu.primary, gpu.suffix, page);
        gpu_card->setFixedHeight(70);
        grid->addWidget(gpu_card, 2, 1);

        root->addLayout(grid);
        root->addStretch(1);

        auto* actions = new QHBoxLayout();
        actions->setContentsMargins(0, 12, 0, 0);
        actions->setSpacing(10);
        auto* monitor_btn = new finn::ui::FinnButton(
            QStringLiteral("Open System Monitor"),
            finn::ui::FinnButton::Variant::Secondary, {}, page);
        connect(monitor_btn, &QAbstractButton::clicked, this, [this]() {
            if (!QProcess::startDetached(QStringLiteral("finn-monitor"), {})) {
                showToast(QStringLiteral("System Monitor (finn-monitor) is not installed"));
            }
        });
        auto* updates_btn = new finn::ui::FinnButton(
            QStringLiteral("Check for Updates"),
            finn::ui::FinnButton::Variant::Secondary, {}, page);
        connect(updates_btn, &QAbstractButton::clicked, this, [this]() {
            selectPage(QStringLiteral("updates"));
            if (check_updates_btn_) check_updates_btn_->click();
        });
        actions->addWidget(monitor_btn);
        actions->addWidget(updates_btn);
        actions->addStretch(1);
        root->addLayout(actions);
        return page;
    }

    void refreshLiveHardware() {
        const auto& svc = finn::sys::FinnDesktopServices::instance();
        const auto& bl = svc.backlight();
        brightness_slider_->setEnabled(bl.available);
        if (bl.available) {
            QSignalBlocker b(brightness_slider_);
            brightness_slider_->setValue(bl.percent);
            brightness_row_->setSubtitle(
                QStringLiteral("%1 (%2%)").arg(QString::fromStdString(bl.device)).arg(bl.percent));
        } else {
            brightness_row_->setSubtitle(
                QStringLiteral("No hardware backlight interface under /sys/class/backlight"));
        }

        const auto& net = svc.network();
        {
            QSignalBlocker b(wifi_toggle_);
            wifi_toggle_->setEnabled(net.wifi_hardware);
            wifi_toggle_->setChecked(net.wifi_hardware && net.wifi_enabled);
        }
        if (!net.wifi_hardware) {
            wifi_row_->setSubtitle(QStringLiteral("No Wi-Fi radio detected on this machine"));
        } else if (!net.active_wifi_ssid.isEmpty()) {
            wifi_row_->setSubtitle(QStringLiteral("Connected to %1").arg(net.active_wifi_ssid));
        } else {
            wifi_row_->setSubtitle(net.wifi_enabled ? QStringLiteral("Ready") : QStringLiteral("Turned off"));
        }

        QString net_sub = net.wired_connected
                              ? QStringLiteral("Wired: %1").arg(net.wired_label)
                              : QStringLiteral("No wired interface active");
        if (!net.ipv4_summary.isEmpty()) net_sub += QStringLiteral("  |  %1").arg(net.ipv4_summary);
        net_detail_row_->setSubtitle(net_sub);

        while (QLayoutItem* item = wifi_list_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        for (const auto& w : net.networks) {
            auto* cbtn = new finn::ui::FinnButton(
                w.active ? QStringLiteral("Connected") : QStringLiteral("Connect"),
                w.active ? finn::ui::FinnButton::Variant::Secondary
                         : finn::ui::FinnButton::Variant::Primary, {});
            cbtn->setEnabled(!w.active);
            connect(cbtn, &QAbstractButton::clicked, this, [this, w]() {
                QString pwd;
                if (w.secured) {
                    pwd = finn::ui::FinnDialog::prompt(
                        this, QStringLiteral("Wi-Fi Password"),
                        QStringLiteral("Password for %1:").arg(w.ssid));
                    if (pwd.isEmpty()) return;
                }
                finn::sys::FinnDesktopServices::instance().connectWifi(w.ssid, pwd);
            });
            wifi_list_layout_->addWidget(new finn::ui::FinnSettingRow(
                w.ssid,
                QStringLiteral("Signal %1%%2").arg(w.signal).arg(w.secured ? QStringLiteral("  |  Secured") : QString()),
                QStringLiteral("wifi-02"), cbtn));
        }

        const auto& bt = svc.bluetooth();
        {
            QSignalBlocker b(bt_toggle_);
            bt_toggle_->setEnabled(bt.available);
            bt_toggle_->setChecked(bt.available && bt.powered);
        }
        bt_row_->setSubtitle(
            bt.available ? QStringLiteral("Controller: %1").arg(bt.controller_name)
                         : QStringLiteral("No Bluetooth controller detected"));

        while (QLayoutItem* item = bt_list_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        for (const auto& d : bt.devices) {
            auto* bbtn = new finn::ui::FinnButton(
                d.connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"),
                finn::ui::FinnButton::Variant::Secondary, {});
            connect(bbtn, &QAbstractButton::clicked, this, [d]() {
                finn::sys::FinnDesktopServices::instance().toggleBluetoothDevice(d.address, !d.connected);
            });
            bt_list_layout_->addWidget(new finn::ui::FinnSettingRow(
                d.name, d.address, QStringLiteral("bluetooth"), bbtn));
        }

        const auto& aud = svc.audio();
        volume_slider_->setEnabled(aud.available);
        mute_btn_->setEnabled(aud.available);
        if (aud.available) {
            QSignalBlocker b1(volume_slider_);
            QSignalBlocker b2(mute_btn_);
            volume_slider_->setValue(aud.volume_percent);
            mute_btn_->setChecked(aud.muted);
            sound_row_->setSubtitle(
                aud.muted ? QStringLiteral("Muted (%1%)").arg(aud.volume_percent)
                          : QStringLiteral("Volume: %1%").arg(aud.volume_percent));
        } else {
            sound_row_->setSubtitle(QStringLiteral("No active PipeWire default audio sink"));
        }

        const auto& bat = svc.battery();
        battery_row_->setSubtitle(
            bat.available ? QStringLiteral("%1 (%2% — %3)")
                                .arg(QString::fromStdString(bat.name))
                                .arg(bat.capacity_percent)
                                .arg(QString::fromStdString(bat.status_text))
                          : QStringLiteral("AC Power (no battery reported by kernel)"));
    }

    finn::ui::FinnSidebar* sidebar_ = nullptr;
    finn::ui::FinnNavItem* about_item_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QMap<QString, int> page_indices_;
    finn::ui::FinnButton* check_updates_btn_ = nullptr;

    finn::ui::FinnSlider* brightness_slider_ = nullptr;
    finn::ui::FinnSettingRow* brightness_row_ = nullptr;
    finn::ui::FinnToggle* wifi_toggle_ = nullptr;
    finn::ui::FinnSettingRow* wifi_row_ = nullptr;
    finn::ui::FinnSettingRow* net_detail_row_ = nullptr;
    QVBoxLayout* wifi_list_layout_ = nullptr;
    finn::ui::FinnToggle* bt_toggle_ = nullptr;
    finn::ui::FinnSettingRow* bt_row_ = nullptr;
    QVBoxLayout* bt_list_layout_ = nullptr;
    finn::ui::FinnSlider* volume_slider_ = nullptr;
    finn::ui::FinnButton* mute_btn_ = nullptr;
    finn::ui::FinnSettingRow* sound_row_ = nullptr;
    finn::ui::FinnSettingRow* battery_row_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    QString initial_tab;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-settings 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-settings [--page <name>] [--tab <name>]");
            return 0;
        }
        if ((std::strcmp(argv[i], "--page") == 0 || std::strcmp(argv[i], "--tab") == 0) &&
            i + 1 < argc) {
            initial_tab = QString::fromUtf8(argv[++i]);
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("settings"),
                                  QStringLiteral("Settings"));
    SettingsWindow win(initial_tab);
    win.show();
    return app.exec();
}
