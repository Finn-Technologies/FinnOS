import QtQuick
import FinnUI 1.0

// Exact Figma Notifications Component Set (2:169):
// 340x80 cards, r=20, cornerSmoothing=0.6, Light #FFFFFF@70% / Dark #414141@70%,
// with genuine receding 3-card stack (scale 1.0 / 0.90 / 0.81, vertical offset 0 / 15 / 29px).
Item {
    id: root
    property string formFactor: "desktop"
    property bool expanded: false

    readonly property int cardCount: Math.min(3, ShellNotifications.count)
    implicitHeight: expanded ? (cardCount * 88) : (cardCount <= 1 ? 84 : (cardCount === 2 ? 96 : 108))

    Repeater {
        model: ShellNotifications
        delegate: Item {
            id: cardWrap
            visible: index < 3
            readonly property int depthIdx: index
            readonly property real targetScale: root.expanded ? 1.0 : (depthIdx === 0 ? 1.0 : (depthIdx === 1 ? 0.90 : 0.81))
            readonly property real targetY: root.expanded ? (depthIdx * 88) : (depthIdx === 0 ? 0 : (depthIdx === 1 ? 15 : 29))

            width: root.width
            height: 80
            y: targetY
            scale: targetScale
            z: 10 - depthIdx
            opacity: depthIdx === 0 ? 1.0 : (root.expanded ? 1.0 : (depthIdx === 1 ? 0.92 : 0.82))
            transformOrigin: Item.Top

            Behavior on y {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: FinnTheme.motionNote; easing.type: Easing.OutCubic }
            }
            Behavior on scale {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: FinnTheme.motionNote; easing.type: Easing.OutCubic }
            }

            FinnGlassPanel {
                anchors.fill: parent
                radius: FinnTheme.radiusXl
                backdropBlur: !ShellCorona.waylandSession
                fillColor: depthIdx === 0 || root.expanded
                    ? FinnTheme.withAlpha(FinnTheme.dark ? "#161B26" : "#FFFFFF", FinnTheme.dark ? 0.68 : 0.64)
                    : FinnTheme.notifGlass
                borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.14) : FinnTheme.withAlpha("#000000", 0.08)
                shadow: true
                shadowAlpha: 14

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    opacity: (depthIdx === 0 || root.expanded) ? 1.0 : 0.0
                    Behavior on opacity {
                        enabled: !FinnTheme.reducedMotion
                        NumberAnimation { duration: FinnTheme.motionFast }
                    }

                    // Left 40x40 app icon tile (Figma 2:177, r=10)
                    Rectangle {
                        width: 40
                        height: 40
                        radius: 10
                        anchors.verticalCenter: parent.verticalCenter
                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.14) : FinnTheme.withAlpha("#000000", 0.06)

                        FinnIcon {
                            anchors.centerIn: parent
                            name: icon
                            size: 26
                            monochrome: false
                        }
                    }

                    // Middle Title (16px) + Description (14px) (Figma 2:183, 2:184)
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(80, parent.width - 104)
                        spacing: 3

                        Text {
                            text: summary
                            color: FinnTheme.dark ? "#FFFFFF" : "#000000"
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 15
                            font.weight: Font.Normal
                            elide: Text.ElideRight
                            width: parent.width
                        }

                        Text {
                            text: body
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.72) : FinnTheme.withAlpha("#000000", 0.70)
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }

                    // Right timestamp + 32x32 dismiss button (Figma 2:179, 2:181)
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Text {
                            text: timeLabel
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.70) : FinnTheme.withAlpha("#000000", 0.70)
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        FinnIconButton {
                            iconName: "x"
                            buttonSize: 30
                            iconSize: 14
                            radius: FinnTheme.radiusMd
                            iconColor: FinnTheme.dark ? "#FFFFFF" : "#000000"
                            onClicked: ShellNotifications.dismiss(notifId)
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                z: -1
                onClicked: {
                    if (root.cardCount > 1) root.expanded = !root.expanded
                }
            }
        }
    }
}
