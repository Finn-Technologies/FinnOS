// Unified asynchronous Qt6/QtDBus hardware & desktop services backend shared by
// finn-shell and Settings. Every probe reports real OS hardware/daemon state.

#ifndef FINN_SYSTEM_DESKTOP_SERVICES_HPP
#define FINN_SYSTEM_DESKTOP_SERVICES_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "system/sys_files.hpp"

class QProcess;
class QTimer;

namespace finn::sys {

struct AudioState {
    bool available = false;
    bool muted = false;
    int volume_percent = 0;
    QString sink_name;
};

struct WifiNetwork {
    QString ssid;
    int signal = 0;
    bool active = false;
    bool secured = false;
};

struct NetworkState {
    bool nm_running = false;
    bool wifi_hardware = false;
    bool wifi_enabled = false;
    bool wired_connected = false;
    QString wired_label;
    QString active_wifi_ssid;
    QString ipv4_summary;
    QVector<WifiNetwork> networks;
};

struct BluetoothDevice {
    QString address;
    QString name;
    bool connected = false;
    bool paired = false;
};

struct BluetoothState {
    bool available = false;
    bool powered = false;
    QString controller_name;
    QVector<BluetoothDevice> devices;
};

struct MediaState {
    bool active = false;
    bool playing = false;
    QString service_name;
    QString player_identity;
    QString title;
    QString artist;
};

class FinnDesktopServices final : public QObject {
    Q_OBJECT

public:
    explicit FinnDesktopServices(QObject* parent = nullptr);

    static FinnDesktopServices& instance();

    const AudioState& audio() const { return audio_; }
    const BacklightInfo& backlight() const { return backlight_; }
    const BatteryInfo& battery() const { return battery_; }
    const NetworkState& network() const { return network_; }
    const BluetoothState& bluetooth() const { return bluetooth_; }
    const MediaState& media() const { return media_; }

public slots:
    void refreshAll();
    void refreshAudio();
    void refreshNetwork();
    void refreshBluetooth();
    void refreshMedia();

    void setVolumePercent(int percent);
    void toggleMute();
    void setBrightnessPercent(int percent);
    void setWifiEnabled(bool enabled);
    void connectWifi(const QString& ssid, const QString& password = {});
    void setBluetoothPowered(bool powered);
    void toggleBluetoothDevice(const QString& address, bool connect);
    void mediaPlayPause();
    void mediaNext();
    void mediaPrevious();
    bool triggerPowerAction(const QString& action);  // suspend, reboot, poweroff

signals:
    void stateUpdated();
    void commandError(const QString& message);
    void wifiConnectFinished(bool ok, const QString& ssid, const QString& message);

private:
    void runProbe(const QString& tag, const QString& program,
                  const QStringList& args,
                  std::function<void(int exit_code, const QString& out)> on_done);

    AudioState audio_;
    BacklightInfo backlight_;
    BatteryInfo battery_;
    NetworkState network_;
    BluetoothState bluetooth_;
    MediaState media_;

    QTimer* poll_timer_ = nullptr;
    QStringList in_flight_;
};

}  // namespace finn::sys

#endif
