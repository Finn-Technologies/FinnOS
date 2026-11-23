// C++20 System Settings backend for finn-settings.
// Reads real hardware, OS, display, network, sound, compatibility, and theme
// state — never fakes capabilities when a subsystem is absent.

#ifndef FINN_APPS_SETTINGS_CONTROLLER_HPP
#define FINN_APPS_SETTINGS_CONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "system/desktop_services.hpp"

namespace finn::apps {

class SettingsController : public QObject {
    Q_OBJECT

    // System / About (Figma 20:98)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)
    Q_PROPERTY(QString architecture READ architecture CONSTANT)
    Q_PROPERTY(QString processorPrimary READ processorPrimary CONSTANT)
    Q_PROPERTY(QString processorSuffix READ processorSuffix CONSTANT)
    Q_PROPERTY(QString graphicsPrimary READ graphicsPrimary CONSTANT)
    Q_PROPERTY(QString graphicsSuffix READ graphicsSuffix CONSTANT)
    Q_PROPERTY(QString memoryPrimary READ memoryPrimary CONSTANT)
    Q_PROPERTY(QString memorySuffix READ memorySuffix CONSTANT)
    Q_PROPERTY(QString storagePrimary READ storagePrimary CONSTANT)
    Q_PROPERTY(QString storageSuffix READ storageSuffix CONSTANT)
    Q_PROPERTY(QString displaySummary READ displaySummary CONSTANT)
    Q_PROPERTY(QString currentResolution READ currentResolution NOTIFY displayChanged)
    Q_PROPERTY(QStringList availableResolutions READ availableResolutions NOTIFY displayChanged)
    Q_PROPERTY(QString currentUser READ currentUser CONSTANT)
    Q_PROPERTY(QString hostname READ hostname CONSTANT)

    // Network & Hardware (honest availability)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY hardwareChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY hardwareChanged)
    Q_PROPERTY(QString wifiSsid READ wifiSsid NOTIFY hardwareChanged)
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY hardwareChanged)

    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY hardwareChanged)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY hardwareChanged)
    Q_PROPERTY(QString bluetoothSubtitle READ bluetoothSubtitle NOTIFY hardwareChanged)

    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY hardwareChanged)
    Q_PROPERTY(double outputVolume READ outputVolume NOTIFY hardwareChanged)
    Q_PROPERTY(bool audioMuted READ audioMuted NOTIFY hardwareChanged)
    Q_PROPERTY(QString audioDeviceName READ audioDeviceName NOTIFY hardwareChanged)

    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY hardwareChanged)
    Q_PROPERTY(double brightness READ brightness NOTIFY hardwareChanged)

    // Sound (Figma 71:54)
    Q_PROPERTY(QString alertSound READ alertSound WRITE setAlertSound NOTIFY soundChanged)
    Q_PROPERTY(QStringList alertSoundChoices READ alertSoundChoices CONSTANT)
    Q_PROPERTY(double alertVolume READ alertVolume WRITE setAlertVolume NOTIFY soundChanged)
    Q_PROPERTY(bool startupSound READ startupSound WRITE setStartupSound NOTIFY soundChanged)
    Q_PROPERTY(bool uiSounds READ uiSounds WRITE setUiSounds NOTIFY soundChanged)
    Q_PROPERTY(bool volumeFeedback READ volumeFeedback WRITE setVolumeFeedback NOTIFY soundChanged)

    // Security & Lock
    Q_PROPERTY(int autoLockMinutes READ autoLockMinutes WRITE setAutoLockMinutes NOTIFY securityChanged)

    // Wallpapers
    Q_PROPERTY(QVariantList availableWallpapers READ availableWallpapers CONSTANT)
    Q_PROPERTY(QString wallpaperPath READ wallpaperPath NOTIFY appearanceChanged)
    Q_PROPERTY(QString lockWallpaperPath READ lockWallpaperPath NOTIFY appearanceChanged)

    // Compatibility runtimes & Updates
    Q_PROPERTY(QVariantList compatRuntimes READ compatRuntimes CONSTANT)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateChanged)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY updateChanged)

