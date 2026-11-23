// C++20 System Monitor backend & live sparkline chart for finn-monitor.
// Reads real OS CPU, memory, uptime, load, and process metrics.

#ifndef FINN_APPS_MONITOR_CONTROLLER_HPP
#define FINN_APPS_MONITOR_CONTROLLER_HPP

#include <QObject>
#include <QQuickPaintedItem>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace finn::apps {

class CpuSparklineItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY samplesChanged)

public:
    explicit CpuSparklineItem(QQuickItem* parent = nullptr);

    QVariantList samples() const { return samples_; }
    void setSamples(const QVariantList& s);
    QColor lineColor() const { return color_; }
    void setLineColor(const QColor& c);

    void paint(QPainter* painter) override;

signals:
    void samplesChanged();

private:
    QVariantList samples_;
    QColor color_{0x00, 0x7B, 0xFF};
};

class MonitorController : public QObject {
    Q_OBJECT

    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY metricsChanged)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY metricsChanged)
    Q_PROPERTY(QString memoryUsedText READ memoryUsedText NOTIFY metricsChanged)
    Q_PROPERTY(QString memoryTotalText READ memoryTotalText NOTIFY metricsChanged)
    Q_PROPERTY(QString loadAverageText READ loadAverageText NOTIFY metricsChanged)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY metricsChanged)
    Q_PROPERTY(int processCount READ processCount NOTIFY metricsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY metricsChanged)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY processesChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY processesChanged)

public:
    explicit MonitorController(QObject* parent = nullptr);

    double cpuPercent() const { return cpu_pct_; }
    double memoryPercent() const { return mem_pct_; }
    QString memoryUsedText() const { return mem_used_text_; }
    QString memoryTotalText() const { return mem_total_text_; }
    QString loadAverageText() const { return load_text_; }
    QString uptimeText() const { return uptime_text_; }
    int processCount() const { return proc_count_; }
    QVariantList cpuHistory() const { return cpu_history_; }
    QVariantList processes() const;
    QString filterText() const { return filter_; }
    void setFilterText(const QString& f);

    Q_INVOKABLE void sampleNow();
    Q_INVOKABLE void terminatePid(int pid);

signals:
    void metricsChanged();
    void processesChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    void sampleCpuAndMem();
    void sampleProcesses();

    QTimer timer_;
    double cpu_pct_ = 0.0;
    double mem_pct_ = 0.0;
    QString mem_used_text_;
    QString mem_total_text_;
    QString load_text_;
    QString uptime_text_;
    int proc_count_ = 0;
    QVariantList cpu_history_;
    QVariantList all_procs_;
    QString filter_;

    uint64_t prev_total_ticks_ = 0;
    uint64_t prev_idle_ticks_ = 0;
};

}  // namespace finn::apps

#endif  // FINN_APPS_MONITOR_CONTROLLER_HPP
