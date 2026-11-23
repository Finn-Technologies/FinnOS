import QtQuick
import FinnUI 1.0

// Exact Figma Spotlight Search (4:226):
// - Top floating pill 300x44 (5:16, r=pill, "Search or ask anything")
// - Below: 350x228 frosted card (5:2, r=12) with Recommendations (5 apps) &
//   Recently asked (2x2), switching seamlessly to live search + math results.
Column {
    id: root
    property bool shown: false
    property string queryText: ""
    property int selectedIndex: 0

    spacing: 14
    width: 350
    visible: openSpring.value > 0.01
    opacity: Math.max(0.0, Math.min(1.0, openSpring.value))
    transform: Translate { y: (1.0 - openSpring.value) * -10 }

    FinnSpring {
        id: openSpring
        preset: "snappy"
    }
    Component.onCompleted: openSpring.snap(shown ? 1.0 : 0.0)
    onShownChanged: {
        openSpring.target = shown ? 1.0 : 0.0
        if (shown) {
            searchInput.text = ""
            queryText = ""
            selectedIndex = 0
            searchInput.focusInput()
        }
    }

    readonly property var liveResults: queryText.trim().length > 0 ? ShellApps.searchQuery(queryText) : []
    readonly property var recApps: ShellApps.favoriteApps(5)

    function activateResult(item) {
        if (!item) return
        if (item.kind === "app") {
            ShellCorona.launchApp(item.appId)
        } else if (item.kind === "toggle_theme") {
            FinnTheme.dark = !FinnTheme.dark
            ShellCorona.closePanels()
        }
    }

    // Search Pill (Figma 5:16: 300x44)
    FinnSearchField {
        id: searchInput
        anchors.horizontalCenter: parent.horizontalCenter
        width: 300
        height: 44
        fontSize: 15
        placeholderText: "Search or ask anything"
        onTextEdited: (t) => {
            root.queryText = t
            root.selectedIndex = 0
        }
        onAccepted: {
            if (root.liveResults.length > 0) {
                root.activateResult(root.liveResults[Math.min(root.selectedIndex, root.liveResults.length - 1)])
            }
        }
    }

    // Recommendations / Live Results Card (Figma 5:2: 350x228, r=12, zero drop shadow)
    FinnGlassPanel {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 350
        height: 228
        radius: FinnTheme.radiusLg
        shadow: false

        MouseArea { anchors.fill: parent }

        // Empty-query Recommendations view
        Column {
            visible: root.queryText.trim().length === 0
            anchors.fill: parent
            anchors.margins: 15
            spacing: 12

            Text {
                text: "Recommendations"
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: 14
                font.weight: Font.Normal
            }

            Row {
                width: parent.width
                spacing: 16

                Repeater {
                    model: root.recApps
                    delegate: Item {
                        width: 52
                        height: 56

                        FinnSquircle {
                            anchors.fill: parent
                            radius: FinnTheme.radiusMd
                            color: recAppMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                            borderWidth: 0
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 5

                            FinnIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: modelData.icon
                                size: 32
                                monochrome: false
                            }

                            Text {
                                width: 62
                                anchors.horizontalCenter: parent.horizontalCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData.name.replace("Finn ", "")
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Normal
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: recAppMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellCorona.launchApp(modelData.appId)
                        }
                    }
                }
            }

            Text {
                text: "Recently asked"
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: 14
                font.weight: Font.Normal
            }

            Grid {
                columns: 2
                columnSpacing: 10
                rowSpacing: 10
                width: parent.width

                Repeater {
                    model: [
                        { label: "Dark / Light mode", icon: "swatch-book", action: "theme" },
                        { label: "System Settings", icon: "settings-app", action: "settings" },
                        { label: "Open Terminal", icon: "terminal", action: "terminal" },
                        { label: "System Monitor", icon: "monitor", action: "monitor" }
                    ]
                    delegate: Item {
                        width: 155
                        height: 28

                        FinnSquircle {
                            anchors.fill: parent
                            radius: 7
                            color: qaMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                            borderWidth: 0
                        }

                        Row {
                            anchors.fill: parent
                            spacing: 10

                            FinnIcon {
                                name: modelData.icon
                                size: 28
                                monochrome: modelData.action === "theme"
                                tint: FinnTheme.ink
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: modelData.label
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Normal
                                elide: Text.ElideRight
                                width: 117
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: qaMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.action === "theme") {
                                    FinnTheme.dark = !FinnTheme.dark
                                    ShellCorona.closePanels()
                                } else {
                                    ShellCorona.launchApp(modelData.action)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Live Results view
        Column {
            visible: root.queryText.trim().length > 0
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4

            Repeater {
                model: root.liveResults
                delegate: Item {
                    width: parent.width
                    height: 38

                    FinnSquircle {
                        anchors.fill: parent
                        radius: FinnTheme.radiusMd
                        color: index === root.selectedIndex ? FinnTheme.tileOn : (resMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                        borderWidth: 0
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10

                        FinnIcon {
                            name: modelData.icon
                            size: 24
                            monochrome: modelData.kind !== "app"
                            tint: FinnTheme.ink
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 42
                            Text {
                                text: modelData.title
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                visible: modelData.subtitle && modelData.subtitle.length > 0
                                text: modelData.subtitle || ""
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }

                    MouseArea {
                        id: resMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activateResult(modelData)
                    }
                }
            }

            FinnEmptyState {
                visible: root.liveResults.length === 0
                width: parent.width
                height: 180
                iconName: "search-01"
                headline: "No matches found"
                description: "Try an app name or arithmetic expression."
            }
        }
    }
}
