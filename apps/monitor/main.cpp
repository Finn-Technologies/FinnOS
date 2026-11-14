// Finn Monitor: system telemetry & process manager redesigned to match Figma Settings (20:64) 1:1.
// Uses ChromeMode::FigmaSplit, 250px FinnSidebar with 26x26 circular colored badges,
// (12,64) process filter pill, Settings About-style FinnHeroCard + 2x2 FinnStatCards,
// and a clean borderless Settings-style r=6 process list card.

#include <FinnSDK/FinnSDK.hpp>

#include "system/sys_files.hpp"

#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <csignal>
#include <cstring>
#include <deque>
#include <unordered_map>

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
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
               "QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }"
               "QScrollBar::handle:horizontal { background: %1; border-radius: 4px; min-width: 28px; }"
               "QScrollBar::handle:horizontal:hover { background: %2; }"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }")
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

QString read_first_line(const char* path) {
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

QString format_uptime() {
    const QString line = read_first_line("/proc/uptime");
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QStringLiteral("Unavailable");
    bool ok = false;
    const double secs_d = parts[0].toDouble(&ok);
    if (!ok) return QStringLiteral("Unavailable");
    const qint64 secs = static_cast<qint64>(secs_d);
    const qint64 days = secs / 86400;
    const qint64 hrs = (secs % 86400) / 3600;
    const qint64 mins = (secs % 3600) / 60;
    if (days > 0) return QStringLiteral("%1d %2h %3m").arg(days).arg(hrs).arg(mins);
    if (hrs > 0) return QStringLiteral("%1h %2m").arg(hrs).arg(mins);
    return QStringLiteral("%1m %2s").arg(mins).arg(secs % 60);
}

QString format_kib(std::uint64_t kib) {
    if (kib >= 1048576ULL) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(kib) / 1048576.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(static_cast<double>(kib) / 1024.0, 0, 'f', 0);
}

void read_swap_kib(std::uint64_t* total_kib, std::uint64_t* free_kib) {
    *total_kib = 0;
    *free_kib = 0;
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("SwapTotal:"))) {
            const auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) *total_kib = parts[1].toULongLong();
        } else if (line.startsWith(QLatin1String("SwapFree:"))) {
            const auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) *free_kib = parts[1].toULongLong();
        }
    }
}

class SparklineWidget final : public QWidget {
public:
    explicit SparklineWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(64);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void pushSample(double pct) {
        samples_.push_back(qBound(0.0, pct, 100.0));
        while (samples_.size() > 60) samples_.pop_front();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto& t = FinnThemeManager::instance().tokens();

        const QRectF r = QRectF(rect()).adjusted(1, 2, -1, -2);
        p.setPen(QPen(t.hair, 1.0));
        p.setBrush(t.surface_canvas);
        p.drawRoundedRect(r, kRadiusMd, kRadiusMd);

        if (samples_.size() < 2) return;

        QPainterPath line_path;
        QPainterPath fill_path;
        const qsizetype n = static_cast<qsizetype>(samples_.size());
        const double dx = r.width() / 59.0;
        const double start_x = r.right() - (n - 1) * dx;

        for (qsizetype i = 0; i < n; ++i) {
            const double x = start_x + i * dx;
            const double ratio = samples_[static_cast<std::size_t>(i)] / 100.0;
            const double y = r.bottom() - 4.0 - ratio * (r.height() - 8.0);
            if (i == 0) {
                line_path.moveTo(x, y);
                fill_path.moveTo(x, r.bottom() - 1.0);
                fill_path.lineTo(x, y);
            } else {
                line_path.lineTo(x, y);
                fill_path.lineTo(x, y);
            }
        }
        fill_path.lineTo(r.right(), r.bottom() - 1.0);
        fill_path.closeSubpath();

        QColor fill_col = t.accent;
        fill_col.setAlpha(46);
        p.fillPath(fill_path, fill_col);
        p.setPen(QPen(t.accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line_path);
    }

private:
    std::deque<double> samples_;
};

class ProcessTreeItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 0 || col == 3 || col == 4) {
            return data(col, Qt::UserRole).toDouble() < other.data(col, Qt::UserRole).toDouble();
        }
        return text(col).compare(other.text(col), Qt::CaseInsensitive) < 0;
    }
};

