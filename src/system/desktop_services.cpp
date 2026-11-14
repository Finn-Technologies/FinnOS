// Implements FinnDesktopServices: async hardware, audio, network, bluetooth,
// MPRIS media, and power actions shared by finn-shell and Settings.

#include "system/desktop_services.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "session/client.hpp"

namespace finn::sys {
namespace {

bool has_binary(const QString& name) {
    return !QStandardPaths::findExecutable(name).isEmpty();
}

}  // namespace

FinnDesktopServices& FinnDesktopServices::instance() {
    static FinnDesktopServices svc;
    return svc;
}

FinnDesktopServices::FinnDesktopServices(QObject* parent) : QObject(parent) {
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(3000);
    connect(poll_timer_, &QTimer::timeout, this, &FinnDesktopServices::refreshAll);
    poll_timer_->start();
    QTimer::singleShot(100, this, &FinnDesktopServices::refreshAll);
}

void FinnDesktopServices::runProbe(
    const QString& tag, const QString& program, const QStringList& args,
    std::function<void(int exit_code, const QString& out)> on_done) {
    if (in_flight_.contains(tag)) return;
    if (!has_binary(program)) {
        on_done(-1, {});
        return;
    }

    in_flight_.push_back(tag);
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, tag, on_done = std::move(on_done)](
                int code, QProcess::ExitStatus) {
                in_flight_.removeAll(tag);
                const QString out =
                    QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                on_done(code, out);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, tag, on_done](QProcess::ProcessError) {
                in_flight_.removeAll(tag);
                proc->deleteLater();
                on_done(-1, {});
            });
    proc->start(program, args);
}

void FinnDesktopServices::refreshAll() {
    backlight_ = read_backlight();
    battery_ = read_battery();
    refreshAudio();
    refreshNetwork();
    refreshBluetooth();
    refreshMedia();
    emit stateUpdated();
}

