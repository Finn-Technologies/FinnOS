import QtQuick
import FinnUI 1.0

// Adaptive FinnOS Hero Card (Figma Settings - About 25:145 + optional action banner).
Item {
    id: root
    property string productName: "FinnOS"
    property string editionName: "1.0"
    property string title: ""
    property string subtitle: ""
    property string actionText: ""
    property bool tvMode: false

    signal actionClicked()

    readonly property bool bannerMode: title.length > 0 || subtitle.length > 0 || actionText.length > 0

    implicitWidth: 258
    implicitHeight: bannerMode ? (tvMode ? 116 : 92) : 150

    FinnSquircle {
        anchors.fill: parent
        radius: bannerMode ? FinnTheme.radiusLg : FinnTheme.radiusSm
        color: FinnTheme.tile
        borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0E000000"
        borderWidth: 0.5
        specularRim: false
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: (bannerMode ? FinnTheme.radiusLg : FinnTheme.radiusSm) - 1
        opacity: 0.90
        gradient: Gradient {
            GradientStop { position: 0.0; color: FinnTheme.tile }
            GradientStop { position: 1.0; color: FinnTheme.heroTint }
        }
    }

    // Centered Figma 25:145 Mark + Wordmark Mode (Regular Fixel Display, zero bold)
    Row {
        visible: !root.bannerMode
        anchors.centerIn: parent
        spacing: 10

        FinnBootMark {
            width: 24
            height: 24
            color: FinnTheme.ink
            anchors.verticalCenter: parent.verticalCenter
        }

        Row {
            spacing: 5
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: root.productName
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Normal
            }

            Text {
                text: root.editionName
                color: FinnTheme.mute
                font.family: FinnTheme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Normal
            }
        }
    }

    // Banner Mode (Mark + Title + Subtitle + Optional Action Button)
    Row {
        visible: root.bannerMode
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 14

        FinnSquircle {
            width: root.tvMode ? 52 : 42
            height: root.tvMode ? 52 : 42
            radius: 12
            anchors.verticalCenter: parent.verticalCenter
            color: FinnTheme.withAlpha(FinnTheme.accent, 0.12)
            borderWidth: 0

            FinnBootMark {
                anchors.centerIn: parent
                width: root.tvMode ? 28 : 22
                height: root.tvMode ? 28 : 22
                color: FinnTheme.accent
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (root.tvMode ? 66 : 56) - (heroBtn.visible ? heroBtn.width + 14 : 0)
            spacing: 4

            Text {
                text: root.title
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 22 : 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.tvMode ? 15 : 12
                elide: Text.ElideRight
                width: parent.width
            }
        }

        FinnButton {
            id: heroBtn
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "secondary"
            tvMode: root.tvMode
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.actionClicked()
        }
    }
}
