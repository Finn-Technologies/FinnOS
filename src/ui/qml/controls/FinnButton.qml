import QtQuick
import FinnUI 1.0

Item {
    id: root
    property string text: ""
    property string iconName: ""
    property string variant: "secondary" // primary | secondary | ghost | danger
    property bool checked: false
    property bool tvMode: false
    property real radius: FinnTheme.radiusSm

    signal clicked()

    implicitHeight: tvMode ? 52 : 34
    implicitWidth: Math.max(tvMode ? 120 : 72, row.implicitWidth + (tvMode ? 36 : 24))
    activeFocusOnTab: enabled
    opacity: enabled ? 1.0 : 0.45

    FinnSpring {
        id: pressSpring
        preset: "bouncy"
    }
    Component.onCompleted: pressSpring.snap(1.0)
    scale: pressSpring.value

    readonly property color bgColor: {
        if (!enabled) return FinnTheme.surfaceCanvas
        if (variant === "primary") {
            if (ma.containsPress) return Qt.darker(FinnTheme.accent, 1.15)
            if (ma.containsMouse || activeFocus) return Qt.lighter(FinnTheme.accent, 1.08)
            return FinnTheme.accent
        }
        if (variant === "danger") {
            if (ma.containsPress) return Qt.darker(FinnTheme.danger, 1.15)
            if (ma.containsMouse) return FinnTheme.dangerHover
            return FinnTheme.danger
        }
        if (checked) return FinnTheme.tileOn
        if (variant === "ghost") {
            if (ma.containsPress) return FinnTheme.surfacePressed
            if (ma.containsMouse || activeFocus) return FinnTheme.surfaceHover
            return "transparent"
        }
        if (ma.containsPress) return FinnTheme.surfacePressed
        if (ma.containsMouse) return Qt.tint(FinnTheme.surfaceInput, FinnTheme.surfaceHover)
        return FinnTheme.surfaceInput
    }

    readonly property color fgColor: {
        if (!enabled) return FinnTheme.mute
        if (variant === "primary" || variant === "danger") return "#FFFFFF"
        if (checked) return FinnTheme.on
        return FinnTheme.ink
    }

    FinnSquircle {
        anchors.fill: parent
        color: root.bgColor
        borderColor: root.checked ? FinnTheme.accent : (root.variant === "ghost" ? "transparent" : FinnTheme.hair)
        borderWidth: root.variant === "primary" || root.variant === "danger" ? 0 : 1
        radius: root.radius
    }

    FinnFocusRing {
        shown: root.activeFocus
        radiusValue: root.radius
        ringWidth: root.tvMode ? 3 : 2
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        FinnIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            tint: root.fgColor
            size: root.tvMode ? 22 : 16
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: root.text.length > 0
            text: root.text
            color: root.fgColor
            font.family: FinnTheme.fontFamily
            font.pixelSize: root.tvMode ? 20 : 13
            font.weight: root.variant === "primary" ? Font.Medium : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { pressSpring.preset = "snappy"; pressSpring.target = 0.95 }
        onReleased: { pressSpring.preset = "bouncy"; pressSpring.target = 1.0 }
        onCanceled: { pressSpring.preset = "snappy"; pressSpring.target = 1.0 }
        onClicked: root.clicked()
    }

    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
}
