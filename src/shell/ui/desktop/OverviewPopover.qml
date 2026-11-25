import QtQuick
import FinnUI 1.0

// Plasma Overview-style Open Windows Switcher Popover
Item {
    id: root
    property bool shown: false

    width: 520 + 12
    height: 300 + 12
    visible: openSpring.value > 0.01
    opacity: Math.max(0.0, Math.min(1.0, openSpring.value))
    transform: Translate { y: (1.0 - openSpring.value) * 14 }

    FinnSpring { id: openSpring; preset: "snappy" }
    Component.onCompleted: openSpring.snap(shown ? 1.0 : 0.0)
    onShownChanged: openSpring.target = shown ? 1.0 : 0.0

    FinnGlassPanel {
        anchors.fill: parent
        radius: FinnTheme.radiusLg
        shadow: true
        backdropBlur: !ShellCorona.waylandSession

        MouseArea { anchors.fill: parent }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "Open Windows"
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 15
            }

            FinnEmptyState {
                visible: ShellCorona.openWindows.length === 0
                width: parent.width
                height: 220
                iconName: "view-open-windows"
                headline: "No open windows"
                description: "Launch an app from the dock or Home menu."
                actionText: "Open Home"
                onActionClicked: ShellCorona.openPanel("home")
            }

            Grid {
                visible: ShellCorona.openWindows.length > 0
                width: parent.width
                columns: 2
                spacing: 12

                Repeater {
                    model: ShellCorona.openWindows
                    delegate: FinnCard {
                        width: 236
                        height: 86
                        clickable: true
                        selected: modelData.activated
                        onClicked: ShellCorona.activateWindow(modelData.id)

                        Row {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            FinnIcon {
                                name: modelData.appId || "window"
                                size: 28
                                monochrome: false
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 138
                                spacing: 2
                                Text {
                                    text: modelData.title
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                                Text {
                                    text: modelData.minimized ? "Minimized" : (modelData.activated ? "Active" : "Background")
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                }
                            }

                            FinnIconButton {
                                iconName: "x"
                                buttonSize: 24
                                iconSize: 12
                                dangerHover: true
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: ShellCorona.closeWindow(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
