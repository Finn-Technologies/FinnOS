import QtQuick
import FinnUI 1.0

// Dual-mode FinnOS Slider:
// - mode: "pill"    -> Figma Control Center 17:24 / 17:30 (36px tall, #000@50% track, white glowing fill, embedded icon)
// - mode: "compact" -> Figma Settings Sound 71:341 (18px tall pill track, white fill)
Item {
    id: root
    property real value: 0.5 // 0.0 .. 1.0
    property string mode: "pill" // pill | compact
    property string iconName: ""
    property bool interactive: true

    signal moved(real newValue)

    implicitWidth: mode === "pill" ? 432 : 150
    implicitHeight: mode === "pill" ? 36 : 18
    activeFocusOnTab: enabled && interactive
    opacity: 1.0

    FinnSpring {
        id: fillSpring
        preset: "snappy"
    }
    Component.onCompleted: fillSpring.snap(Math.max(0.0, Math.min(1.0, value)))
    onValueChanged: {
        if (!ma.pressed) fillSpring.target = Math.max(0.0, Math.min(1.0, value))
    }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        clip: true
        color: FinnTheme.dark
            ? FinnTheme.withAlpha("#FFFFFF", 0.18)
            : (root.mode === "pill" ? FinnTheme.withAlpha("#000000", 0.50) : FinnTheme.withAlpha("#000000", 0.20))
        border.color: FinnTheme.hairSoft
        border.width: root.mode === "compact" ? 0.5 : 0

        // Filled bar
        Rectangle {
            id: fillBar
            readonly property real minFill: root.height
            width: Math.max(minFill, fillSpring.value * track.width)
            height: track.height
            radius: height / 2
            color: root.mode === "compact"
                ? (FinnTheme.dark ? FinnTheme.accent : "#FFFFFF")
                : "#FFFFFF"
            border.color: (root.mode === "compact" && !FinnTheme.dark) ? FinnTheme.hairSoft : "transparent"
            border.width: (root.mode === "compact" && !FinnTheme.dark) ? 0.6 : 0
        }

        // Embedded icon on left for Control Center pill slider (Figma 17:32, 17:36)
        FinnIcon {
            visible: root.mode === "pill" && root.iconName.length > 0
            name: root.iconName
            size: 20
            tint: "#0F141A"
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    FinnFocusRing {
        shown: root.activeFocus
        radiusValue: root.height / 2
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: root.enabled && root.interactive
        cursorShape: Qt.PointingHandCursor
        function updateFromX(mx) {
            var v = Math.max(0.0, Math.min(1.0, mx / Math.max(1, width)))
            fillSpring.snap(v)
            root.value = v
            root.moved(v)
        }
        onPressed: (mouse) => updateFromX(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) updateFromX(mouse.x) }
    }

    Keys.onLeftPressed: {
        if (!enabled || !interactive) return
        value = Math.max(0.0, value - 0.05)
        moved(value)
    }
    Keys.onRightPressed: {
        if (!enabled || !interactive) return
        value = Math.min(1.0, value + 0.05)
        moved(value)
    }
}
