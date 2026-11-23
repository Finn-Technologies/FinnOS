import QtQuick

Item {
    id: root
    property string iconName: "info"
    property string headline: "Nothing here"
    property string description: ""
    property string actionText: ""

    signal actionClicked()

    implicitWidth: 320
    implicitHeight: col.implicitHeight + 32

    Column {
        id: col
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 340)
        spacing: 10

        Rectangle {
            width: 48
            height: 48
            radius: FinnTheme.radiusMd
            color: FinnTheme.surfaceInput
            border.color: FinnTheme.hair
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter

            FinnIcon {
                anchors.centerIn: parent
                name: root.iconName
                size: 24
                tint: FinnTheme.mute
            }
        }

        Text {
            text: root.headline
            color: FinnTheme.ink
            font.family: FinnTheme.fontFamily
            font.pixelSize: 15
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width
        }

        Text {
            visible: root.description.length > 0
            text: root.description
            color: FinnTheme.dim
            font.family: FinnTheme.fontFamily
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width
        }

        FinnButton {
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "primary"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: root.actionClicked()
        }
    }
}
