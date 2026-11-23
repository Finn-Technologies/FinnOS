import QtQuick
import FinnUI 1.0

// Exact Figma Settings - Sound list row (71:239..71:358: 32-48px tall, hair_soft border)
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string iconName: ""
    property bool grouped: false
    property bool flat: false
    property bool isFirst: true
    property bool isLast: true
    property bool tvMode: false
    default property alias trailingData: trailingSlot.data

    implicitHeight: subtitle.length > 0 ? (tvMode ? 72 : 52) : (tvMode ? 56 : 38)
    implicitWidth: 526

    FinnSquircle {
        visible: !root.flat
        anchors.fill: parent
        color: FinnTheme.tile
        borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0E000000"
        borderWidth: 0.5
        specularRim: false
        radius: (!root.grouped || (root.isFirst && root.isLast)) ? FinnTheme.radiusSm : 2.0
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: root.flat ? 4 : 12
        anchors.rightMargin: root.flat ? 4 : 12
        spacing: 10

        FinnIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: root.tvMode ? 22 : 16
            tint: FinnTheme.dim
            anchors.verticalCenter: parent.verticalCenter
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(60, parent.width - trailingSlot.width - (root.iconName.length > 0 ? 34 : 12))
            spacing: 2

            Text {
                text: root.title
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 20 : 13
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 15 : 11
                elide: Text.ElideRight
                width: parent.width
            }
        }

        Row {
            id: trailingSlot
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
        }
    }
}
