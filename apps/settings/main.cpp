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
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
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
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <cstring>
#include <functional>

#include "common/paths.hpp"
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

struct CpuProbeResult {
    StatParts stats;
    bool exact_model_found = false;
};

CpuProbeResult read_cpu_model_sync() {
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
    const bool exact_found = !primary.isEmpty();
    if (primary.isEmpty()) {
        primary = QSysInfo::currentCpuArchitecture();
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
        const int cores = qMax(1, QThread::idealThreadCount());
        suffix = cores == 1 ? QStringLiteral("1 core")
                            : QStringLiteral("%1 cores").arg(cores);
    }
    return {{primary, suffix}, exact_found};
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
    AccentSwatchButton(const QString& name, const QColor& color, QWidget* parent = nullptr)
        : QAbstractButton(parent),
          hex_(color.name(QColor::HexRgb).toUpper()),
          color_(color) {
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("%1 (%2)").arg(name, hex_));
        setAccessibleName(QStringLiteral("%1 accent color").arg(name));
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

        // One accent for the live connectivity rows, one neutral for the rest.
        const QColor accent_light = finn::ui::nav_accent_light();
        const QColor accent_dark = finn::ui::nav_accent_dark();
        const QColor neutral_light = finn::ui::nav_neutral_light();
        const QColor neutral_dark = finn::ui::nav_neutral_dark();

        sidebar_ = new finn::ui::FinnSidebar(250, this);
        sidebar_->setHeaderTitle(QStringLiteral("Settings"));
        apply_header_16px_regular(sidebar_->headerLabel());
        sidebar_->setSearchEnabled(true, QStringLiteral("Search"));

        sidebar_->addItem(QStringLiteral("wifi"), QStringLiteral("wifi-02"),
                          QStringLiteral("Wi-Fi"), accent_light, accent_dark);
        sidebar_->addItem(QStringLiteral("bluetooth"), QStringLiteral("bluetooth"),
                          QStringLiteral("Bluetooth"), accent_light, accent_dark);
        sidebar_->addItem(QStringLiteral("network"), QStringLiteral("internet"),
                          QStringLiteral("Network"), accent_light, accent_dark);
        sidebar_->addSectionSpacer(8);

        sidebar_->addItem(QStringLiteral("general"), QStringLiteral("sliders"),
                          QStringLiteral("General"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("appearance"), QStringLiteral("paint-brush"),
                          QStringLiteral("Appearance"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("displays"), QStringLiteral("display"),
                          QStringLiteral("Display"), accent_light, accent_dark);
        sidebar_->addItem(QStringLiteral("sound"), QStringLiteral("volume-02"),
                          QStringLiteral("Sound"), accent_light, accent_dark);
        sidebar_->addSectionSpacer(8);

        sidebar_->addItem(QStringLiteral("accessibility"), QStringLiteral("accessibility"),
                          QStringLiteral("Accessibility"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("accounts"), QStringLiteral("user"),
                          QStringLiteral("Accounts"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("privacy"), QStringLiteral("shield"),
                          QStringLiteral("Privacy & Security"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("updates"), QStringLiteral("update"),
                          QStringLiteral("Updates"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("compat"), QStringLiteral("window"),
                          QStringLiteral("Compatibility"), neutral_light, neutral_dark);
        sidebar_->addItem(QStringLiteral("about"), QStringLiteral("info"),
                          QStringLiteral("About"), accent_light, accent_dark);
        sidebar_->addStretch();
        setSidebarWidget(sidebar_);

        stack_ = new finn::ui::FinnPageStack(this);
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
                    if (page_indices_.contains(id)) {
                        stack_->setCurrentIndex(page_indices_.value(id));
                        if (id == QLatin1String("updates")) refreshOtaStatus();
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
        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::commandError, this,
                [this](const QString& msg) { showToast(msg); });
        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::wifiConnectFinished, this,
                [this](bool ok, const QString&, const QString& msg) {
                    if (ok) showToast(msg);
                });
        refreshLiveHardware();
        QTimer::singleShot(150, this, &SettingsWindow::refreshOtaStatus);
    }

private:
    void selectPage(const QString& id) {
        if (!page_indices_.contains(id)) return;
        sidebar_->setCurrentId(id);
        stack_->setCurrentIndex(page_indices_.value(id));
        if (id == QLatin1String("updates")) refreshOtaStatus();
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        for (const auto& preset : finn::ui::accent_presets()) {
            const QString hex = preset.color.name(QColor::HexRgb).toUpper();
            auto* sw = new AccentSwatchButton(preset.name, preset.color, accent_row);
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

        auto* lock_wp_btns = new QWidget(page);
        auto* lock_wlay = new QHBoxLayout(lock_wp_btns);
        lock_wlay->setContentsMargins(0, 0, 0, 0);
        lock_wlay->setSpacing(6);
        auto* choose_lock_wp = new finn::ui::FinnButton(
            QStringLiteral("Choose Image..."), finn::ui::FinnButton::Variant::Secondary, {}, lock_wp_btns);
        connect(choose_lock_wp, &QAbstractButton::clicked, this, [this]() {
            const QString file = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select Lock Screen Wallpaper"), QDir::homePath(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
            if (!file.isEmpty()) {
                finn::ui::FinnThemeManager::instance().setLockWallpaper(file);
                showToast(QStringLiteral("Lock screen wallpaper updated"));
            }
        });
        auto* reset_lock_wp = new finn::ui::FinnButton(
            QStringLiteral("Default"), finn::ui::FinnButton::Variant::Ghost, {}, lock_wp_btns);
        connect(reset_lock_wp, &QAbstractButton::clicked, this, [this]() {
            finn::ui::FinnThemeManager::instance().setLockWallpaper({});
            showToast(QStringLiteral("Restored the default lock screen wallpaper"));
        });
        lock_wlay->addWidget(choose_lock_wp);
        lock_wlay->addWidget(reset_lock_wp);
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Lock Screen Wallpaper"),
            QStringLiteral("Full-screen image behind the lock screen; hidden until you unlock"),
            QStringLiteral("lock"), lock_wp_btns, page));

        root->addStretch(1);
        return page;
    }

    QWidget* buildDisplaysPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("Accessibility"), page));

        const auto& s = finn::ui::FinnThemeManager::instance().settings();

        const bool has_orca =
            !QStandardPaths::findExecutable(QStringLiteral("orca")).isEmpty();
        auto* sr_toggle = new finn::ui::FinnToggle(has_orca && s.screen_reader, page);
        sr_toggle->setEnabled(has_orca);
        sr_toggle->setAccessibleName(QStringLiteral("Screen Reader"));
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
            has_orca ? QStringLiteral("Spoken UI navigation via Orca & AT-SPI2")
                     : QStringLiteral("Orca screen reader is not installed on this system"),
            QStringLiteral("volume-02"), sr_toggle, page));

        auto* hc_toggle = new finn::ui::FinnToggle(s.high_contrast, page);
        hc_toggle->setAccessibleName(QStringLiteral("High Contrast"));
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
        cf_box->setAccessibleName(QStringLiteral("Color Filter"));
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
            showToast(QStringLiteral("Color filter preference saved: %1").arg(cf_box->currentText()));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Color Filters"),
            QStringLiteral("Saved preference (compositor color-matrix shader not yet wired)"),
            QStringLiteral("paint-brush"), cf_box, page));

        auto* ts_slider = new finn::ui::FinnSlider(85, 175, qRound(s.text_scale * 100), page);
        ts_slider->setFixedWidth(160);
        ts_slider->setAccessibleName(QStringLiteral("Text Scaling"));
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
        motion_toggle->setAccessibleName(QStringLiteral("Reduced Motion"));
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
        sticky_toggle->setAccessibleName(QStringLiteral("Sticky Keys"));
        connect(sticky_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setStickyKeys(on);
            showToast(on ? QStringLiteral("Sticky Keys preference saved")
                         : QStringLiteral("Sticky Keys disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Sticky Keys"),
            QStringLiteral("Saved preference (compositor XKB sequential modifiers not yet wired)"),
            QStringLiteral("keyboard"), sticky_toggle, page));

        auto* filter_toggle = new finn::ui::FinnToggle(s.filter_keys, page);
        filter_toggle->setAccessibleName(QStringLiteral("Filter Keys"));
        connect(filter_toggle, &QAbstractButton::toggled, this, [this](bool on) {
            finn::ui::FinnThemeManager::instance().setFilterKeys(on);
            showToast(on ? QStringLiteral("Filter Keys preference saved")
                         : QStringLiteral("Filter Keys disabled"));
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Filter Keys"),
            QStringLiteral("Saved preference (compositor key-bounce filter not yet wired)"),
            QStringLiteral("keyboard"), filter_toggle, page));

        auto* cur_box = new QComboBox(page);
        cur_box->setAccessibleName(QStringLiteral("Pointer Size"));
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
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
        root->setContentsMargins(12, 8, 12, 14);
        root->setSpacing(10);
        root->addWidget(makePageHeader(QStringLiteral("System Updates"), page));

        ota_banner_badge_ = new finn::ui::FinnBadge(
            QStringLiteral("Info"), finn::ui::FinnBadge::Tone::Neutral, page);
        ota_banner_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("OTA Recovery & Slot Status"),
            QStringLiteral("Checking system slots..."),
            QStringLiteral("shield"), ota_banner_badge_, page);
        ota_banner_row_->setVisible(false);
        root->addWidget(ota_banner_row_);

        ota_slot_badge_ = new finn::ui::FinnBadge(
            QStringLiteral("Checking"), finn::ui::FinnBadge::Tone::Neutral, page);
        ota_status_row_ = new finn::ui::FinnSettingRow(
            QStringLiteral("FinnOS System Image (OTA)"),
            QStringLiteral("Querying finn-update status..."),
            QStringLiteral("update"), ota_slot_badge_, page);
        root->addWidget(ota_status_row_);

        ota_channel_box_ = new QComboBox(page);
        ota_channel_box_->setAccessibleName(QStringLiteral("Update Channel"));
        ota_channel_box_->addItem(QStringLiteral("Stable"), QStringLiteral("stable"));
        ota_channel_box_->addItem(QStringLiteral("Beta"), QStringLiteral("beta"));
        ota_channel_box_->addItem(QStringLiteral("Dev"), QStringLiteral("dev"));
        connect(ota_channel_box_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            const QString ch = ota_channel_box_->itemData(idx).toString();
            runOtaCommand(
                QStringLiteral("config"),
                {QStringLiteral("config"), QStringLiteral("--channel"), ch,
                 QStringLiteral("--json")},
                [this, ch](int code, const QJsonObject& obj, const QString& err) {
                    if (code != 0) {
                        showToast(err.isEmpty() ? QStringLiteral("Failed to update channel") : err);
                        refreshOtaStatus();
                        return;
                    }
                    Q_UNUSED(obj);
                    showToast(QStringLiteral("Update channel set to %1").arg(ch));
                    refreshOtaStatus();
                });
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Release channel"),
            QStringLiteral("Stable, Beta, or Dev"),
            QStringLiteral("internet"), ota_channel_box_, page));

        ota_auto_check_toggle_ = new finn::ui::FinnToggle(true, page);
        ota_auto_check_toggle_->setAccessibleName(QStringLiteral("Automatic checks"));
        connect(ota_auto_check_toggle_, &QAbstractButton::toggled, this, [this](bool on) {
            runOtaCommand(
                QStringLiteral("config"),
                {QStringLiteral("config"), QStringLiteral("--auto-check"),
                 on ? QStringLiteral("true") : QStringLiteral("false"),
                 QStringLiteral("--json")},
                [this, on](int code, const QJsonObject&, const QString& err) {
                    if (code != 0) {
                        showToast(err.isEmpty() ? QStringLiteral("Failed to save setting") : err);
                        refreshOtaStatus();
                        return;
                    }
                    showToast(on ? QStringLiteral("Automatic checks enabled")
                                 : QStringLiteral("Automatic checks disabled"));
                });
        });
        root->addWidget(new finn::ui::FinnSettingRow(
            QStringLiteral("Automatic checks"),
            QStringLiteral("Check for updates daily"),
            QStringLiteral("update"), ota_auto_check_toggle_, page));

        auto* action_row = new QWidget(page);
        auto* alay = new QHBoxLayout(action_row);
        alay->setContentsMargins(0, 0, 0, 0);
        alay->setSpacing(8);

        ota_check_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Check for Updates"), finn::ui::FinnButton::Variant::Primary,
            QStringLiteral("update"), action_row);
        ota_check_btn_->setAccessibleName(QStringLiteral("Check for System Updates"));
        connect(ota_check_btn_, &QAbstractButton::clicked, this, [this]() { runOtaCheck(); });

        ota_download_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Download"), finn::ui::FinnButton::Variant::Secondary,
            {}, action_row);
        ota_download_btn_->setAccessibleName(QStringLiteral("Download System Update"));
        ota_download_btn_->setEnabled(false);
        connect(ota_download_btn_, &QAbstractButton::clicked, this, [this]() { runOtaDownload(); });

        ota_apply_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Install & Reboot"), finn::ui::FinnButton::Variant::Primary,
            {}, action_row);
        ota_apply_btn_->setAccessibleName(QStringLiteral("Install System Update and Reboot"));
        ota_apply_btn_->setEnabled(false);
        connect(ota_apply_btn_, &QAbstractButton::clicked, this, [this]() { runOtaApply(); });

        ota_rollback_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Rollback"), finn::ui::FinnButton::Variant::Secondary,
            {}, action_row);
        ota_rollback_btn_->setAccessibleName(QStringLiteral("Rollback to Previous Slot"));
        ota_rollback_btn_->setEnabled(false);
        connect(ota_rollback_btn_, &QAbstractButton::clicked, this, [this]() {
            if (!finn::ui::FinnDialog::confirm(
                    this, QStringLiteral("Rollback System Slot"),
                    QStringLiteral("Switch the next boot back to the previous known-good system slot?"),
                    QStringLiteral("Rollback"), true)) {
                return;
            }
            runOtaCommand(
                QStringLiteral("rollback"),
                {QStringLiteral("rollback"), QStringLiteral("--reason"),
                 QStringLiteral("user-requested"), QStringLiteral("--json")},
                [this](int code, const QJsonObject& obj, const QString& err) {
                    if (code != 0) {
                        setOtaBanner(
                            finn::ui::FinnBadge::Tone::Danger, QStringLiteral("Error"),
                            QStringLiteral("Rollback failed"),
                            err.isEmpty() ? obj.value(QStringLiteral("error")).toString() : err);
                        showToast(QStringLiteral("Rollback failed"));
                        return;
                    }
                    const QString target = obj.value(QStringLiteral("target_slot")).toString().toUpper();
                    setOtaBanner(
                        finn::ui::FinnBadge::Tone::Warning, QStringLiteral("Rollback"),
                        QStringLiteral("Rollback Armed"),
                        QStringLiteral("Next boot will use Slot %1.").arg(target));
                    showToast(QStringLiteral("Rollback armed for Slot %1").arg(target));
                    refreshOtaStatus();
                });
        });

        alay->addWidget(ota_check_btn_);
        alay->addWidget(ota_download_btn_);
        alay->addWidget(ota_apply_btn_);
        alay->addWidget(ota_rollback_btn_);
        alay->addStretch(1);
        root->addWidget(action_row);

        auto* notes_card = new finn::ui::FinnCard(finn::ui::kRadiusLg, false, page);
        auto* nlay = new QVBoxLayout(notes_card);
        nlay->setContentsMargins(14, 12, 14, 12);
        nlay->setSpacing(6);
        nlay->addWidget(new finn::ui::FinnLabel(
            QStringLiteral("Release notes"),
            finn::ui::FinnLabel::Role::Headline, finn::ui::FinnLabel::Tone::Ink, notes_card));
        ota_notes_label_ = new finn::ui::FinnLabel(
            QStringLiteral("Not checked yet"),
            finn::ui::FinnLabel::Role::Body, finn::ui::FinnLabel::Tone::Dim, notes_card);
        ota_notes_label_->setWordWrap(true);
        nlay->addWidget(ota_notes_label_);
        root->addWidget(notes_card);

        auto* pkg_row = new finn::ui::FinnSettingRow(
            QStringLiteral("System packages"),
            QStringLiteral("Check pacman updates"),
            QStringLiteral("package"), nullptr, page);
        check_updates_btn_ = new finn::ui::FinnButton(
            QStringLiteral("Check Packages"), finn::ui::FinnButton::Variant::Secondary,
            QStringLiteral("update"), page);
        check_updates_btn_->setAccessibleName(QStringLiteral("Check Package Updates"));
        connect(check_updates_btn_, &QAbstractButton::clicked, this, [this, pkg_row]() {
            if (pacman_check_proc_) return;
            if (QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
                pkg_row->setSubtitle(QStringLiteral("pacman is not available on this host"));
                return;
            }
            check_updates_btn_->setEnabled(false);
            pkg_row->setSubtitle(QStringLiteral("Querying pacman package updates..."));
            pacman_check_proc_ = new QProcess(this);
            auto* timeout = new QTimer(pacman_check_proc_);
            timeout->setSingleShot(true);
            connect(timeout, &QTimer::timeout, this, [this, pkg_row]() {
                if (!pacman_check_proc_) return;
                pacman_check_proc_->kill();
                pkg_row->setSubtitle(QStringLiteral("Timed out querying pacman package updates"));
                showToast(QStringLiteral("Package check timed out"));
            });
            connect(pacman_check_proc_, &QProcess::finished, this,
                    [this, pkg_row, timeout](int, QProcess::ExitStatus) {
                        timeout->stop();
                        QProcess* p = pacman_check_proc_;
                        pacman_check_proc_ = nullptr;
                        check_updates_btn_->setEnabled(true);
                        const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                        p->deleteLater();
                        const int count = out.isEmpty() ? 0 : out.split(QLatin1Char('\n')).size();
                        pkg_row->setSubtitle(
                            count == 0
                                ? QStringLiteral("Packages are up to date")
                                : QStringLiteral("%1 package update(s) available — run sudo pacman -Syu in Finn Terminal")
                                      .arg(count));
                        showToast(count == 0 ? QStringLiteral("Packages up to date")
                                             : QStringLiteral("%1 package updates found").arg(count));
                    });
            connect(pacman_check_proc_, &QProcess::errorOccurred, this,
                    [this, pkg_row, timeout](QProcess::ProcessError) {
                        timeout->stop();
                        if (pacman_check_proc_) {
                            pacman_check_proc_->deleteLater();
                            pacman_check_proc_ = nullptr;
                        }
                        check_updates_btn_->setEnabled(true);
                        pkg_row->setSubtitle(QStringLiteral("Failed to run pacman"));
                    });
            timeout->start(4000);
            pacman_check_proc_->start(QStringLiteral("pacman"), {QStringLiteral("-Qu")});
        });
        pkg_row->setTrailingWidget(check_updates_btn_);
        root->addWidget(pkg_row);
        root->addStretch(1);
        return page;
    }

    QWidget* buildCompatPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(12, 8, 12, 14);
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
            QStringLiteral("Linux Applications (FinnOS / Flatpak / AppImage)"),
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
        root->setContentsMargins(12, 8, 12, 14);
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
        auto* storage_card = new finn::ui::FinnStatCard(
            QStringLiteral("Storage"), disk_primary, disk_suffix, page);
        storage_card->setFixedHeight(70);
        grid->addWidget(storage_card, 1, 1);

        const auto cpu_probe = read_cpu_model_sync();
        cpu_card_ = new finn::ui::FinnStatCard(
            QStringLiteral("CPU"), cpu_probe.stats.primary, cpu_probe.stats.suffix, page);
        cpu_card_->setFixedHeight(70);
        grid->addWidget(cpu_card_, 2, 0);
        if (!cpu_probe.exact_model_found &&
            !QStandardPaths::findExecutable(QStringLiteral("lscpu")).isEmpty()) {
            auto* lscpu_proc = new QProcess(this);
            connect(lscpu_proc, &QProcess::finished, this,
                    [this, lscpu_proc, fallback_suffix = cpu_probe.stats.suffix](
                        int code, QProcess::ExitStatus status) {
                        const QString out =
                            QString::fromUtf8(lscpu_proc->readAllStandardOutput());
                        lscpu_proc->deleteLater();
                        if (status != QProcess::NormalExit || code != 0 || !cpu_card_) return;
                        QString model;
                        QString vendor;
                        QString arch;
                        QString cores;
                        for (const QString& line : out.split(QLatin1Char('\n'))) {
                            if (line.startsWith(QStringLiteral("Model name:"))) {
                                const QString v = line.section(QLatin1Char(':'), 1).trimmed();
                                if (!v.isEmpty() && v != QLatin1String("-")) model = v;
                            } else if (line.startsWith(QStringLiteral("Vendor ID:"))) {
                                const QString v = line.section(QLatin1Char(':'), 1).trimmed();
                                if (!v.isEmpty() && v != QLatin1String("-")) vendor = v;
                            } else if (line.startsWith(QStringLiteral("Architecture:"))) {
                                arch = line.section(QLatin1Char(':'), 1).trimmed();
                            } else if (line.startsWith(QStringLiteral("CPU(s):"))) {
                                cores = line.section(QLatin1Char(':'), 1).trimmed();
                            }
                        }
                        QString primary = model;
                        QString suffix = fallback_suffix;
                        if (primary.contains(QStringLiteral(" @ "))) {
                            suffix = primary.section(QStringLiteral(" @ "), 1).trimmed();
                            primary = primary.section(QStringLiteral(" @ "), 0, 0).trimmed();
                        }
                        primary.remove(QStringLiteral("(R)"));
                        primary.remove(QStringLiteral("(TM)"));
                        primary.replace(QStringLiteral("  "), QStringLiteral(" "));
                        primary = primary.trimmed();
                        if (primary.isEmpty()) {
                            const QString eff_arch =
                                !arch.isEmpty() ? arch : QSysInfo::currentCpuArchitecture();
                            primary = !vendor.isEmpty()
                                          ? QStringLiteral("%1 %2").arg(vendor, eff_arch)
                                          : eff_arch;
                        }
                        bool ok = false;
                        const int ncores = cores.toInt(&ok);
                        if (suffix.isEmpty() && ok && ncores > 0) {
                            suffix = ncores == 1 ? QStringLiteral("1 core")
                                                 : QStringLiteral("%1 cores").arg(ncores);
                        }
                        cpu_card_->setValueParts(primary, suffix);
                    });
            lscpu_proc->start(QStringLiteral("lscpu"), {});
        }

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
        monitor_btn->setAccessibleName(QStringLiteral("Open System Monitor"));
        connect(monitor_btn, &QAbstractButton::clicked, this, [this]() {
            if (!QProcess::startDetached(QStringLiteral("finn-monitor"), {})) {
                showToast(QStringLiteral("System Monitor (finn-monitor) is not installed"));
            }
        });
        auto* updates_btn = new finn::ui::FinnButton(
            QStringLiteral("Check for Updates"),
            finn::ui::FinnButton::Variant::Secondary, {}, page);
        updates_btn->setAccessibleName(QStringLiteral("Check for Updates"));
        connect(updates_btn, &QAbstractButton::clicked, this, [this]() {
            selectPage(QStringLiteral("updates"));
            runOtaCheck();
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

        // Only rebuild Wi-Fi / Bluetooth rows when the network/device list changed (M19).
        QVector<WifiRowKey> next_wifi;
        next_wifi.reserve(net.networks.size());
        for (const auto& w : net.networks) {
            next_wifi.push_back({w.ssid, w.signal, w.active, w.secured});
        }
        if (next_wifi != last_wifi_snapshot_) {
            last_wifi_snapshot_ = next_wifi;
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
                cbtn->setAccessibleName(
                    QStringLiteral("%1 %2").arg(w.active ? QStringLiteral("Connected to")
                                                         : QStringLiteral("Connect to"),
                                                w.ssid));
                connect(cbtn, &QAbstractButton::clicked, this, [this, w]() {
                    QString pwd;
                    if (w.secured) {
                        pwd = finn::ui::FinnDialog::prompt(
                            this, QStringLiteral("Wi-Fi Password"),
                            QStringLiteral("Password for %1:").arg(w.ssid),
                            {}, true);
                        if (pwd.isEmpty()) return;
                    }
                    finn::sys::FinnDesktopServices::instance().connectWifi(w.ssid, pwd);
                    showToast(QStringLiteral("Connecting to %1...").arg(w.ssid));
                });
                wifi_list_layout_->addWidget(new finn::ui::FinnSettingRow(
                    w.ssid,
                    QStringLiteral("Signal %1%%2").arg(w.signal).arg(w.secured ? QStringLiteral("  |  Secured") : QString()),
                    QStringLiteral("wifi-02"), cbtn));
            }
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

        QVector<BtRowKey> next_bt;
        next_bt.reserve(bt.devices.size());
        for (const auto& d : bt.devices) {
            next_bt.push_back({d.address, d.name, d.connected});
        }
        if (next_bt != last_bt_snapshot_) {
            last_bt_snapshot_ = next_bt;
            while (QLayoutItem* item = bt_list_layout_->takeAt(0)) {
                if (QWidget* w = item->widget()) w->deleteLater();
                delete item;
            }
            for (const auto& d : bt.devices) {
                auto* bbtn = new finn::ui::FinnButton(
                    d.connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"),
                    finn::ui::FinnButton::Variant::Secondary, {});
                bbtn->setAccessibleName(
                    QStringLiteral("%1 %2").arg(d.connected ? QStringLiteral("Disconnect")
                                                            : QStringLiteral("Connect"),
                                                d.name));
                connect(bbtn, &QAbstractButton::clicked, this, [d]() {
                    finn::sys::FinnDesktopServices::instance().toggleBluetoothDevice(d.address, !d.connected);
                });
                bt_list_layout_->addWidget(new finn::ui::FinnSettingRow(
                    d.name, d.address, QStringLiteral("bluetooth"), bbtn));
            }
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

    struct WifiRowKey {
        QString ssid;
        int signal = 0;
        bool active = false;
        bool secured = false;
        bool operator==(const WifiRowKey&) const = default;
    };
    struct BtRowKey {
        QString address;
        QString name;
        bool connected = false;
        bool operator==(const BtRowKey&) const = default;
    };

    QString finnUpdateExecutable() const {
        const QString canonical = QString::fromUtf8(finn::kFinnUpdateBin);
        if (QFileInfo::exists(canonical) && QFileInfo(canonical).isExecutable()) {
            return canonical;
        }
        return QStandardPaths::findExecutable(QStringLiteral("finn-update"));
    }

    void setOtaBanner(finn::ui::FinnBadge::Tone tone, const QString& badge_text,
                      const QString& /*title*/, const QString& subtitle) {
        if (!ota_banner_row_ || !ota_banner_badge_) return;
        ota_banner_badge_->setTone(tone);
        ota_banner_badge_->setText(badge_text);
        ota_banner_row_->setSubtitle(subtitle);
        ota_banner_row_->setVisible(true);
    }

    void runOtaCommand(
        const QString& tag, const QStringList& args,
        std::function<void(int exit_code, const QJsonObject& obj, const QString& err)> on_done) {
        Q_UNUSED(tag);
        const QString exe = finnUpdateExecutable();
        if (exe.isEmpty()) {
            setOtaBanner(
                finn::ui::FinnBadge::Tone::Warning, QStringLiteral("Missing"),
                QStringLiteral("OTA Client Missing"),
                QStringLiteral("finn-update is not installed at %1")
                    .arg(QString::fromUtf8(finn::kFinnUpdateBin)));
            if (ota_status_row_) {
                ota_status_row_->setSubtitle(QStringLiteral("finn-update binary not installed"));
            }
            if (ota_slot_badge_) {
                ota_slot_badge_->setTone(finn::ui::FinnBadge::Tone::Neutral);
                ota_slot_badge_->setText(QStringLiteral("Unavailable"));
            }
            on_done(-1, {}, QStringLiteral("finn-update binary not found"));
            return;
        }
        auto* proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [proc, on_done = std::move(on_done)](int code, QProcess::ExitStatus status) {
                    const QByteArray stdout_raw = proc->readAllStandardOutput();
                    const QString stderr_str =
                        QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    proc->deleteLater();
                    QJsonParseError jerr;
                    const QJsonDocument doc = QJsonDocument::fromJson(stdout_raw, &jerr);
                    const QJsonObject obj =
                        (jerr.error == QJsonParseError::NoError && doc.isObject())
                            ? doc.object()
                            : QJsonObject{};
                    QString err = obj.value(QStringLiteral("error")).toString();
                    if (err.isEmpty()) err = stderr_str;
                    const int effective_code =
                        (status == QProcess::NormalExit) ? code : -1;
                    on_done(effective_code, obj, err);
                });
        connect(proc, &QProcess::errorOccurred, this,
                [proc, on_done](QProcess::ProcessError) {
                    const QString err = proc->errorString();
                    proc->deleteLater();
                    on_done(-1, {}, err);
                });
        proc->start(exe, args);
    }

    void refreshOtaStatus() {
        runOtaCommand(
            QStringLiteral("status"),
            {QStringLiteral("status"), QStringLiteral("--json")},
            [this](int code, const QJsonObject& obj, const QString& err) {
                if (code != 0 || obj.isEmpty()) {
                    if (ota_status_row_) {
                        ota_status_row_->setSubtitle(
                            err.isEmpty() ? QStringLiteral("Unable to query OTA state") : err);
                    }
                    return;
                }
                const QString cur_ver = obj.value(QStringLiteral("current_version")).toString();
                const QString ch = obj.value(QStringLiteral("channel")).toString();
                const QString backend = obj.value(QStringLiteral("backend")).toString();
                const QString layout = obj.value(QStringLiteral("layout")).toString();
                const QString active = obj.value(QStringLiteral("active_slot")).toString();
                const QString booted = obj.value(QStringLiteral("booted_slot")).toString();
                const QString pending = obj.value(QStringLiteral("pending_slot")).toString();
                const int tries = obj.value(QStringLiteral("tries_left")).toInt();
                const bool committed = obj.value(QStringLiteral("slot_committed")).toBool(true);
                const bool auto_chk = obj.value(QStringLiteral("auto_check")).toBool(true);
                const QString last_chk = obj.value(QStringLiteral("last_check")).toString();

                ota_layout_ = layout;
                if (ota_channel_box_) {
                    QSignalBlocker b(ota_channel_box_);
                    const int cidx = ota_channel_box_->findData(ch);
                    if (cidx >= 0) ota_channel_box_->setCurrentIndex(cidx);
                }
                if (ota_auto_check_toggle_) {
                    QSignalBlocker b(ota_auto_check_toggle_);
                    ota_auto_check_toggle_->setChecked(auto_chk);
                }

                const QJsonObject slots_obj = obj.value(QStringLiteral("slots")).toObject();
                const QString ver_a =
                    slots_obj.value(QStringLiteral("a")).toObject().value(QStringLiteral("version")).toString();
                const QString ver_b =
                    slots_obj.value(QStringLiteral("b")).toObject().value(QStringLiteral("version")).toString();

                const QString gh_repo = obj.value(QStringLiteral("github_repo")).toString();
                const QString feed_label = gh_repo.isEmpty()
                    ? QStringLiteral("GitHub Releases")
                    : QStringLiteral("%1 (GitHub Releases)").arg(gh_repo);

                if (layout == QLatin1String("single")) {
                    ota_slot_badge_->setTone(finn::ui::FinnBadge::Tone::Warning);
                    ota_slot_badge_->setText(QStringLiteral("Single Slot"));
                    ota_status_row_->setSubtitle(
                        QStringLiteral("FinnOS %1 · %2 · %3")
                            .arg(cur_ver, ch, feed_label));
                    if (ota_banner_row_) ota_banner_row_->setVisible(false);
                    ota_rollback_btn_->setEnabled(false);
                } else {
                    const QString slot_summary =
                        QStringLiteral("FinnOS %1 · Active Slot %2 (Booted %3 · A:%4 B:%5) · Last check: %6")
                            .arg(cur_ver,
                                 active.isEmpty() ? QStringLiteral("-") : active.toUpper(),
                                 booted.isEmpty() ? QStringLiteral("-") : booted.toUpper(),
                                 ver_a.isEmpty() ? QStringLiteral("empty") : ver_a,
                                 ver_b.isEmpty() ? QStringLiteral("empty") : ver_b,
                                 last_chk.isEmpty() ? QStringLiteral("never") : last_chk);
                    ota_status_row_->setSubtitle(slot_summary);

                    if (!pending.isEmpty() && !committed) {
                        ota_slot_badge_->setTone(finn::ui::FinnBadge::Tone::Warning);
                        ota_slot_badge_->setText(QStringLiteral("Trial Boot"));
                        setOtaBanner(
                            finn::ui::FinnBadge::Tone::Warning, QStringLiteral("Pending"),
                            QStringLiteral("Trial Boot Active"),
                            QStringLiteral("Slot %1 is pending health commit (%2 boot tries remaining). Roll back if unstable.")
                                .arg(pending.toUpper())
                                .arg(tries));
                    } else {
                        ota_slot_badge_->setTone(finn::ui::FinnBadge::Tone::Success);
                        ota_slot_badge_->setText(QStringLiteral("Healthy"));
                        if (ota_banner_row_) ota_banner_row_->setVisible(false);
                    }
                    const bool can_rollback =
                        !pending.isEmpty() ||
                        (!ver_a.isEmpty() && !ver_b.isEmpty());
                    ota_rollback_btn_->setEnabled(can_rollback);
                }

                const QJsonValue avail_val = obj.value(QStringLiteral("available_update"));
                if (avail_val.isObject()) {
                    const QJsonObject avail = avail_val.toObject();
                    const QString tver = avail.value(QStringLiteral("version")).toString();
                    const bool elig = avail.value(QStringLiteral("eligible")).toBool();
                    const qint64 sz = avail.value(QStringLiteral("size")).toInteger();
                    const QString kind = avail.value(QStringLiteral("kind")).toString();
                    ota_update_eligible_ = elig;
                    ota_download_btn_->setEnabled(elig);
                    ota_apply_btn_->setEnabled(elig && layout == QLatin1String("ab"));
                    ota_notes_label_->setText(
                        QStringLiteral("Cached update: FinnOS %1 (%2, %3)\nSize: %4")
                            .arg(tver, ch, kind,
                                 QLocale().formattedDataSize(sz)));
                }
            });
    }

    void runOtaCheck() {
        if (ota_busy_) return;
        ota_busy_ = true;
        ota_check_btn_->setEnabled(false);
        ota_status_row_->setSubtitle(QStringLiteral("Checking signed OTA manifest..."));
        runOtaCommand(
            QStringLiteral("check"),
            {QStringLiteral("check"), QStringLiteral("--json")},
            [this](int code, const QJsonObject& obj, const QString& err) {
                ota_busy_ = false;
                ota_check_btn_->setEnabled(true);
                if (code != 0) {
                    setOtaBanner(
                        finn::ui::FinnBadge::Tone::Danger, QStringLiteral("Check Failed"),
                        QStringLiteral("Update Check Error"),
                        err.isEmpty() ? QStringLiteral("Manifest check failed (exit %1)").arg(code)
                                      : err);
                    ota_download_btn_->setEnabled(false);
                    ota_apply_btn_->setEnabled(false);
                    showToast(QStringLiteral("OTA check failed"));
                    return;
                }
                const QString status = obj.value(QStringLiteral("status")).toString();
                const QString tver = obj.value(QStringLiteral("target_version")).toString();
                const QString rnotes = obj.value(QStringLiteral("release_notes")).toString();
                const QString reason = obj.value(QStringLiteral("reason")).toString();
                const int rollout = obj.value(QStringLiteral("rollout")).toInt(100);
                const int bucket = obj.value(QStringLiteral("machine_id_bucket")).toInt(-1);
                const bool elig = obj.value(QStringLiteral("eligible")).toBool();
                const QJsonObject art = obj.value(QStringLiteral("selected_artifact")).toObject();

                ota_update_eligible_ = elig;
                ota_download_btn_->setEnabled(elig);
                ota_apply_btn_->setEnabled(elig && ota_layout_ == QLatin1String("ab"));

                if (status == QLatin1String("up_to_date")) {
                    ota_notes_label_->setText(
                        QStringLiteral("FinnOS %1 is up to date.")
                            .arg(obj.value(QStringLiteral("current_version")).toString()));
                    showToast(QStringLiteral("FinnOS is up to date"));
                } else {
                    const qint64 sz = art.value(QStringLiteral("size")).toInteger();
                    const QString kind = art.value(QStringLiteral("delta_from")).isNull()
                                             ? QStringLiteral("full")
                                             : QStringLiteral("delta");
                    QString summary = QStringLiteral("FinnOS %1 (%2 · %3 · rollout %4%, bucket %5)")
                                          .arg(tver, kind, QLocale().formattedDataSize(sz))
                                          .arg(rollout)
                                          .arg(bucket);
                    if (!elig && !reason.isEmpty()) {
                        summary += QStringLiteral("\nStatus: %1 (%2)").arg(status, reason);
                    }
                    if (!rnotes.isEmpty()) {
                        summary += QStringLiteral("\n\n%1").arg(rnotes);
                    }
                    ota_notes_label_->setText(summary);
                    showToast(elig ? QStringLiteral("Update %1 available").arg(tver)
                                   : QStringLiteral("Update %1 deferred (%2)").arg(tver, reason));
                }
                refreshOtaStatus();
            });
    }

    void runOtaDownload() {
        if (ota_busy_) return;
        ota_busy_ = true;
        ota_download_btn_->setEnabled(false);
        ota_apply_btn_->setEnabled(false);
        showToast(QStringLiteral("Downloading verified update..."));
        runOtaCommand(
            QStringLiteral("download"),
            {QStringLiteral("download"), QStringLiteral("--json")},
            [this](int code, const QJsonObject& obj, const QString& err) {
                ota_busy_ = false;
                ota_download_btn_->setEnabled(ota_update_eligible_);
                ota_apply_btn_->setEnabled(ota_update_eligible_ && ota_layout_ == QLatin1String("ab"));
                if (code != 0) {
                    setOtaBanner(
                        finn::ui::FinnBadge::Tone::Danger, QStringLiteral("Download Failed"),
                        QStringLiteral("Download Error"),
                        err.isEmpty() ? QStringLiteral("Download failed (exit %1)").arg(code)
                                      : err);
                    showToast(QStringLiteral("Update download failed"));
                    return;
                }
                const QString ver = obj.value(QStringLiteral("version")).toString();
                const qint64 resumed = obj.value(QStringLiteral("resumed_from")).toInteger();
                showToast(resumed > 0
                              ? QStringLiteral("Downloaded FinnOS %1 (resumed from %2)")
                                    .arg(ver, QLocale().formattedDataSize(resumed))
                              : QStringLiteral("Downloaded and verified FinnOS %1").arg(ver));
            });
    }

    void runOtaApply() {
        if (ota_busy_) return;
        ota_busy_ = true;
        ota_check_btn_->setEnabled(false);
        ota_download_btn_->setEnabled(false);
        ota_apply_btn_->setEnabled(false);
        showToast(QStringLiteral("Staging update to inactive slot..."));
        runOtaCommand(
            QStringLiteral("apply"),
            {QStringLiteral("apply"), QStringLiteral("--json")},
            [this](int code, const QJsonObject& obj, const QString& err) {
                ota_busy_ = false;
                ota_check_btn_->setEnabled(true);
                ota_download_btn_->setEnabled(ota_update_eligible_);
                ota_apply_btn_->setEnabled(ota_update_eligible_ && ota_layout_ == QLatin1String("ab"));
                if (code != 0) {
                    setOtaBanner(
                        finn::ui::FinnBadge::Tone::Danger, QStringLiteral("Install Failed"),
                        QStringLiteral("OTA Staging Error"),
                        err.isEmpty() ? QStringLiteral("Apply failed (exit %1)").arg(code)
                                      : err);
                    showToast(QStringLiteral("System update install failed"));
                    return;
                }
                const QString target = obj.value(QStringLiteral("target_version")).toString();
                const QString slot = obj.value(QStringLiteral("staged_slot")).toString().toUpper();
                setOtaBanner(
                    finn::ui::FinnBadge::Tone::Accent, QStringLiteral("Reboot Ready"),
                    QStringLiteral("Update Staged"),
                    QStringLiteral("FinnOS %1 staged to Slot %2. Reboot to test and commit.")
                        .arg(target, slot));
                refreshOtaStatus();
                if (finn::ui::FinnDialog::confirm(
                        this, QStringLiteral("Restart to Complete Update"),
                        QStringLiteral("FinnOS %1 is staged on Slot %2. Restart now?")
                            .arg(target, slot),
                        QStringLiteral("Restart Now"))) {
                    finn::sys::FinnDesktopServices::instance().triggerPowerAction(
                        QStringLiteral("reboot"));
                }
            });
    }

    finn::ui::FinnSidebar* sidebar_ = nullptr;
    finn::ui::FinnPageStack* stack_ = nullptr;
    QMap<QString, int> page_indices_;
    finn::ui::FinnButton* check_updates_btn_ = nullptr;
    QProcess* pacman_check_proc_ = nullptr;

    finn::ui::FinnSettingRow* ota_banner_row_ = nullptr;
    finn::ui::FinnBadge* ota_banner_badge_ = nullptr;
    finn::ui::FinnSettingRow* ota_status_row_ = nullptr;
    finn::ui::FinnBadge* ota_slot_badge_ = nullptr;
    QComboBox* ota_channel_box_ = nullptr;
    finn::ui::FinnToggle* ota_auto_check_toggle_ = nullptr;
    finn::ui::FinnButton* ota_check_btn_ = nullptr;
    finn::ui::FinnButton* ota_download_btn_ = nullptr;
    finn::ui::FinnButton* ota_apply_btn_ = nullptr;
    finn::ui::FinnButton* ota_rollback_btn_ = nullptr;
    finn::ui::FinnLabel* ota_notes_label_ = nullptr;
    QString ota_layout_;
    bool ota_update_eligible_ = false;
    bool ota_busy_ = false;

    finn::ui::FinnStatCard* cpu_card_ = nullptr;
    finn::ui::FinnSlider* brightness_slider_ = nullptr;
    finn::ui::FinnSettingRow* brightness_row_ = nullptr;
    finn::ui::FinnToggle* wifi_toggle_ = nullptr;
    finn::ui::FinnSettingRow* wifi_row_ = nullptr;
    finn::ui::FinnSettingRow* net_detail_row_ = nullptr;
    QVBoxLayout* wifi_list_layout_ = nullptr;
    QVector<WifiRowKey> last_wifi_snapshot_;
    finn::ui::FinnToggle* bt_toggle_ = nullptr;
    finn::ui::FinnSettingRow* bt_row_ = nullptr;
    QVBoxLayout* bt_list_layout_ = nullptr;
    QVector<BtRowKey> last_bt_snapshot_;
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
