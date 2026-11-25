import QtQuick
import FinnUI 1.0

// Exact Figma Home Menu (4:24 / 4:60: 585x460, r=12, glass panel + 50px frosted footer)
Item {
    id: root
    property bool shown: false
    property string activeCategory: ""

    width: 585
    height: 460
    visible: openSpring.value > 0.01
    opacity: Math.max(0.0, Math.min(1.0, openSpring.value))
    transform: Translate { y: (1.0 - openSpring.value) * 14 }
    scale: 0.97 + 0.03 * openSpring.value

    FinnSpring {
        id: openSpring
        preset: "snappy"
    }
    Component.onCompleted: openSpring.snap(shown ? 1.0 : 0.0)
    onShownChanged: {
        if (!shown) activeCategory = ""
        openSpring.target = shown ? 1.0 : 0.0
    }

    readonly property var favorites: ShellApps.favoriteApps(10)
    readonly property var folders: ShellApps.categoryFolders()
    readonly property var recents: ShellApps.recentApps(8)

    FinnGlassPanel {
        anchors.fill: parent
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        // Consume clicks inside popover so backdrop doesn't close it
        MouseArea { anchors.fill: parent }

        // Main content area (height 410px above 50px footer)
        Item {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 410

            // ---- Top-Left: Your favorite apps (Figma 4:109, 4:196) ----
            Text {
                x: 15
                y: 16
                text: "Your favorite apps"
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 15
            }

            Grid {
                x: 15
                y: 44
                width: 270
                columns: 2
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: root.favorites
                    delegate: Item {
                        width: 130
                        height: 32

                        FinnSquircle {
                            anchors.fill: parent
                            radius: FinnTheme.radiusMd
                            color: favMa.containsPress ? FinnTheme.surfacePressed : (favMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                            borderWidth: 0
                        }

                        Row {
                            anchors.fill: parent
                            spacing: 10

                            FinnIcon {
                                name: modelData.icon
                                size: 32
                                monochrome: false
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: modelData.name
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Normal
                                elide: Text.ElideRight
                                width: 96
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: favMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellCorona.launchApp(modelData.appId)
                        }
                    }
                }
            }

            // ---- Vertical Divider (Figma 4:100: x=293, y=96, h=100) ----
            Rectangle {
                x: 293
                y: 96
                width: 1
                height: 100
                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.35) : FinnTheme.withAlpha("#000000", 0.35)
            }

            // ---- Top-Right: All Apps category folders (Figma 4:69, 4:187) ----
            Row {
                x: 310
                y: 16
                spacing: 8

                FinnIconButton {
                    visible: root.activeCategory.length > 0
                    iconName: "chevron-left"
                    buttonSize: 22
                    iconSize: 14
                    onClicked: root.activeCategory = ""
                }

                Text {
                    text: root.activeCategory.length > 0 ? root.activeCategory : "All Apps"
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 15
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Folder grid (3x2, 80x80 tiles r=14, Figma 4:132)
            Grid {
                visible: root.activeCategory.length === 0
                x: 310
                y: 44
                columns: 3
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: root.folders
                    delegate: Column {
                        id: folderDelegate
                        readonly property var folderIcons: modelData.icons
                        readonly property string folderName: modelData.name
                        width: 80
                        spacing: 5

                        Item {
                            width: 80
                            height: 80

                            FinnSquircle {
                                anchors.fill: parent
                                radius: FinnTheme.radiusFolder
                                color: fMa.containsMouse
                                    ? FinnTheme.tileOn
                                    : FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.72 : 0.85)
                                borderWidth: 0
                                specularRim: false
                            }

                            // Empty category subtle glyph (never fake/pad unrelated app icons)
                            FinnIcon {
                                visible: folderDelegate.folderIcons.length === 0
                                anchors.centerIn: parent
                                size: 26
                                name: folderDelegate.folderName === "Internet" ? "globe" : "grid"
                                tint: FinnTheme.mute
                                opacity: 0.45
                            }

                            // Exact Figma 4:133 folder layout: 3x 32x32 icons at (6,6), (42,6), (6,42) + 2x2 15x15 cluster at (42,42)
                            FinnIcon {
                                visible: folderDelegate.folderIcons.length > 0
                                x: 6; y: 6; size: 32; monochrome: false
                                name: folderDelegate.folderIcons.length > 0 ? folderDelegate.folderIcons[0] : ""
                            }
                            FinnIcon {
                                visible: folderDelegate.folderIcons.length > 1
                                x: 42; y: 6; size: 32; monochrome: false
                                name: folderDelegate.folderIcons.length > 1 ? folderDelegate.folderIcons[1] : ""
                            }
                            FinnIcon {
                                visible: folderDelegate.folderIcons.length > 2
                                x: 6; y: 42; size: 32; monochrome: false
                                name: folderDelegate.folderIcons.length > 2 ? folderDelegate.folderIcons[2] : ""
                            }
                            Repeater {
                                model: Math.max(0, Math.min(4, folderDelegate.folderIcons.length - 3))
                                delegate: FinnIcon {
                                    x: 42 + (index % 2) * 17
                                    y: 42 + Math.floor(index / 2) * 17
                                    size: 15
                                    monochrome: false
                                    name: folderDelegate.folderIcons[3 + index]
                                }
                            }

                            MouseArea {
                                id: fMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.activeCategory = folderDelegate.folderName
                            }
                        }

                        Text {
                            width: 80
                            horizontalAlignment: Text.AlignHCenter
                            text: folderDelegate.folderName
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // Category drill-down list
            Column {
                visible: root.activeCategory.length > 0
                x: 310
                y: 46
                width: 260
                spacing: 6

                Repeater {
                    model: root.activeCategory.length > 0 ? ShellApps.appsInCategory(root.activeCategory) : []
                    delegate: Item {
                        width: 260
                        height: 36

                        FinnSquircle {
                            anchors.fill: parent
                            radius: FinnTheme.radiusMd
                            color: catMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                            borderWidth: 0
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            spacing: 10

                            FinnIcon {
                                name: modelData.icon
                                size: 28
                                monochrome: false
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: modelData.name
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 14
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: catMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellCorona.launchApp(modelData.appId)
                        }
                    }
                }
            }

            // ---- Bottom Section: Recent activity (Figma 4:369: y=268, 4x2 grid) ----
            Text {
                x: 15
                y: 268
                text: "Recent activity"
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 15
            }

            Grid {
                x: 15
                y: 296
                width: 555
                columns: 4
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: root.recents
                    delegate: Item {
                        width: 131
                        height: 36

                        FinnSquircle {
                            anchors.fill: parent
                            radius: FinnTheme.radiusMd
                            color: recMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                            borderWidth: 0
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            spacing: 8

                            FinnIcon {
                                name: modelData.icon
                                size: 28
                                monochrome: false
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 88
                                spacing: 1

                                Text {
                                    text: modelData.name
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    width: parent.width
                                }

                                Text {
                                    text: modelData.category
                                    color: FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }
                        }

                        MouseArea {
                            id: recMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ShellCorona.launchApp(modelData.appId)
                        }
                    }
                }
            }
        }

        // ---- Exact 50px Frosted Footer Bar (Figma 4:219: Avatar + Name | Restart, Power — zero divider line) ----
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 50
            color: FinnTheme.glassFooter

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 15
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                FinnAvatar {
                    width: 30
                    height: 30
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: ShellCorona.currentDisplayName
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 15
                anchors.verticalCenter: parent.verticalCenter
                spacing: 15

                FinnIconButton {
                    iconName: "restart"
                    buttonSize: 28
                    iconSize: 22
                    onClicked: ShellCorona.reboot()
                }

                FinnIconButton {
                    iconName: "power"
                    buttonSize: 28
                    iconSize: 22
                    dangerHover: true
                    onClicked: ShellCorona.powerOff()
                }
            }
        }
    }
}
