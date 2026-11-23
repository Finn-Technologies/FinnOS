// C++20 System Monitor backend & sparkline implementation for finn-monitor.

#include "monitor_controller.hpp"

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace finn::apps {

namespace {

QString format_gb(uint64_t bytes) {
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
}

QString format_uptime(int64_t secs) {
    if (secs <= 0) return QStringLiteral("Unavailable");
    const int64_t days = secs / 86400;
    const int64_t hours = (secs % 86400) / 3600;
    const int64_t mins = (secs % 3600) / 60;
    if (days > 0) return QStringLiteral("%1d %2h %3m").arg(days).arg(hours).arg(mins);
    return QStringLiteral("%1h %2m").arg(hours).arg(mins);
}

}  // namespace

CpuSparklineItem::CpuSparklineItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void CpuSparklineItem::setSamples(const QVariantList& s) {
    samples_ = s;
    emit samplesChanged();
    update();
}

void CpuSparklineItem::setLineColor(const QColor& c) {
    if (c == color_) return;
    color_ = c;
    emit samplesChanged();
    update();
}

void CpuSparklineItem::paint(QPainter* p) {
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = boundingRect().adjusted(4, 8, -4, -8);
    if (r.width() <= 4 || r.height() <= 4) return;

    // Subtle horizontal grid lines
    QPen gridPen(QColor(128, 128, 128, 36), 1.0);
    p->setPen(gridPen);
    for (int i = 0; i <= 4; ++i) {
        const qreal y = r.top() + (r.height() * i) / 4.0;
        p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    if (samples_.isEmpty()) return;
    if (samples_.size() == 1) {
        const double pct = std::clamp(samples_[0].toDouble(), 0.0, 100.0);
        const qreal y = r.bottom() - (r.height() * pct) / 100.0;
        p->setPen(QPen(color_, 2.0, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        return;
    }

    QPainterPath line;
    QPainterPath fill;
    const int n = static_cast<int>(samples_.size());
    for (int i = 0; i < n; ++i) {
        const double pct = std::clamp(samples_[i].toDouble(), 0.0, 100.0);
        const qreal x = r.left() + (r.width() * i) / std::max(1, n - 1);
        const qreal y = r.bottom() - (r.height() * pct) / 100.0;
        if (i == 0) {
            line.moveTo(x, y);
            fill.moveTo(x, r.bottom());
            fill.lineTo(x, y);
        } else {
            line.lineTo(x, y);
            fill.lineTo(x, y);
        }
    }
    fill.lineTo(r.right(), r.bottom());
    fill.closeSubpath();

    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    QColor cTop = color_;
    cTop.setAlpha(72);
    QColor cBot = color_;
    cBot.setAlpha(4);
    grad.setColorAt(0.0, cTop);
    grad.setColorAt(1.0, cBot);
    p->fillPath(fill, grad);

    p->setPen(QPen(color_, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(line);
}

MonitorController::MonitorController(QObject* parent) : QObject(parent) {
    sampleNow();
    timer_.setInterval(1500);
    connect(&timer_, &QTimer::timeout, this, &MonitorController::sampleNow);
    timer_.start();
}

void MonitorController::setFilterText(const QString& f) {
    if (f == filter_) return;
    filter_ = f;
    emit processesChanged();
}

QVariantList MonitorController::processes() const {
    const QString needle = filter_.trimmed().toLower();
    if (needle.isEmpty()) return all_procs_;
    QVariantList out;
    for (const QVariant& v : all_procs_) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString().toLower().contains(needle) ||
            QString::number(m.value(QStringLiteral("pid")).toInt()).contains(needle)) {
            out.append(m);
        }
    }
    return out;
}

void MonitorController::sampleNow() {
    sampleCpuAndMem();
    sampleProcesses();
    emit metricsChanged();
    emit processesChanged();
}

void MonitorController::sampleCpuAndMem() {
    double loads[3] = {0.0, 0.0, 0.0};
    if (getloadavg(loads, 3) == 3) {
        load_text_ = QStringLiteral("%1, %2, %3")
                         .arg(loads[0], 0, 'f', 2)
                         .arg(loads[1], 0, 'f', 2)
                         .arg(loads[2], 0, 'f', 2);
    } else {
        load_text_ = QStringLiteral("Unavailable");
    }

#ifdef __APPLE__
    // CPU via Mach host_processor_info
    host_cpu_load_info_data_t cpuinfo {};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&cpuinfo), &count) == KERN_SUCCESS) {
        const uint64_t user = cpuinfo.cpu_ticks[CPU_STATE_USER];
        const uint64_t sys = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
        const uint64_t nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];
        const uint64_t idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        const uint64_t total = user + sys + nice + idle;
        if (prev_total_ticks_ > 0 && total > prev_total_ticks_) {
            const uint64_t dt = total - prev_total_ticks_;
            const uint64_t di = idle - prev_idle_ticks_;
            cpu_pct_ = std::clamp(100.0 * static_cast<double>(dt - di) / static_cast<double>(dt), 0.0, 100.0);
        } else if (total > 0) {
            cpu_pct_ = std::clamp(100.0 * static_cast<double>(total - idle) / static_cast<double>(total), 0.0, 100.0);
        }
        prev_total_ticks_ = total;
        prev_idle_ticks_ = idle;
    }

    uint64_t total_mem = 0;
    size_t len = sizeof(total_mem);
    sysctlbyname("hw.memsize", &total_mem, &len, nullptr, 0);

    vm_statistics64_data_t vm {};
    mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
    uint64_t used_mem = 0;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &vm_count) == KERN_SUCCESS) {
        vm_size_t page_size = 4096;
        host_page_size(mach_host_self(), &page_size);
        used_mem = (static_cast<uint64_t>(vm.active_count) +
                    static_cast<uint64_t>(vm.wire_count) +
                    static_cast<uint64_t>(vm.compressor_page_count)) *
                   static_cast<uint64_t>(page_size);
    }
    mem_total_text_ = total_mem > 0 ? format_gb(total_mem) : QStringLiteral("Unavailable");
    mem_used_text_ = used_mem > 0 ? format_gb(used_mem) : QStringLiteral("Unavailable");
    mem_pct_ = total_mem > 0 ? std::clamp(100.0 * static_cast<double>(used_mem) / static_cast<double>(total_mem), 0.0, 100.0) : 0.0;

    struct timeval boottime {};
    size_t bt_len = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &bt_len, nullptr, 0) == 0) {
        const int64_t now_s = static_cast<int64_t>(time(nullptr));
        uptime_text_ = format_uptime(now_s - boottime.tv_sec);
    }
