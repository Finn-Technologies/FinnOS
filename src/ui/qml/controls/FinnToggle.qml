import QtQuick
import FinnUI 1.0

// Exact Figma Settings - Sound Switch/SM (71:350: 30x18 pill, 14x14 thumb, #007BFF on)
// Scales smoothly for Tablet, Mobile, and TV form factors.
Item {
    id: root
    property bool checked: false
    property int trackWidth: 34
    property int trackHeight: 20

    signal toggled(bool checked)

    implicitWidth: trackWidth
    implicitHeight: trackHeight
    width: trackWidth
    height: trackHeight
    activeFocusOnTab: enabled
    opacity: enabled ? 1.0 : 0.45

    FinnSpring {
        id: knobSpring
        preset: "snappy"
    }
    Component.onCompleted: knobSpring.snap(checked ? 1.0 : 0.0)
    onCheckedChanged: knobSpring.target = checked ? 1.0 : 0.0

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? FinnTheme.accent : FinnTheme.withAlpha(FinnTheme.ink, FinnTheme.dark ? 0.22 : 0.18)

        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: FinnTheme.motionHoverIn }
        }
    }

    Rectangle {
        readonly property real inset: 2.0
        readonly property real thumbSize: root.height - inset * 2.0
        width: thumbSize
        height: thumbSize
        radius: thumbSize / 2.0
        y: inset
        x: inset + knobSpring.value * (root.width - thumbSize - inset * 2.0)
        color: "#FCFCFC"
        border.color: FinnTheme.withAlpha("#000000", 0.08)
        border.width: 0.5
    }

    FinnFocusRing {
        shown: root.activeFocus
        radiusValue: root.height / 2
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }

    Keys.onReturnPressed: if (enabled) { checked = !checked; toggled(checked) }
    Keys.onSpacePressed: if (enabled) { checked = !checked; toggled(checked) }
}