void FinnDesktopServices::refreshAudio() {
    runProbe(QStringLiteral("wpctl_vol"), QStringLiteral("wpctl"),
             {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")},
             [this](int code, const QString& out) {
                 AudioState next;
                 if (code == 0 && out.contains(QStringLiteral("Volume:"))) {
                     static const QRegularExpression re(
                         QStringLiteral("Volume:\\s*([0-9]+(?:\\.[0-9]+)?)"));
                     const auto m = re.match(out);
                     if (m.hasMatch()) {
                         next.available = true;
                         next.volume_percent = std::clamp(
                             qRound(m.captured(1).toDouble() * 100.0), 0, 100);
                         next.muted = out.contains(QStringLiteral("MUTED"),
                                                   Qt::CaseInsensitive);
                     }
                 }
                 audio_ = next;
                 emit stateUpdated();
             });
}

void FinnDesktopServices::refreshNetwork() {
    // Immediate local IPv4 summary fallback
    QStringList ips;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if ((iface.flags() & QNetworkInterface::IsLoopBack) ||
            !(iface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ips << QStringLiteral("%1 (%2)").arg(entry.ip().toString(),
                                                     iface.name());
            }
        }
    }
    network_.ipv4_summary = ips.join(QStringLiteral(", "));

    runProbe(
        QStringLiteral("nmcli_dev"), QStringLiteral("nmcli"),
        {QStringLiteral("-t"), QStringLiteral("-f"),
         QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"), QStringLiteral("dev")},
        [this](int code, const QString& out) {
            NetworkState next = network_;
            next.wifi_hardware = false;
            next.wired_connected = false;
            next.wired_label.clear();
            next.active_wifi_ssid.clear();
            if (code != 0) {
                next.nm_running = false;
                network_ = next;
                emit stateUpdated();
                return;
            }
            next.nm_running = true;
            for (const QString& line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(QLatin1Char(':'));
                if (parts.size() < 4) continue;
                const QString& dev = parts[0];
                const QString& type = parts[1];
                const QString& state = parts[2];
                const QString& conn = parts[3];
                if (type == QStringLiteral("wifi")) {
                    next.wifi_hardware = true;
                    if (state.startsWith(QStringLiteral("connected"))) {
                        next.active_wifi_ssid = conn;
                    }
                } else if (type == QStringLiteral("ethernet") &&
                           state.startsWith(QStringLiteral("connected"))) {
                    next.wired_connected = true;
                    next.wired_label =
                        conn.isEmpty() ? dev : QStringLiteral("%1 (%2)").arg(conn, dev);
                }
            }
            network_ = next;
            emit stateUpdated();

            if (network_.wifi_hardware) {
                runProbe(
                    QStringLiteral("nmcli_radio"), QStringLiteral("nmcli"),
                    {QStringLiteral("-t"), QStringLiteral("-f"),
                     QStringLiteral("WIFI"), QStringLiteral("general")},
                    [this](int rcode, const QString& rout) {
                        if (rcode == 0) {
                            network_.wifi_enabled =
                                rout.trimmed().compare(QStringLiteral("enabled"),
                                                       Qt::CaseInsensitive) == 0;
                            emit stateUpdated();
                        }
                    });
                runProbe(
                    QStringLiteral("nmcli_wifi"), QStringLiteral("nmcli"),
                    {QStringLiteral("-t"), QStringLiteral("-f"),
                     QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
                     QStringLiteral("dev"), QStringLiteral("wifi"),
                     QStringLiteral("list"), QStringLiteral("--rescan"),
                     QStringLiteral("no")},
                    [this](int wcode, const QString& wout) {
                        if (wcode != 0) return;
                        QVector<WifiNetwork> list;
                        for (const QString& line :
                             wout.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                            const QStringList f = line.split(QLatin1Char(':'));
                            if (f.size() < 4 || f[1].isEmpty()) continue;
                            WifiNetwork w;
                            w.active = (f[0] == QStringLiteral("*"));
                            w.ssid = f[1];
                            w.signal = f[2].toInt();
                            w.secured = !f[3].isEmpty() && f[3] != QStringLiteral("--");
                            list.push_back(std::move(w));
                        }
                        network_.networks = std::move(list);
                        emit stateUpdated();
                    });
            } else {
                network_.wifi_enabled = false;
                network_.networks.clear();
            }
        });
}

void FinnDesktopServices::refreshBluetooth() {
    runProbe(QStringLiteral("bt_show"), QStringLiteral("bluetoothctl"),
             {QStringLiteral("show")},
             [this](int code, const QString& out) {
                 BluetoothState next;
                 if (code == 0 && out.contains(QStringLiteral("Controller"))) {
                     next.available = true;
                     for (const QString& line : out.split(QLatin1Char('\n'))) {
                         const QString trimmed = line.trimmed();
                         if (trimmed.startsWith(QStringLiteral("Name:"))) {
                             next.controller_name =
                                 trimmed.mid(5).trimmed();
                         } else if (trimmed.startsWith(QStringLiteral("Powered:"))) {
                             next.powered =
                                 trimmed.mid(8).trimmed() == QStringLiteral("yes");
                         }
                     }
                 }
                 bluetooth_ = next;
                 emit stateUpdated();

                 if (bluetooth_.available && bluetooth_.powered) {
                     runProbe(
                         QStringLiteral("bt_devs"), QStringLiteral("bluetoothctl"),
                         {QStringLiteral("devices")},
                         [this](int dcode, const QString& dout) {
                             if (dcode != 0) return;
                             QVector<BluetoothDevice> devs;
                             for (const QString& line :
                                  dout.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                                 // Format: Device XX:XX:XX:XX:XX:XX Name
                                 const QString t = line.trimmed();
                                 if (!t.startsWith(QStringLiteral("Device "))) continue;
                                 const int sp = t.indexOf(QLatin1Char(' '), 7);
                                 if (sp < 0) continue;
                                 BluetoothDevice d;
                                 d.address = t.mid(7, sp - 7);
                                 d.name = t.mid(sp + 1).trimmed();
                                 d.paired = true;
                                 devs.push_back(std::move(d));
                             }
                             bluetooth_.devices = std::move(devs);
                             emit stateUpdated();
                         });
                 }
             });
}

void FinnDesktopServices::refreshMedia() {
    MediaState next;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        media_ = next;
        return;
    }
    const QDBusReply<QStringList> names = bus.interface()->registeredServiceNames();
    if (!names.isValid()) {
        media_ = next;
        return;
    }
    for (const QString& svc : names.value()) {
        if (!svc.startsWith(QStringLiteral("org.mpris.MediaPlayer2."))) continue;
        QDBusInterface player(svc, QStringLiteral("/org/mpris/MediaPlayer2"),
                              QStringLiteral("org.mpris.MediaPlayer2.Player"), bus);
        if (!player.isValid()) continue;
        const QString status = player.property("PlaybackStatus").toString();
        const QVariantMap meta = player.property("Metadata").toMap();
        next.active = true;
        next.playing = (status == QStringLiteral("Playing"));
        next.service_name = svc;
        next.player_identity = svc.mid(23);
        next.title = meta.value(QStringLiteral("xesam:title")).toString();
        const QStringList artists =
            meta.value(QStringLiteral("xesam:artist")).toStringList();
        next.artist = artists.join(QStringLiteral(", "));
        if (next.title.isEmpty()) next.title = next.player_identity;
        break;
    }
    media_ = next;
}

