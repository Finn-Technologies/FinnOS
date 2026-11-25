import QtQuick
import FinnUI 1.0
import FinnTerminal 1.0

// Finn Terminal — Full-space KDE Konsole-style tabbed terminal (NO sidebar).
// Translucent obsidian glass window over real-time compositor backdrop blur,
// with multi-tab persistent PTY sessions and integrated top tab strip.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 880
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 540
    appTitle: "Finn Terminal"
    pageTitle: ""
    navItems: []
    showSidebarSearch: false
    fullBleedContent: true
    windowContentColor: "#B80E121B"

    property int nextTabId: 2
    property int activeTabIndex: 0
    property int termFontSize: 13
    property bool splitView: false

    ListModel {
        id: tabsModel
        ListElement { tabId: 1; label: "Shell 1" }
    }

    function addTab() {
        const id = nextTabId++
        tabsModel.append({ tabId: id, label: "Shell " + id })
        activeTabIndex = tabsModel.count - 1
    }

    function closeTab(idx) {
        if (tabsModel.count <= 1) {
            win.close()
            return
        }
        tabsModel.remove(idx)
        if (activeTabIndex >= tabsModel.count) {
            activeTabIndex = tabsModel.count - 1
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // Integrated KDE Konsole-style Top Tab Bar (sits cleanly left of floating WindowOptions pill)
        Item {
            id: tabBar
            width: parent.width
            height: 40

            Rectangle {
                anchors.fill: parent
                color: "#22FFFFFF"
                opacity: 0.25
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: tabActions.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                clip: true

                Repeater {
                    model: tabsModel
                    delegate: Item {
                        width: Math.min(168, Math.max(96, tabLabel.implicitWidth + 46))
                        height: 28
                        readonly property bool isSel: index === win.activeTabIndex

                        FinnSquircle {
                            anchors.fill: parent
                            radius: 8
                            color: isSel ? "#2EFFFFFF" : (tabMa.containsMouse ? "#18FFFFFF" : "transparent")
                            borderColor: isSel ? "#30FFFFFF" : "transparent"
                            borderWidth: isSel ? 0.6 : 0
                        }

                        MouseArea {
                            id: tabMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.activeTabIndex = index
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 6

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: isSel ? "#10B981" : "#64748B"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                id: tabLabel
                                text: model.label
                                color: isSel ? "#F8FAFC" : "#94A3B8"
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: isSel ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                width: Math.max(40, parent.width - 30)
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Item {
                                width: 16
                                height: 16
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tabsModel.count > 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "×"
                                    color: closeMa.containsMouse ? "#F8FAFC" : "#64748B"
                                    font.pixelSize: 13
                                }

                                MouseArea {
                                    id: closeMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.closeTab(index)
                                }
                            }
                        }
                    }
                }

                // New Tab (+) button
                Item {
                    width: 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter

                    FinnSquircle {
                        anchors.fill: parent
                        radius: 7
                        color: addMa.containsMouse ? "#24FFFFFF" : "transparent"
                        borderWidth: 0
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: "#CBD5E1"
                        font.pixelSize: 16
                        font.weight: Font.Normal
                    }

                    MouseArea {
                        id: addMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.addTab()
                    }
                }
            }

            // Compact Konsole split, font & clear controls right next to the floating WindowOptions pill
            Row {
                id: tabActions
                anchors.right: parent.right
                anchors.rightMargin: 102
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Text {
                    text: win.splitView ? "Unsplit" : "Split"
                    color: win.splitView ? "#38BDF8" : "#94A3B8"
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 11
                    font.weight: win.splitView ? Font.Medium : Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.splitView = !win.splitView
                    }
                }

                Text {
                    text: "A-"
                    color: "#94A3B8"
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.termFontSize = Math.max(9, win.termFontSize - 1)
                    }
                }

                Text {
                    text: "A+"
                    color: "#94A3B8"
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.termFontSize = Math.min(24, win.termFontSize + 1)
                    }
                }

                Text {
                    text: "Clear"
                    color: "#94A3B8"
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const item = termRepeater.itemAt(win.activeTabIndex)
                            if (item) item.clearScreen()
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#1CFFFFFF"
            }
        }

        // Full-space Terminal Viewport (100% of remaining window area, with optional Konsole Split Pane)
        Row {
            width: parent.width
            height: parent.height - tabBar.height
            spacing: 0

            Item {
                width: win.splitView ? parent.width * 0.5 : parent.width
                height: parent.height

                Repeater {
                    id: termRepeater
                    model: tabsModel
                    delegate: FinnTerminalView {
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: index === win.activeTabIndex
                        focus: index === win.activeTabIndex
                        fontSize: win.termFontSize
                        Component.onCompleted: startShell()
                        onTitleChanged: {
                            if (title.length > 0) {
                                tabsModel.setProperty(index, "label", title)
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: win.splitView
                width: 1
                height: parent.height
                color: "#26FFFFFF"
            }

            Item {
                visible: win.splitView
                width: win.splitView ? (parent.width * 0.5 - 1) : 0
                height: parent.height

                FinnTerminalView {
                    id: splitTermView
                    anchors.fill: parent
                    anchors.margins: 8
                    fontSize: win.termFontSize
                    property bool started: false
                    onVisibleChanged: {
                        if (visible && !started) {
                            started = true
                            startShell()
                        }
                    }
                }
            }
        }
    }
}