public:
    explicit SettingsController(QObject* parent = nullptr);

    QString deviceName() const { return device_name_; }
    QString osName() const { return os_name_; }
    QString osVersion() const { return os_version_; }
    QString kernelVersion() const { return kernel_version_; }
    QString architecture() const { return arch_; }
    QString processorPrimary() const { return cpu_primary_; }
    QString processorSuffix() const { return cpu_suffix_; }
    QString graphicsPrimary() const { return gpu_primary_; }
    QString graphicsSuffix() const { return gpu_suffix_; }
    QString memoryPrimary() const { return mem_primary_; }
    QString memorySuffix() const { return mem_suffix_; }
    QString storagePrimary() const { return disk_primary_; }
    QString storageSuffix() const { return disk_suffix_; }
    QString displaySummary() const { return display_summary_; }
    QString currentResolution() const { return current_resolution_; }
    QStringList availableResolutions() const { return available_resolutions_; }
    QString currentUser() const { return current_user_; }
    QString hostname() const { return hostname_; }

    bool wifiAvailable() const { return services_.network().wifi_hardware; }
    bool wifiEnabled() const { return services_.network().wifi_enabled; }
    QString wifiSsid() const;
    QVariantList wifiNetworks() const;

    bool bluetoothAvailable() const { return services_.bluetooth().available; }
    bool bluetoothEnabled() const { return services_.bluetooth().powered; }
    QString bluetoothSubtitle() const;

    bool audioAvailable() const { return services_.audio().available; }
    double outputVolume() const { return services_.audio().volume_percent / 100.0; }
    bool audioMuted() const { return services_.audio().muted; }
    QString audioDeviceName() const;

    bool brightnessAvailable() const { return services_.backlight().available; }
    double brightness() const { return services_.backlight().percent / 100.0; }

    QString alertSound() const { return alert_sound_; }
    void setAlertSound(const QString& sound);
    QStringList alertSoundChoices() const;

    double alertVolume() const { return alert_volume_; }
    void setAlertVolume(double v);

    bool startupSound() const { return startup_sound_; }
    void setStartupSound(bool on);

    bool uiSounds() const { return ui_sounds_; }
    void setUiSounds(bool on);

    bool volumeFeedback() const { return volume_feedback_; }
    void setVolumeFeedback(bool on);

    int autoLockMinutes() const { return auto_lock_minutes_; }
    void setAutoLockMinutes(int minutes);

    QVariantList availableWallpapers() const { return wallpapers_; }
    QString wallpaperPath() const;
    QString lockWallpaperPath() const;

    QVariantList compatRuntimes() const { return compat_runtimes_; }
    QString updateStatus() const { return update_status_; }
    bool updateChecking() const { return update_checking_; }

    Q_INVOKABLE void refreshHardware();
    Q_INVOKABLE void toggleWifi();
    Q_INVOKABLE void scanWifi();
    Q_INVOKABLE void toggleBluetooth();
    Q_INVOKABLE void setOutputVolume(double v);
    Q_INVOKABLE void setAudioMuted(bool m);
    Q_INVOKABLE void setBrightnessLevel(double v);
    Q_INVOKABLE void applyResolution(const QString& resolution);
    Q_INVOKABLE void previewAlertSound();
    Q_INVOKABLE void applyWallpaper(const QString& path);
    Q_INVOKABLE void applyLockWallpaper(const QString& path);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void copySystemSummary();

signals:
    void hardwareChanged();
    void displayChanged();
    void soundChanged();
    void securityChanged();
    void appearanceChanged();
    void updateChanged();
    void toastRequested(const QString& message, const QString& severity);

private:
    void probeHardwareStatic();
    void probeDisplayModes();
    void probeWallpapers();
    void probeCompatRuntimes();
    void loadPersistedSettings();

    finn::sys::FinnDesktopServices services_;

    QString device_name_;
    QString os_name_;
    QString os_version_;
    QString kernel_version_;
    QString arch_;
    QString cpu_primary_;
    QString cpu_suffix_;
    QString gpu_primary_;
    QString gpu_suffix_;
    QString mem_primary_;
    QString mem_suffix_;
    QString disk_primary_;
    QString disk_suffix_;
    QString display_summary_;
    QString current_resolution_;
    QStringList available_resolutions_;
    QString current_user_;
    QString hostname_;

    QString alert_sound_;
    double alert_volume_ = 0.72;
    bool startup_sound_ = true;
    bool ui_sounds_ = true;
    bool volume_feedback_ = true;
    int auto_lock_minutes_ = 5;

    QVariantList wallpapers_;
    QVariantList compat_runtimes_;
    QString update_status_;
    bool update_checking_ = false;
};

}  // namespace finn::apps

#endif  // FINN_APPS_SETTINGS_CONTROLLER_HPP
