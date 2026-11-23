import QtQuick
import FinnUI 1.0

Item {
    id: root
    property string message: ""
    property string severity: "info" // info | success | warning | error
    property bool shown: false

    implicitWidth: Math.min(420, row.implicitWidth + 28)
    implicitHeight: 38
    visible: opacity > 0.01
    opacity: animSpring.value
    transform: Translate { y: (1.0 - animSpring.value) * 14 }

    FinnSpring {
        id: animSpring
        preset: "snappy"
    }
    Component.onCompleted: animSpring.snap(0.0)
    onShownChanged: animSpring.target = shown ? 1.0 : 0.0

    Timer {
        id: hideTimer
        interval: 2800
        onTriggered: root.shown = false
    }

    function showToast(text, sev, durationMs) {
        message = text
        severity = sev || "info"
        shown = true
        hideTimer.interval = durationMs || 2800
        hideTimer.restart()
    }

    FinnGlassPanel {
        anchors.fill: parent
        radius: FinnTheme.radiusMd
        fillColor: FinnTheme.tile
        borderColor: {
            if (root.severity === "error") return FinnTheme.danger
            if (root.severity === "warning") return FinnTheme.warning
            if (root.severity === "success") return FinnTheme.success
            return FinnTheme.hair
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8

            Rectangle {
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (root.severity === "error") return FinnTheme.danger
                    if (root.severity === "warning") return FinnTheme.warning
                    if (root.severity === "success") return FinnTheme.success
                    return FinnTheme.accent
                }
            }

            Text {
                text: root.message
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
