// Real capability probes for the first-run flow. Header-only so both
// `finn-shell` (startup preflight) and `finn-oobe` (live gates) can use the
// same code without a new CMake source in either program.
//
// Every Capability starts Unknown ("not probed"). A value is recorded as
// Available/Unavailable only when a real executable, D-Bus service or file
// operation answered. No probe ever assumes success from a package name.

#ifndef FINN_SHELL_OOBE_CAPABILITY_PROBE_HPP
#define FINN_SHELL_OOBE_CAPABILITY_PROBE_HPP

#include "shell/full_oobe_flow.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <string>
#include <utility>

namespace finn::shell::oobe::probe {

// Written by finn-shell's OOBE preflight, read by finn-oobe. Lives under the
// user's own state directory: no root-owned socket or new daemon is involved.
inline constexpr const char* kOobeCapabilitiesRel =
    ".local/state/finn/oobe-capabilities";

namespace detail {

inline std::string S(const QString& text) { return text.toStdString(); }

inline bool run_command(const QString& program, const QStringList& args,
                        QString* out, int timeout_ms = 1500) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(timeout_ms)) return false;
    if (!p.waitForFinished(timeout_ms)) {
        p.kill();
        p.waitForFinished(200);
        return false;
    }
    if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

inline Capability from_executable(const char* program, const char* detail) {
    const QString path = QStandardPaths::findExecutable(QString::fromLatin1(program));
    if (path.isEmpty()) {
        return Capability::unavailable(S(QStringLiteral("%1 not found on PATH")
                                             .arg(QString::fromLatin1(program))));
    }
    return Capability::available(S(QStringLiteral("%1 (%2)")
                                       .arg(QString::fromLatin1(detail), path)));
}

}  // namespace detail

// Measured at run time. `nmcli` is the actual connector OOBE uses; finding the
// binary alone is not proof of a Wi-Fi device, so the device list is queried
// separately. A failed query is Unavailable with the reason, never "no".
inline CapabilitySnapshot capabilities() {
    CapabilitySnapshot caps;

    const QString nmcli = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    caps.network_manager = nmcli.isEmpty()
        ? Capability::unavailable(detail::S(QStringLiteral("nmcli not found on PATH")))
        : Capability::available(detail::S(QStringLiteral("nmcli (%1)").arg(nmcli)));

    QString devices;
    if (!nmcli.isEmpty() &&
        detail::run_command(nmcli, {"-t", "-f", "TYPE", "device", "status"}, &devices)) {
        const bool wifi = devices.contains(QStringLiteral("wifi"), Qt::CaseInsensitive);
        const bool wired = devices.contains(QStringLiteral("ethernet"), Qt::CaseInsensitive);
        caps.wifi_hardware = wifi
            ? Capability::available(detail::S(QStringLiteral("nmcli reports a Wi-Fi device")))
            : Capability::unavailable(detail::S(QStringLiteral("nmcli reports no Wi-Fi device")));
        caps.wired_link = wired
            ? Capability::available(detail::S(QStringLiteral("nmcli reports an Ethernet device")))
            : Capability::unavailable(detail::S(QStringLiteral("nmcli reports no Ethernet device")));
    } else {
        const QString why = nmcli.isEmpty() ? QStringLiteral("nmcli not found")
                                            : QStringLiteral("nmcli device query failed");
        caps.wifi_hardware = Capability::unknown(detail::S(why));
        caps.wired_link = Capability::unknown(detail::S(why));
    }

    caps.account_service = detail::from_executable("pkexec", "privileged account helper");
    const QString useradd = QStandardPaths::findExecutable(QStringLiteral("useradd"));
    if (!useradd.isEmpty() && caps.account_service.state == Availability::Available) {
        caps.account_service.detail += detail::S(QStringLiteral(" + useradd (%1)").arg(useradd));
    } else if (caps.account_service.state == Availability::Available) {
        caps.account_service = Capability::unavailable(
            detail::S(QStringLiteral("useradd not found on PATH")));
    }

    // A real write into the user's Finn config directory, then removed. This
    // is the sink OOBE actually uses for account.conf and locale.conf.
    const QString dir = QDir::homePath() + QStringLiteral("/.config/finn");
    const QString file = dir + QStringLiteral("/.oobe-write-probe");
    if (QDir().mkpath(dir) && QFile(file).open(QIODevice::WriteOnly)) {
        caps.settings_write = Capability::available(
            detail::S(QStringLiteral("wrote and removed %1").arg(file)));
        QFile::remove(file);
    } else {
        caps.settings_write = Capability::unavailable(
            detail::S(QStringLiteral("cannot write %1").arg(dir)));
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        caps.accessibility_bus = Capability::unavailable(
            detail::S(QStringLiteral("session D-Bus unavailable")));
    } else {
        QDBusInterface a11y(QStringLiteral("org.a11y.Bus"),
                            QStringLiteral("/org/a11y/bus"),
                            QStringLiteral("org.a11y.Bus"), bus);
        caps.accessibility_bus = a11y.isValid()
            ? Capability::available(detail::S(QStringLiteral("org.a11y.Bus registered")))
            : Capability::unavailable(detail::S(QStringLiteral("org.a11y.Bus not registered")));
    }
    return caps;
}

