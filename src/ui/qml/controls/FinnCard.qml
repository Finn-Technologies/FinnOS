import QtQuick
import FinnUI 1.0

Item {
    id: root
    property real radius: FinnTheme.radiusSm
    property bool clickable: false
    property bool selected: false
    property bool tvFocusMode: false
    property color fillColor: selected ? FinnTheme.tileOn : FinnTheme.tile
    property color borderColor: selected ? FinnTheme.accent : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0E000000")
    property real borderWidth: selected ? 1.4 : 0.5
    default property alias contentData: body.data

    signal clicked()

    activeFocusOnTab: clickable
    focus: clickable

    FinnSpring {
        id: scaleSpring
        preset: "bouncy"
    }
    Component.onCompleted: scaleSpring.snap(1.0)

    scale: scaleSpring.value

    onActiveFocusChanged: {
        if (tvFocusMode) {
            scaleSpring.target = activeFocus ? 1.05 : 1.0
        }
    }

    FinnSquircle {
        anchors.fill: parent
        color: mouseArea.containsPress && root.clickable
            ? FinnTheme.surfacePressed
            : (mouseArea.containsMouse && root.clickable
                ? Qt.tint(root.fillColor, FinnTheme.surfaceHover)
                : root.fillColor)
        borderColor: root.activeFocus ? FinnTheme.accent : root.borderColor
        borderWidth: root.activeFocus ? (root.tvFocusMode ? 3.0 : 1.8) : root.borderWidth
        radius: root.radius
        specularRim: false
        shadow: root.tvFocusMode && root.activeFocus
        shadowAlpha: 26
    }

    FinnFocusRing {
        shown: root.activeFocus && root.clickable
        radiusValue: root.radius
        ringWidth: root.tvFocusMode ? 3 : 2
    }

    Item {
        id: body
        anchors.fill: parent
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: root.clickable
        hoverEnabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: {
            scaleSpring.preset = "snappy"
            scaleSpring.target = 0.97
        }
        onReleased: {
            scaleSpring.preset = "bouncy"
            scaleSpring.target = (root.tvFocusMode && root.activeFocus) ? 1.05 : 1.0
        }
        onCanceled: {
            scaleSpring.preset = "snappy"
            scaleSpring.target = 1.0
        }
        onClicked: root.clicked()
    }

    Keys.onReturnPressed: if (clickable) clicked()
    Keys.onSpacePressed: if (clickable) clicked()
}