class MonitorWindow final : public FinnWindow {
    Q_OBJECT
public:
    MonitorWindow()
        : FinnWindow(QStringLiteral("Finn Monitor"), QStringLiteral("monitor")) {
        setMinimumSize(800, 500);
        resize(940, 620);
        setChromeMode(ChromeMode::FigmaSplit);

        setupSidebar();
        setupContent();

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, [this](const FinnTokens&) { applyTreeStyle(); });

        applyTreeStyle();
        refreshDisksPage();
        poll();
        QTimer::singleShot(250, this, [this]() { poll(); });

        auto* timer = new QTimer(this);
        timer->setInterval(1200);
        connect(timer, &QTimer::timeout, this, [this]() { poll(); });
        timer->start();
    }

private:
    void setupSidebar() {
        sidebar_ = new FinnSidebar(250, this);
        sidebar_->setHeaderTitle(QStringLiteral("Monitor"));
        apply_header_16px_regular(sidebar_->headerLabel());
        sidebar_->setSearchEnabled(true, QStringLiteral("Filter processes"));

        if (sidebar_->searchField()) {
            sidebar_->searchField()->disconnect(sidebar_);
            connect(sidebar_->searchField(), &FinnSearchField::textChanged,
                    this, [this](const QString& q) {
                        if (!q.trimmed().isEmpty() && stack_->currentIndex() > 0) {
                            switchPage(QStringLiteral("processes"));
                            sidebar_->setCurrentId(QStringLiteral("processes"));
                        }
                        applyFilter();
                    });
        }

        sidebar_->addItem(QStringLiteral("overview"), QStringLiteral("monitor"),
                          QStringLiteral("System Overview"), QColor(0x00, 0x7B, 0xFF));
        sidebar_->addItem(QStringLiteral("processes"), QStringLiteral("settings-02"),
                          QStringLiteral("Processes"), QColor(0x22, 0xC5, 0x5E));
        sidebar_->addItem(QStringLiteral("performance"), QStringLiteral("sun-01"),
                          QStringLiteral("Performance"), QColor(0xF5, 0x9E, 0x0B));
        sidebar_->addItem(QStringLiteral("disks"), QStringLiteral("folder"),
                          QStringLiteral("Storage & Disks"), QColor(0x58, 0x56, 0xD6));
        sidebar_->addStretch();
        setSidebarWidget(sidebar_);

        connect(sidebar_, &FinnSidebar::selected, this,
                [this](const QString& id) { switchPage(id); });
    }

    void setupContent() {
        auto* content = new QWidget(this);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(12, 24, 12, 14);
        root->setSpacing(10);

        // Top header row (16px Regular title + live CPU/RAM summary pill +
        // Refresh / End Process button to the left of the 96px floating FinnWindowControls reserve)
        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(8);

        page_title_ = new FinnLabel(
            QStringLiteral("System Overview"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, content);
        apply_header_16px_regular(page_title_);

        summary_pill_ = new FinnBadge(
            QStringLiteral("CPU 0% · RAM 0%"), FinnBadge::Tone::Accent, content);

        auto* refresh_btn = new FinnButton(
            QStringLiteral("Refresh"), FinnButton::Variant::Secondary,
            QStringLiteral("refresh"), content);
        refresh_btn->setFixedHeight(28);

        end_btn_ = new FinnButton(
            QStringLiteral("End Process"), FinnButton::Variant::Danger,
            {}, content);
        end_btn_->setFixedHeight(28);
        end_btn_->setEnabled(false);

        header_row->addWidget(page_title_);
        header_row->addSpacing(4);
        header_row->addWidget(summary_pill_);
        header_row->addStretch(1);
        header_row->addWidget(refresh_btn);
        header_row->addWidget(end_btn_);
        root->addLayout(header_row);

        stack_ = new QStackedWidget(content);
        finn::ui::install_stack_animator(stack_);
        stack_->addWidget(buildOverviewAndProcessesPage());  // 0: overview & processes
        stack_->addWidget(buildPerformancePage());           // 1: performance
        stack_->addWidget(buildDisksPage());                 // 2: disks
        root->addWidget(stack_, 1);

        setContentWidget(content);

        connect(refresh_btn, &QAbstractButton::clicked, this, [this]() {
            poll();
            refreshDisksPage();
        });
        connect(end_btn_, &QAbstractButton::clicked, this, [this]() { endSelectedProcess(); });
        sidebar_->setCurrentId(QStringLiteral("overview"));
    }

    QWidget* buildOverviewAndProcessesPage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 2, 0, 0);
        root->setSpacing(10);

        // Figma Settings About-style FinnHeroCard (258x150) + 2x2 FinnStatCards (258x70, r=6)
        overview_header_wrap_ = new QWidget(page);
        auto* ov_lay = new QVBoxLayout(overview_header_wrap_);
        ov_lay->setContentsMargins(0, 0, 0, 0);
        ov_lay->setSpacing(10);

        auto* top_row = new QHBoxLayout();
        top_row->setContentsMargins(0, 0, 0, 0);
        top_row->setSpacing(10);

        hero_card_ = new FinnHeroCard(
            QStringLiteral("FinnOS "),
            QStringLiteral("Linux %1 · %2")
                .arg(QSysInfo::kernelVersion().section(QLatin1Char('-'), 0, 0),
                     QSysInfo::currentCpuArchitecture()),
            QStringLiteral("boot-mark"), overview_header_wrap_);
        hero_card_->setInlineSuffix(QStringLiteral("Monitor"));
        hero_card_->setFixedHeight(150);
        top_row->addWidget(hero_card_, 1);

        auto* stats_grid = new QGridLayout();
        stats_grid->setContentsMargins(0, 0, 0, 0);
        stats_grid->setHorizontalSpacing(10);
        stats_grid->setVerticalSpacing(10);
        stats_grid->setColumnStretch(0, 1);
        stats_grid->setColumnStretch(1, 1);

        cpu_stat_card_ = new FinnStatCard(
            QStringLiteral("CPU"), QStringLiteral("0.0%"),
            QStringLiteral("%1 cores").arg(qMax(1, QThread::idealThreadCount())),
            overview_header_wrap_);
        mem_stat_card_ = new FinnStatCard(
            QStringLiteral("Memory"), QStringLiteral("—"),
            QStringLiteral("available"), overview_header_wrap_);
        swap_stat_card_ = new FinnStatCard(
            QStringLiteral("Swap"), QStringLiteral("0 MB"),
            QStringLiteral("inactive"), overview_header_wrap_);
        uptime_stat_card_ = new FinnStatCard(
            QStringLiteral("Uptime"), format_uptime(),
            QStringLiteral("active"), overview_header_wrap_);

        stats_grid->addWidget(cpu_stat_card_, 0, 0);
        stats_grid->addWidget(mem_stat_card_, 0, 1);
        stats_grid->addWidget(swap_stat_card_, 1, 0);
        stats_grid->addWidget(uptime_stat_card_, 1, 1);
        top_row->addLayout(stats_grid, 2);

        ov_lay->addLayout(top_row);
        root->addWidget(overview_header_wrap_);

        // Clean borderless Settings-style r=6 process list card
        proc_card_ = new FinnCard(kRadiusMd, false, page);
        auto* pc_lay = new QVBoxLayout(proc_card_);
        pc_lay->setContentsMargins(8, 8, 8, 8);
        pc_lay->setSpacing(0);

        tree_ = new QTreeWidget(proc_card_);
        tree_->setFrameShape(QFrame::NoFrame);
        tree_->setColumnCount(5);
        tree_->setHeaderLabels({QStringLiteral("PID"), QStringLiteral("Name"),
                                QStringLiteral("State"), QStringLiteral("CPU %"),
                                QStringLiteral("Memory")});
        tree_->setRootIsDecorated(false);
        tree_->setAlternatingRowColors(false);
        tree_->setSortingEnabled(true);
        tree_->sortByColumn(3, Qt::DescendingOrder);
        tree_->header()->setStretchLastSection(false);
        tree_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
        tree_->header()->resizeSection(0, 68);
        tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
        tree_->header()->resizeSection(2, 72);
        tree_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
        tree_->header()->resizeSection(3, 76);
        tree_->header()->setSectionResizeMode(4, QHeaderView::Fixed);
        tree_->header()->resizeSection(4, 96);
        pc_lay->addWidget(tree_, 1);

        root->addWidget(proc_card_, 1);

        connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
            end_btn_->setEnabled(!tree_->selectedItems().isEmpty());
        });
        return page;
    }

    QWidget* buildPerformancePage() {
        auto* page = new QWidget();
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(0, 4, 0, 12);
        root->setSpacing(10);

        auto* cpu_card = new FinnCard(kRadiusMd, false, page);
        auto* cpu_lay = new QVBoxLayout(cpu_card);
        cpu_lay->setContentsMargins(14, 12, 14, 12);
        cpu_lay->setSpacing(8);
        auto* cpu_top = new QHBoxLayout();
        cpu_top->addWidget(new FinnLabel(
            QStringLiteral("CPU Activity History (60s)"),
            FinnLabel::Role::Body, FinnLabel::Tone::Ink, cpu_card));
        cpu_top->addStretch(1);
        perf_cpu_lbl_ = new FinnLabel(
            QStringLiteral("0.0%"),
            FinnLabel::Role::Headline, FinnLabel::Tone::Ink, cpu_card);
        cpu_top->addWidget(perf_cpu_lbl_);
        cpu_lay->addLayout(cpu_top);
        cpu_graph_ = new SparklineWidget(cpu_card);
        cpu_graph_->setMinimumHeight(110);
        cpu_lay->addWidget(cpu_graph_);
        root->addWidget(cpu_card);

        auto* mem_card = new FinnCard(kRadiusMd, false, page);
        auto* mem_lay = new QVBoxLayout(mem_card);
        mem_lay->setContentsMargins(14, 12, 14, 12);
        mem_lay->setSpacing(8);
        auto* mem_top = new QHBoxLayout();
        mem_top->addWidget(new FinnLabel(
            QStringLiteral("Memory Pressure"),
            FinnLabel::Role::Body, FinnLabel::Tone::Ink, mem_card));
        mem_top->addStretch(1);
        perf_mem_lbl_ = new FinnLabel(
            QStringLiteral("—"),
            FinnLabel::Role::Headline, FinnLabel::Tone::Ink, mem_card);
        mem_top->addWidget(perf_mem_lbl_);
        mem_lay->addLayout(mem_top);
        mem_bar_ = new FinnProgressBar(8, mem_card);
        mem_lay->addWidget(mem_bar_);
        perf_mem_sub_ = new FinnLabel(
            QStringLiteral("Sampling /proc/meminfo..."),
            FinnLabel::Role::Caption, FinnLabel::Tone::Dim, mem_card);
        mem_lay->addWidget(perf_mem_sub_);
        root->addWidget(mem_card);

        auto* grid = new QGridLayout();
        grid->setSpacing(10);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        load_stat_card_ = new FinnStatCard(
            QStringLiteral("Load Average"), QStringLiteral("0.00"),
            QStringLiteral("1m · 5m · 15m"), page);
        proc_stat_card_ = new FinnStatCard(
            QStringLiteral("Active Processes"), QStringLiteral("—"),
            QStringLiteral("running"), page);
        grid->addWidget(load_stat_card_, 0, 0);
        grid->addWidget(proc_stat_card_, 0, 1);
        root->addLayout(grid);

        root->addStretch(1);
        return page;
    }

    QWidget* buildDisksPage() {
        auto* sc = new QScrollArea();
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sc->setAutoFillBackground(false);
        sc->viewport()->setAutoFillBackground(false);

        auto* page = new QWidget(sc);
        page->setAutoFillBackground(false);
        disks_layout_ = new QVBoxLayout(page);
        disks_layout_->setContentsMargins(0, 4, 2, 12);
        disks_layout_->setSpacing(10);
        sc->setWidget(page);
        return sc;
    }

    void refreshDisksPage() {
        if (!disks_layout_) return;
        while (QLayoutItem* item = disks_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
            if (!vol.isValid() || !vol.isReady() || vol.bytesTotal() <= 0) continue;
            const QString fs_type = QString::fromUtf8(vol.fileSystemType());
            if (fs_type == QLatin1String("tmpfs") || fs_type == QLatin1String("devtmpfs") ||
                fs_type == QLatin1String("squashfs") || fs_type == QLatin1String("overlay")) {
                if (vol.rootPath() != QLatin1String("/")) continue;
            }
            auto* card = new FinnCard(kRadiusMd, false, disks_layout_->parentWidget());
            auto* lay = new QVBoxLayout(card);
            lay->setContentsMargins(14, 12, 14, 12);
            lay->setSpacing(6);

            const double total_gb = vol.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
            const double avail_gb = vol.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
            const double used_gb = std::max(0.0, total_gb - avail_gb);
            const double ratio =
                vol.bytesTotal() > 0 ? std::clamp(used_gb / total_gb, 0.0, 1.0) : 0.0;

            auto* top = new QHBoxLayout();
            auto* mnt = new FinnLabel(
                QStringLiteral("%1 (%2)").arg(vol.rootPath(), fs_type.isEmpty() ? QStringLiteral("fs") : fs_type),
                FinnLabel::Role::Body, FinnLabel::Tone::Ink, card);
            mnt->setFont(finn_font(10.5, QFont::Medium));
            auto* cap = new FinnLabel(
                QStringLiteral("%1 GB used of %2 GB (%3 GB free)")
                    .arg(used_gb, 0, 'f', 1)
                    .arg(total_gb, 0, 'f', 1)
                    .arg(avail_gb, 0, 'f', 1),
                FinnLabel::Role::Caption, FinnLabel::Tone::Dim, card);
            top->addWidget(mnt);
            top->addStretch(1);
            top->addWidget(cap);
            lay->addLayout(top);

            auto* bar = new FinnProgressBar(7, card);
            bar->setValue(ratio);
            lay->addWidget(bar);

            disks_layout_->addWidget(card);
        }
        disks_layout_->addStretch(1);
    }

    void switchPage(const QString& id) {
        if (id == QLatin1String("overview")) {
            page_title_->setText(QStringLiteral("System Overview"));
            overview_header_wrap_->setVisible(true);
            stack_->setCurrentIndex(0);
        } else if (id == QLatin1String("processes")) {
            page_title_->setText(QStringLiteral("Processes"));
            overview_header_wrap_->setVisible(false);
            stack_->setCurrentIndex(0);
        } else if (id == QLatin1String("performance")) {
            page_title_->setText(QStringLiteral("Performance"));
            stack_->setCurrentIndex(1);
        } else if (id == QLatin1String("disks")) {
            page_title_->setText(QStringLiteral("Storage & Disks"));
            refreshDisksPage();
            stack_->setCurrentIndex(2);
        }
    }

    void applyTreeStyle() {
        const auto& t = FinnThemeManager::instance().tokens();
        tree_->setStyleSheet(
            QStringLiteral(
                "QTreeWidget { background: transparent; color: %1; border: none; padding: 0px; outline: 0; }"
                "QTreeWidget::item { height: 28px; padding: 2px 6px; border-radius: 5px; }"
                "QTreeWidget::item:hover { background: %2; }"
                "QTreeWidget::item:selected { background: %3; color: %1; }"
                "QHeaderView { background: transparent; border: none; }"
                "QHeaderView::section { background: transparent; color: %4; border: none; "
                "border-bottom: 1px solid %5; padding: 6px 8px; font-weight: 500; }%6")
                .arg(t.ink.name(), rgba_css(t.surface_hover),
                     rgba_css(t.tile_on), rgba_css(t.dim),
                     rgba_css(t.hair), scrollbar_qss(t)));
    }

    void poll() {
        // 1. CPU delta
        double total_delta = 0.0;
        double cpu_pct = 0.0;
        const auto cpus = finn::sys::read_proc_stat();
        if (!cpus.empty()) {
            const auto& agg = cpus.front();
            if (has_prev_cpu_ && agg.total > prev_cpu_.total) {
                total_delta = static_cast<double>(agg.total - prev_cpu_.total);
                const double idle_delta =
                    agg.idle >= prev_cpu_.idle ? static_cast<double>(agg.idle - prev_cpu_.idle) : 0.0;
                cpu_pct = qBound(0.0, 100.0 * (total_delta - idle_delta) / total_delta, 100.0);
                cpu_graph_->pushSample(cpu_pct);
                const QString pct_str = QStringLiteral("%1%").arg(cpu_pct, 0, 'f', 1);
                cpu_stat_card_->setValueParts(
                    pct_str,
                    QStringLiteral("%1 cores").arg(qMax(1, QThread::idealThreadCount())));
                perf_cpu_lbl_->setText(pct_str);
            }
            prev_cpu_ = agg;
            has_prev_cpu_ = true;
        }

        // 2. Memory & Swap
        const auto mem = finn::sys::read_meminfo();
        int mem_pct = 0;
        if (mem.total_kib > 0) {
            const double ratio = static_cast<double>(mem.used_kib) / static_cast<double>(mem.total_kib);
            mem_pct = qRound(ratio * 100.0);
            mem_bar_->setValue(ratio);
            mem_stat_card_->setValueParts(
                format_kib(mem.used_kib),
                QStringLiteral("of %1").arg(format_kib(mem.total_kib)));
            perf_mem_lbl_->setText(QStringLiteral("%1%").arg(mem_pct));
            perf_mem_sub_->setText(
                QStringLiteral("%1 used of %2 (%3 available)")
                    .arg(format_kib(mem.used_kib),
                         format_kib(mem.total_kib),
                         format_kib(mem.available_kib)));
        }
        std::uint64_t swap_total_kib = 0;
        std::uint64_t swap_free_kib = 0;
        read_swap_kib(&swap_total_kib, &swap_free_kib);
        if (swap_total_kib > 0) {
            const std::uint64_t swap_used =
                swap_total_kib >= swap_free_kib
                    ? (swap_total_kib - swap_free_kib)
                    : 0;
            swap_stat_card_->setValueParts(
                format_kib(swap_used),
                QStringLiteral("of %1").arg(format_kib(swap_total_kib)));
        } else {
            swap_stat_card_->setValueParts(
                QStringLiteral("0 MB"), QStringLiteral("not configured"));
        }

        summary_pill_->setText(
            QStringLiteral("CPU %1% · RAM %2%")
                .arg(cpu_pct, 0, 'f', 0)
                .arg(mem_pct));

        // 3. Uptime & Load
        const QString up = format_uptime();
        const QString load = read_first_line("/proc/loadavg");
        const auto load_parts = load.split(' ', Qt::SkipEmptyParts);
        const QString load_1m = !load_parts.isEmpty() ? load_parts[0] : QStringLiteral("0.00");
        uptime_stat_card_->setValueParts(up, QStringLiteral("load %1").arg(load_1m));
        if (load_parts.size() >= 3) {
            load_stat_card_->setValueParts(
                load_parts[0],
                QStringLiteral("%1 · %2").arg(load_parts[1], load_parts[2]));
        }

        // 4. Processes
        int selected_pid = -1;
        if (!tree_->selectedItems().isEmpty()) {
            selected_pid = tree_->selectedItems().first()->data(0, Qt::UserRole).toInt();
        }

        const auto procs = finn::sys::read_processes();
        proc_stat_card_->setValueParts(
            QString::number(procs.size()), QStringLiteral("tasks"));

        std::unordered_map<int, std::uint64_t> next_ticks;
        next_ticks.reserve(procs.size());

        const int prev_scroll = tree_->verticalScrollBar()->value();

        tree_->setUpdatesEnabled(false);
        tree_->setSortingEnabled(false);
        tree_->clear();

        for (const auto& pr : procs) {
            next_ticks[pr.pid] = pr.cpu_ticks;
            double proc_cpu = 0.0;
            if (total_delta > 0.0) {
                const auto it = prev_proc_ticks_.find(pr.pid);
                if (it != prev_proc_ticks_.end() && pr.cpu_ticks >= it->second) {
                    proc_cpu = qBound(0.0, 100.0 * static_cast<double>(pr.cpu_ticks - it->second) / total_delta, 100.0);
                }
            }
            auto* item = new ProcessTreeItem(tree_);
            item->setText(0, QString::number(pr.pid));
            item->setData(0, Qt::UserRole, pr.pid);
            item->setText(1, QString::fromStdString(pr.name));
            item->setText(2, QString::fromStdString(pr.state));
            item->setText(3, QStringLiteral("%1%").arg(proc_cpu, 0, 'f', 1));
            item->setData(3, Qt::UserRole, proc_cpu);
            item->setText(4, format_kib(pr.rss_kib));
            item->setData(4, Qt::UserRole, static_cast<double>(pr.rss_kib));

            if (pr.pid == selected_pid) {
                item->setSelected(true);
            }
        }
        prev_proc_ticks_ = std::move(next_ticks);

        tree_->setSortingEnabled(true);
        applyFilter();
        tree_->setUpdatesEnabled(true);
        tree_->verticalScrollBar()->setValue(prev_scroll);
        end_btn_->setEnabled(!tree_->selectedItems().isEmpty());
    }

    void applyFilter() {
        const QString q =
            (sidebar_ && sidebar_->searchField())
                ? sidebar_->searchField()->text().trimmed()
                : QString();
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
            auto* item = tree_->topLevelItem(i);
            const bool match = q.isEmpty() ||
                               item->text(1).contains(q, Qt::CaseInsensitive) ||
                               item->text(0).contains(q, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
    }

    void endSelectedProcess() {
        if (tree_->selectedItems().isEmpty()) return;
        auto* item = tree_->selectedItems().first();
        const int pid = item->data(0, Qt::UserRole).toInt();
        const QString name = item->text(1);
        if (pid <= 1) {
            showToast(QStringLiteral("Refusing to terminate PID %1").arg(pid),
                      FinnToast::Severity::Warning);
            return;
        }
        const bool ok = FinnDialog::confirm(
            this,
            QStringLiteral("End Process"),
            QStringLiteral("Send SIGTERM to \"%1\" (PID %2)?").arg(name).arg(pid),
            QStringLiteral("End Process"),
            true);
        if (!ok) return;

        if (::kill(pid, SIGTERM) != 0) {
            showToast(QStringLiteral("Failed to end %1: %2")
                          .arg(name, QString::fromLocal8Bit(std::strerror(errno))),
                      FinnToast::Severity::Error);
            return;
        }
        showToast(QStringLiteral("Sent SIGTERM to %1 (%2)").arg(name).arg(pid),
                  FinnToast::Severity::Success);
        QTimer::singleShot(300, this, [this]() { poll(); });
    }

    bool has_prev_cpu_ = false;
    finn::sys::CpuSample prev_cpu_{};
    std::unordered_map<int, std::uint64_t> prev_proc_ticks_;

    FinnSidebar* sidebar_ = nullptr;
    FinnLabel* page_title_ = nullptr;
    FinnBadge* summary_pill_ = nullptr;
    FinnButton* end_btn_ = nullptr;
    QStackedWidget* stack_ = nullptr;

    QWidget* overview_header_wrap_ = nullptr;
    FinnHeroCard* hero_card_ = nullptr;
    FinnStatCard* cpu_stat_card_ = nullptr;
    FinnStatCard* mem_stat_card_ = nullptr;
    FinnStatCard* swap_stat_card_ = nullptr;
    FinnStatCard* uptime_stat_card_ = nullptr;
    FinnCard* proc_card_ = nullptr;
    QTreeWidget* tree_ = nullptr;

    SparklineWidget* cpu_graph_ = nullptr;
    FinnLabel* perf_cpu_lbl_ = nullptr;
    FinnProgressBar* mem_bar_ = nullptr;
    FinnLabel* perf_mem_lbl_ = nullptr;
    FinnLabel* perf_mem_sub_ = nullptr;
    FinnStatCard* load_stat_card_ = nullptr;
    FinnStatCard* proc_stat_card_ = nullptr;

    QVBoxLayout* disks_layout_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-monitor 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-monitor");
            return 0;
        }
    }
    FinnApplication app(argc, argv, QStringLiteral("monitor"), QStringLiteral("Finn Monitor"));
    MonitorWindow win;
    win.show();
    return app.exec();
}

#include "main.moc"
