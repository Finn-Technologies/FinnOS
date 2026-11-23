import QtQuick

Rectangle {
    id: ring
    property bool shown: false
    property real radiusValue: FinnTheme.radiusMd
    property int ringWidth: 2

    anchors.fill: parent
    anchors.margins: -ringWidth
    radius: radiusValue + ringWidth
    color: "transparent"
    border.color: FinnTheme.accent
    border.width: ringWidth
    opacity: shown ? 1.0 : 0.0
    visible: opacity > 0.01

    Behavior on opacity {
        enabled: !FinnTheme.reducedMotion
        NumberAnimation { duration: FinnTheme.motionHoverIn; easing.type: Easing.OutCubic }
    }
}
