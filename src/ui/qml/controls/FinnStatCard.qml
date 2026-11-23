import QtQuick
import FinnUI 1.0

// Exact Figma Settings - About stat card (25:136..25:170: 258x70, r=6)
FinnCard {
    id: root
    property string label: ""
    property string primaryValue: ""
    property string mutedSuffix: ""
    property string secondaryValue: ""
    property string iconName: ""
    property bool tvMode: false

    readonly property string effectiveSuffix: mutedSuffix.length > 0 ? mutedSuffix : secondaryValue

    implicitWidth: 258
    implicitHeight: tvMode ? 96 : 70
    radius: FinnTheme.radiusSm
    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0E000000"
    borderWidth: 0.5

    Column {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: root.tvMode ? 14 : 14

        Row {
            width: parent.width
            spacing: 6

            FinnIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                size: root.tvMode ? 18 : 14
                tint: FinnTheme.mute
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.label
                color: FinnTheme.mute
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 16 : 12
                font.weight: Font.Normal
                elide: Text.ElideRight
                width: parent.width - (root.iconName.length > 0 ? 22 : 0)
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            spacing: 5
            width: parent.width
            clip: true

            Text {
                text: root.primaryValue
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 20 : 14
                font.weight: Font.Normal
            }

            Text {
                visible: root.effectiveSuffix.length > 0
                text: root.effectiveSuffix
                color: FinnTheme.mute
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 16 : 14
                font.weight: Font.Normal
                elide: Text.ElideRight
                anchors.baseline: parent.children[0].baseline
            }
        }
    }
}