void FinnDesktopServices::setVolumePercent(int percent) {
    if (!has_binary(QStringLiteral("wpctl"))) return;
    const int p = std::clamp(percent, 0, 100);
    const QString arg = QString::number(p / 100.0, 'f', 2);
    QProcess::startDetached(
        QStringLiteral("wpctl"),
        {QStringLiteral("set-volume"), QStringLiteral("-l"),
         QStringLiteral("1.0"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), arg});
    audio_.volume_percent = p;
    emit stateUpdated();
    QTimer::singleShot(180, this, &FinnDesktopServices::refreshAudio);
}

void FinnDesktopServices::toggleMute() {
    if (!has_binary(QStringLiteral("wpctl"))) return;
    QProcess::startDetached(
        QStringLiteral("wpctl"),
        {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
         QStringLiteral("toggle")});
    audio_.muted = !audio_.muted;
    emit stateUpdated();
    QTimer::singleShot(180, this, &FinnDesktopServices::refreshAudio);
}

void FinnDesktopServices::setBrightnessPercent(int percent) {
    const int p = std::clamp(percent, 5, 100);
    std::string err;
    if (write_backlight_percent(p, "/sys/class/backlight", &err)) {
        backlight_ = read_backlight();
        emit stateUpdated();
        return;
    }
    if (has_binary(QStringLiteral("brightnessctl"))) {
        QProcess::startDetached(QStringLiteral("brightnessctl"),
                                {QStringLiteral("set"),
                                 QStringLiteral("%1%").arg(p)});
        QTimer::singleShot(180, this, [this]() {
            backlight_ = read_backlight();
            emit stateUpdated();
        });
        return;
    }
    emit commandError(QString::fromStdString(err));
}

void FinnDesktopServices::setWifiEnabled(bool enabled) {
    if (!has_binary(QStringLiteral("nmcli"))) return;
    QProcess::startDetached(QStringLiteral("nmcli"),
                            {QStringLiteral("radio"), QStringLiteral("wifi"),
                             enabled ? QStringLiteral("on")
                                     : QStringLiteral("off")});
    QTimer::singleShot(300, this, &FinnDesktopServices::refreshNetwork);
}

void FinnDesktopServices::connectWifi(const QString& ssid,
                                      const QString& password) {
    if (!has_binary(QStringLiteral("nmcli"))) return;
    QStringList args{QStringLiteral("dev"), QStringLiteral("wifi"),
                     QStringLiteral("connect"), ssid};
    if (!password.isEmpty()) {
        args << QStringLiteral("password") << password;
    }
    QProcess::startDetached(QStringLiteral("nmcli"), args);
    QTimer::singleShot(800, this, &FinnDesktopServices::refreshNetwork);
}

void FinnDesktopServices::setBluetoothPowered(bool powered) {
    if (!has_binary(QStringLiteral("bluetoothctl"))) return;
    QProcess::startDetached(QStringLiteral("bluetoothctl"),
                            {QStringLiteral("power"),
                             powered ? QStringLiteral("on")
                                     : QStringLiteral("off")});
    QTimer::singleShot(300, this, &FinnDesktopServices::refreshBluetooth);
}

void FinnDesktopServices::toggleBluetoothDevice(const QString& address,
                                                bool connect_dev) {
    if (!has_binary(QStringLiteral("bluetoothctl"))) return;
    QProcess::startDetached(QStringLiteral("bluetoothctl"),
                            {connect_dev ? QStringLiteral("connect")
                                         : QStringLiteral("disconnect"),
                             address});
    QTimer::singleShot(500, this, &FinnDesktopServices::refreshBluetooth);
}

void FinnDesktopServices::mediaPlayPause() {
    if (!media_.active || media_.service_name.isEmpty()) return;
    QDBusInterface player(media_.service_name,
                          QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"));
    player.asyncCall(QStringLiteral("PlayPause"));
    QTimer::singleShot(150, this, &FinnDesktopServices::refreshMedia);
}

void FinnDesktopServices::mediaNext() {
    if (!media_.active || media_.service_name.isEmpty()) return;
    QDBusInterface player(media_.service_name,
                          QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"));
    player.asyncCall(QStringLiteral("Next"));
    QTimer::singleShot(150, this, &FinnDesktopServices::refreshMedia);
}

void FinnDesktopServices::mediaPrevious() {
    if (!media_.active || media_.service_name.isEmpty()) return;
    QDBusInterface player(media_.service_name,
                          QStringLiteral("/org/mpris/MediaPlayer2"),
                          QStringLiteral("org.mpris.MediaPlayer2.Player"));
    player.asyncCall(QStringLiteral("Previous"));
    QTimer::singleShot(150, this, &FinnDesktopServices::refreshMedia);
}

bool FinnDesktopServices::triggerPowerAction(const QString& action) {
    finn::SessionClient client;
    std::string err;
    if (client.power_action(action.toStdString(), &err)) return true;
    emit commandError(QString::fromStdString(err));
    return false;
}

}  // namespace finn::sys
