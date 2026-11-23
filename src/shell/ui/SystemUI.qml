import QtQuick
import QtQuick.Window
import FinnUI 1.0
import FinnShell 1.0

// Unified Adaptive FinnOS System UI (KDE Plasma Corona model).
// Single shell component dynamically adapting across Desktop, Tablet, Mobile,
// and TV (Bigscreen) layouts while sharing wallpaper, notifications, and lock screen.
Window {
    id: root
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1280
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 832
    visible: true
    visibility: ShellCorona.waylandSession ? Window.FullScreen : Window.Windowed
    color: ShellCorona.waylandSession ? "transparent" : (FinnTheme.dark ? "#0B0E14" : "#E8EEF7")
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "FinnOS System UI"

    readonly property string formFactor: (FinnTheme.formFactorSetting === "tablet" || FinnTheme.formFactorSetting === "mobile" || FinnTheme.formFactorSetting === "tv")
        ? FinnTheme.formFactorSetting
        : "desktop"
    readonly property var metrics: FinnTheme.formFactorMetrics(formFactor, width, height)

    // Layer 1: Cover-fitted desktop wallpaper (rendered in standalone/screenshot mode;
    // in live Wayland sessions finn-comp renders the background wallpaper layer behind apps)
    WallpaperLayer {
        anchors.fill: parent
        z: 0
        visible: !ShellCorona.waylandSession
    }

    // Layer 2: Unified Adaptive Shell Form-Factor Containment
    Loader {
        id: shellLoader
        anchors.fill: parent
        z: 10
        opacity: ShellCorona.locked ? 0.0 : 1.0
        visible: opacity > 0.01

        Behavior on opacity {
            enabled: !FinnTheme.reducedMotion
            NumberAnimation { duration: FinnTheme.motionSurfaceClose; easing.type: Easing.OutCubic }
        }

        sourceComponent: {
            if (root.formFactor === "mobile") return mobileComp
            if (root.formFactor === "tablet") return tabletComp
            if (root.formFactor === "tv") return tvComp
            return desktopComp
        }
    }

    Component { id: desktopComp; DesktopShell {} }
    Component { id: tabletComp;  TabletShell {} }
    Component { id: mobileComp;  MobileShell {} }
    Component { id: tvComp;      TvShell {} }

    // Layer 3: Receding 3-Card Figma Notification Stack (top-right on Desktop/Tablet/TV, top-center on Mobile)
    NotificationStack {
        z: 30
        visible: !ShellCorona.locked && ShellNotifications.count > 0
        formFactor: root.formFactor
        anchors.top: parent.top
        anchors.topMargin: root.formFactor === "mobile" ? 42 : (root.formFactor === "tv" ? 84 : 18)
        anchors.right: root.formFactor === "mobile" ? undefined : parent.right
        anchors.rightMargin: root.formFactor === "tv" ? 48 : 18
        anchors.horizontalCenter: root.formFactor === "mobile" ? parent.horizontalCenter : undefined
        width: root.formFactor === "mobile" ? Math.min(360, parent.width - 24) : 340
    }

    // Layer 4: Opaque Session Lock Screen Overlay (Figma 84:389)
    LockScreenOverlay {
        anchors.fill: parent
        z: 50
        formFactor: root.formFactor
        locked: ShellCorona.locked
    }

    // Global keyboard escape to dismiss popovers
    Shortcut {
        sequence: "Escape"
        onActivated: ShellCorona.closePanels()
    }
}
