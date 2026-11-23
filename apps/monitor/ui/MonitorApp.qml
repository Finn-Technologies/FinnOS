import QtQuick
import FinnUI 1.0
import FinnMonitor 1.0

// Finn Monitor — Cohesive FinnOS split-pane with real-time frosted sidebar,
// Emerald-Teal telemetry identity, Bento stat cards, live CPU sparkline, and process table.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 940
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 590
    appTitle: "Finn Monitor"
    pageTitle: activeNavId === "overview" ? "System Overview" : "Running Processes"
    activeNavId: "overview"
    mobileDetailOpen: true

    navItems: [
        { id: "overview", label: "Overview", icon: "activity", badgeColor: "#0D9488", section: "monitor" },
        { id: "processes", label: "Processes", icon: "cpu", badgeColor: "#007BFF", section: "monitor" }
    ]

    onSidebarSearchTextChanged: MonitorCtrl.filterText = sidebarSearchText

    Connections {
        target: MonitorCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    headerActions: [
        FinnIconButton {
            iconName: "refresh"
            buttonSize: 28
            iconSize: 15
            onClicked: MonitorCtrl.sampleNow()
        },
        FinnBadge {
            text: MonitorCtrl.processCount + " tasks"
            tone: "accent"
        }
    ]

    // Overview Tab
    Column {
        visible: win.activeNavId === "overview"
        anchors.fill: parent
        spacing: 12

        // 4 Bento Stat Cards
        Grid {
            width: parent.width
            columns: 2
            columnSpacing: 12
            rowSpacing: 12
            readonly property real cardW: (width - 12) / 2

            FinnStatCard {
                width: parent.cardW
                label: "CPU Usage"
                primaryValue: Math.round(MonitorCtrl.cpuPercent) + "%"
                secondaryValue: "Live kernel load"
                iconName: "cpu"
            }
            FinnStatCard {
                width: parent.cardW
                label: "Memory"
                primaryValue: MonitorCtrl.memoryUsedText
                secondaryValue: "of " + MonitorCtrl.memoryTotalText + " (" + Math.round(MonitorCtrl.memoryPercent) + "%)"
                iconName: "hard-drive"
            }
            FinnStatCard {
                width: parent.cardW
                label: "Load Average"
                primaryValue: MonitorCtrl.loadAverageText
                secondaryValue: "1m / 5m / 15m"
                iconName: "activity"
            }
            FinnStatCard {
                width: parent.cardW
                label: "Uptime"
                primaryValue: MonitorCtrl.uptimeText
                secondaryValue: MonitorCtrl.processCount + " active tasks"
                iconName: "power"
            }
        }

        // Live CPU Sparkline Telemetry Card
        FinnCard {
            width: parent.width
            height: 126
            radius: FinnTheme.radiusLg

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                Row {
                    width: parent.width

                    Row {
                        width: parent.width - 76
                        spacing: 8

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: "#0D9488"
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "CPU Activity History"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    FinnBadge {
                        text: Math.round(MonitorCtrl.cpuPercent) + "%"
                        tone: "accent"
                    }
                }

                CpuSparkline {
                    width: parent.width
                    height: parent.height - 30
                    samples: MonitorCtrl.cpuHistory
                    lineColor: "#0D9488"
                }
            }
        }

        // Top Active Processes Card
        FinnCard {
            width: parent.width
            height: Math.max(120, parent.height - 322)
            radius: FinnTheme.radiusLg

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                Row {
                    width: parent.width
                    height: 22
                    anchors.leftMargin: 8

                    Text {
                        text: "Top Processes"
                        color: FinnTheme.mute
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        width: parent.width - 180
                    }
                    Text {
                        text: "PID"
                        color: FinnTheme.mute
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 11
                        width: 60
                    }
                    Text {
                        text: "CPU"
                        color: FinnTheme.mute
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 11
                        width: 48
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        text: "Memory"
                        color: FinnTheme.mute
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 11
                        width: 66
                        horizontalAlignment: Text.AlignRight
                    }
                }

                FinnDivider { width: parent.width }

                ListView {
                    width: parent.width
                    height: parent.height - 28
                    model: MonitorCtrl.processes.slice(0, 8)
                    clip: true
                    spacing: 2

                    delegate: Item {
                        width: ListView.view.width
                        height: 30

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 8

                            Text {
                                text: modelData.name
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                width: Math.max(80, parent.width - 208)
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.pid
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontMono
                                font.pixelSize: 11
                                width: 60
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.cpuText
                                color: "#0D9488"
                                font.family: FinnTheme.fontMono
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                width: 48
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.memText
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontMono
                                font.pixelSize: 11
                                width: 68
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }
        }
    }

    // Full Processes Tab
    FinnCard {
        visible: win.activeNavId === "processes"
        anchors.fill: parent
        radius: FinnTheme.radiusLg

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Row {
                width: parent.width
                height: 24
                spacing: 8

                Text { text: "Process"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium; width: parent.width - 266; leftPadding: 8 }
                Text { text: "PID"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 11; width: 60 }
                Text { text: "CPU"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 11; width: 48; horizontalAlignment: Text.AlignRight }
                Text { text: "RAM"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 11; width: 64; horizontalAlignment: Text.AlignRight }
                Text { text: "Action"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 11; width: 62; horizontalAlignment: Text.AlignRight }
            }

            FinnDivider { width: parent.width }

            ListView {
                width: parent.width
                height: parent.height - 30
                model: MonitorCtrl.processes
                clip: true
                spacing: 2

                delegate: Item {
                    width: ListView.view.width
                    height: 38

                    FinnSquircle {
                        anchors.fill: parent
                        radius: 8
                        color: pMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                        borderWidth: 0
                    }

                    MouseArea {
                        id: pMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Text {
                            text: modelData.name
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            width: Math.max(80, parent.width - 266)
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.pid
                            color: FinnTheme.mute
                            font.family: FinnTheme.fontMono
                            font.pixelSize: 11
                            width: 60
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.cpuText
                            color: "#0D9488"
                            font.family: FinnTheme.fontMono
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            width: 48
                            horizontalAlignment: Text.AlignRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.memText
                            color: FinnTheme.dim
                            font.family: FinnTheme.fontMono
                            font.pixelSize: 11
                            width: 64
                            horizontalAlignment: Text.AlignRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        FinnButton {
                            text: "End"
                            variant: "danger"
                            implicitHeight: 24
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: MonitorCtrl.terminatePid(modelData.pid)
                        }
                    }
                }
            }
        }
    }
}
