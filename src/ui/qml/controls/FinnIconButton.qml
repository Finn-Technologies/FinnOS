import QtQuick
import FinnUI 1.0

Item {
    id: root
    property string iconName: ""
    property int buttonSize: 32
    property int iconSize: 18
    property real radius: FinnTheme.radiusMd
    property bool active: false
    property bool dangerHover: false
    property bool showDot: false
    property bool monochrome: true
    property color iconColor: active ? FinnTheme.on : FinnTheme.ink
    property string tooltipText: ""

    signal clicked()

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    width: buttonSize
    height: buttonSize
    activeFocusOnTab: enabled
    opacity: enabled ? 1.0 : 0.4

    FinnSpring {
        id: pressSpring
        preset: "bouncy"
    }
    Component.onCompleted: pressSpring.snap(1.0)
    scale: pressSpring.value

    FinnSquircle {
        anchors.fill: parent
        radius: root.radius
        borderWidth: 0
        color: {
            if (!root.enabled) return "transparent"
            if (root.dangerHover && (ma.containsMouse || ma.containsPress)) {
                return ma.containsPress ? Qt.darker(FinnTheme.dangerHover, 1.15) : FinnTheme.dangerHover
            }
            if (ma.containsPress) return FinnTheme.surfacePressed
            if (root.active) return FinnTheme.tileOn
            if (ma.containsMouse) return FinnTheme.surfaceHover
            return "transparent"
        }
    }

    FinnFocusRing {
        shown: root.activeFocus
        radiusValue: root.radius
    }

    FinnIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: root.iconSize
        monochrome: root.monochrome
        tint: (root.dangerHover && (ma.containsMouse || ma.containsPress))
            ? "#FFFFFF"
            : root.iconColor
    }

    Rectangle {
        visible: root.showDot
        width: 4
        height: 4
        radius: 2
        color: FinnTheme.accent
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { pressSpring.preset = "snappy"; pressSpring.target = 0.92 }
        onReleased: { pressSpring.preset = "bouncy"; pressSpring.target = 1.0 }
        onCanceled: { pressSpring.preset = "snappy"; pressSpring.target = 1.0 }
        onClicked: root.clicked()
    }

    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
}
