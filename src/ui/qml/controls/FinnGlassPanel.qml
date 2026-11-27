import QtQuick
import FinnUI 1.0

Item {
    id: root
    property real radius: FinnTheme.radiusLg
    property color fillColor: FinnTheme.dark ? "#78101520" : "#78FFFFFF"
    property color borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.14) : "#18000000"
    property real borderWidth: 0.75
    property bool specularRim: false
    property bool shadow: false
    property int shadowAlpha: 0
    property bool backdropBlur: false
    default property alias contentData: contentLayer.data

    FinnSquircle {
        anchors.fill: parent
        color: root.fillColor
        borderColor: root.borderColor
        borderWidth: root.borderWidth
        radius: root.radius
        specularRim: root.specularRim
        shadow: root.shadow
        shadowAlpha: root.shadowAlpha
        backdropBlur: root.backdropBlur
    }

    Item {
        id: contentLayer
        anchors.fill: parent
        anchors.margins: root.shadow ? 6 : 0
    }
}