inline const char* state_name(Availability state) {
    switch (state) {
        case Availability::Available: return "available";
        case Availability::Unavailable: return "unavailable";
        case Availability::Unknown: return "unknown";
    }
    return "unknown";
}

// Line-oriented on purpose: AGENTS.md forbids JSON inside a daemon. One
// `key|state|detail` line per capability, stable for tests and log readers.
inline QString serialize(const CapabilitySnapshot& caps) {
    const std::pair<const char*, const Capability*> rows[] = {
        {"network_manager", &caps.network_manager},
        {"wifi_hardware", &caps.wifi_hardware},
        {"wired_link", &caps.wired_link},
        {"account_service", &caps.account_service},
        {"settings_write", &caps.settings_write},
        {"accessibility_bus", &caps.accessibility_bus},
    };
    QString out;
    for (const auto& row : rows) {
        out += QString::fromLatin1(row.first) + QLatin1Char('|') +
               QString::fromLatin1(state_name(row.second->state)) + QLatin1Char('|') +
               QString::fromStdString(row.second->detail) + QLatin1Char('\n');
    }
    return out;
}

inline Capability* field(CapabilitySnapshot& caps, const QString& key) {
    if (key == QLatin1String("network_manager")) return &caps.network_manager;
    if (key == QLatin1String("wifi_hardware")) return &caps.wifi_hardware;
    if (key == QLatin1String("wired_link")) return &caps.wired_link;
    if (key == QLatin1String("account_service")) return &caps.account_service;
    if (key == QLatin1String("settings_write")) return &caps.settings_write;
    if (key == QLatin1String("accessibility_bus")) return &caps.accessibility_bus;
    return nullptr;
}

inline CapabilitySnapshot deserialize(const QString& text) {
    CapabilitySnapshot caps;  // every field starts Unknown
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.size() < 3) continue;
        Capability* slot = field(caps, parts.at(0));
        if (!slot) continue;
        const QString state = parts.at(1);
        slot->state = state == QLatin1String("available") ? Availability::Available
                    : state == QLatin1String("unavailable") ? Availability::Unavailable
                                                           : Availability::Unknown;
        slot->detail = detail::S(parts.mid(2).join(QLatin1Char('|')));
    }
    return caps;
}

inline bool write_preflight(const QString& path, const CapabilitySnapshot& caps) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = serialize(caps).toUtf8();
    return f.write(bytes) == bytes.size();
}

inline CapabilitySnapshot read_preflight(const QString& path, bool* ok = nullptr) {
    QFile f(path);
    if (ok) *ok = f.open(QIODevice::ReadOnly);
    if (!f.isOpen()) return CapabilitySnapshot{};  // Unknown everywhere
    const QString text = QString::fromUtf8(f.readAll());
    return deserialize(text);
}

}  // namespace finn::shell::oobe::probe

#endif  // FINN_SHELL_OOBE_CAPABILITY_PROBE_HPP