#else
    QFile stat_f(QStringLiteral("/proc/stat"));
    if (stat_f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString line = QString::fromUtf8(stat_f.readLine()).simplified();
        const QStringList p = line.split(QLatin1Char(' '));
        if (p.size() >= 8) {
            const uint64_t idle = p[4].toULongLong() + p[5].toULongLong();
            uint64_t total = 0;
            for (int i = 1; i < p.size(); ++i) total += p[i].toULongLong();
            if (prev_total_ticks_ > 0 && total > prev_total_ticks_) {
                const uint64_t dt = total - prev_total_ticks_;
                const uint64_t di = idle - prev_idle_ticks_;
                cpu_pct_ = std::clamp(100.0 * static_cast<double>(dt - di) / static_cast<double>(dt), 0.0, 100.0);
            }
            prev_total_ticks_ = total;
            prev_idle_ticks_ = idle;
        }
    }

    uint64_t total_kb = 0;
    uint64_t avail_kb = 0;
    QFile mem_f(QStringLiteral("/proc/meminfo"));
    if (mem_f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(mem_f.readAll()).split(QLatin1Char('\n'));
        for (const QString& l : lines) {
            if (l.startsWith(QStringLiteral("MemTotal:"))) total_kb = l.simplified().section(QLatin1Char(' '), 1, 1).toULongLong();
            else if (l.startsWith(QStringLiteral("MemAvailable:"))) avail_kb = l.simplified().section(QLatin1Char(' '), 1, 1).toULongLong();
        }
    }
    const uint64_t used_kb = total_kb > avail_kb ? (total_kb - avail_kb) : 0;
    mem_total_text_ = format_gb(total_kb * 1024ULL);
    mem_used_text_ = format_gb(used_kb * 1024ULL);
    mem_pct_ = total_kb > 0 ? std::clamp(100.0 * static_cast<double>(used_kb) / static_cast<double>(total_kb), 0.0, 100.0) : 0.0;

    QFile up_f(QStringLiteral("/proc/uptime"));
    if (up_f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const double up = QString::fromUtf8(up_f.readLine()).section(QLatin1Char(' '), 0, 0).toDouble();
        uptime_text_ = format_uptime(static_cast<int64_t>(up));
    }
#endif

    cpu_history_.append(cpu_pct_);
    while (cpu_history_.size() > 32) cpu_history_.removeFirst();
}

void MonitorController::sampleProcesses() {
    QProcess ps;
    ps.start(QStringLiteral("ps"), {QStringLiteral("-axo"), QStringLiteral("pid=,pcpu=,rss=,comm=")});
    if (!ps.waitForFinished(1000)) return;

    const QStringList lines = QString::fromUtf8(ps.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVariantList list;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 4) continue;
        const int pid = parts[0].toInt();
        const double cpu = parts[1].toDouble();
        const qint64 rss_kb = parts[2].toLongLong();
        const QString name = parts.mid(3).join(QLatin1Char(' ')).section(QLatin1Char('/'), -1);

        QVariantMap m;
        m.insert(QStringLiteral("pid"), pid);
        m.insert(QStringLiteral("cpu"), cpu);
        m.insert(QStringLiteral("cpuText"), QStringLiteral("%1%").arg(cpu, 0, 'f', 1));
        m.insert(QStringLiteral("memText"), QStringLiteral("%1 MB").arg(rss_kb / 1024.0, 0, 'f', 1));
        m.insert(QStringLiteral("rssKb"), rss_kb);
        m.insert(QStringLiteral("name"), name);
        list.append(m);
    }
    proc_count_ = static_cast<int>(list.size());
    std::sort(list.begin(), list.end(), [](const QVariant& a, const QVariant& b) {
        const double ca = a.toMap().value(QStringLiteral("cpu")).toDouble();
        const double cb = b.toMap().value(QStringLiteral("cpu")).toDouble();
        if (std::abs(ca - cb) > 0.05) return ca > cb;
        return a.toMap().value(QStringLiteral("rssKb")).toLongLong() >
               b.toMap().value(QStringLiteral("rssKb")).toLongLong();
    });
    if (list.size() > 40) list = list.mid(0, 40);
    all_procs_ = std::move(list);
}

void MonitorController::terminatePid(int pid) {
    if (pid <= 1) return;
    if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
        emit toastRequested(QStringLiteral("Sent SIGTERM to PID %1").arg(pid), QStringLiteral("info"));
        sampleNow();
    } else {
        emit toastRequested(QStringLiteral("Permission denied terminating PID %1").arg(pid),
                            QStringLiteral("error"));
    }
}

}  // namespace finn::apps
