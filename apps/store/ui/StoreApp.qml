import QtQuick
import FinnUI 1.0

// Finn Store — Cohesive FinnOS split-pane with real-time frosted sidebar,
// editorial Indigo-Blue hero banner, and refined app catalog cards.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 940
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 590
    appTitle: "Finn Store"
    pageTitle: activeNavId === "discover" ? "Discover"
             : activeNavId === "installed" ? "Installed Apps"
             : activeNavId === "runtimes" ? "App Runtimes"
             : "FinnSDK Developer Hub"
    activeNavId: "discover"
    mobileDetailOpen: true

    navItems: [
        { id: "discover", label: "Discover", icon: "compass", badgeColor: "#2557EB", section: "store" },
        { id: "installed", label: "Installed", icon: "grid", badgeColor: "#059669", section: "store" },
        { id: "runtimes", label: "Runtimes", icon: "layers", badgeColor: "#6366F1", section: "system" },
        { id: "sdk", label: "FinnSDK", icon: "code-01", badgeColor: "#1E293B", section: "system" }
    ]

    onSidebarSearchTextChanged: StoreCtrl.searchQuery = sidebarSearchText

    Connections {
        target: StoreCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    headerActions: [
        FinnBadge {
            text: StoreCtrl.installedCount + " installed"
            tone: "success"
        },
        FinnIconButton {
            iconName: "refresh"
            buttonSize: 28
            iconSize: 15
            onClicked: StoreCtrl.reload()
        }
    ]

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: bodyCol.implicitHeight + 24
        clip: true

        Column {
            id: bodyCol
            width: parent.width
            spacing: 14

            // Discover / Installed App Grid
            Column {
                visible: win.activeNavId === "discover" || win.activeNavId === "installed"
                width: parent.width
                spacing: 14

                FinnHeroCard {
                    visible: win.activeNavId === "discover"
                    width: parent.width
                    title: "Native FinnOS Apps"
                    subtitle: StoreCtrl.installedCount + " of " + StoreCtrl.totalCount + " preinstalled & registered apps ready on this machine"
                    actionText: "Refresh"
                    onActionClicked: StoreCtrl.reload()
                }

                Text {
                    text: win.activeNavId === "installed" ? "Installed on This Machine" : "Featured System Apps"
                    color: FinnTheme.mute
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Grid {
                    width: parent.width
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 12
                    readonly property real cellW: (width - (columns - 1) * 12) / columns

                    Repeater {
                        model: win.activeNavId === "installed" ? StoreCtrl.installedApps : StoreCtrl.catalogApps
                        delegate: FinnCard {
                            width: parent.cellW
                            height: 92
                            radius: 14
                            clickable: true
                            onClicked: StoreCtrl.launchApp(modelData.appId)

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                FinnIcon {
                                    name: modelData.icon
                                    size: 42
                                    monochrome: false
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 134
                                    spacing: 3

                                    Text {
                                        text: modelData.name
                                        color: FinnTheme.ink
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    Text {
                                        text: modelData.note
                                        color: FinnTheme.dim
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    FinnBadge {
                                        text: modelData.source
                                        tone: modelData.installed ? "success" : "neutral"
                                    }
                                }

                                FinnButton {
                                    text: modelData.installed ? "Open" : "N/A"
                                    enabled: modelData.installed
                                    variant: modelData.installed ? "primary" : "secondary"
                                    implicitHeight: 28
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: StoreCtrl.launchApp(modelData.appId)
                                }
                            }
                        }
                    }
                }
            }

            // Runtime Backends
            Column {
                visible: win.activeNavId === "runtimes"
                width: parent.width
                spacing: 10

                Repeater {
                    model: StoreCtrl.runtimeBackends
                    delegate: FinnCard {
                        width: parent.width
                        implicitHeight: rtRow.implicitHeight + 14
                        radius: FinnTheme.radiusLg

                        FinnSettingRow {
                            id: rtRow
                            anchors.fill: parent
                            anchors.margins: 7
                            title: modelData.name
                            subtitle: modelData.description
                            iconName: "layers"
                            FinnBadge {
                                text: modelData.status
                                tone: modelData.available ? "success" : "neutral"
                            }
                        }
                    }
                }
            }

            // FinnSDK Developer Info
            Column {
                visible: win.activeNavId === "sdk"
                width: parent.width
                spacing: 12

                FinnHeroCard {
                    width: parent.width
                    title: "Build Native Apps with FinnSDK"
                    subtitle: "C++20 + Qt 6 FinnUI — frosted glass split-pane chrome, squircle geometry, and Wayland native performance."
                }

                FinnCard {
                    width: parent.width
                    implicitHeight: sdkCol.implicitHeight + 28
                    radius: FinnTheme.radiusLg

                    Column {
                        id: sdkCol
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        Text {
                            text: "Quick Start"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }

                        FinnSquircle {
                            width: parent.width
                            height: codeTxt.implicitHeight + 22
                            radius: 10
                            color: FinnTheme.surfaceCanvas
                            borderColor: FinnTheme.hairSoft
                            borderWidth: 0.7

                            Text {
                                id: codeTxt
                                anchors.fill: parent
                                anchors.margins: 11
                                text: "Template:      /usr/share/finn/sdk/template\n" +
                                      "CMake package: find_package(FinnSDK REQUIRED)\n" +
                                      "Link target:   FinnSDK::FinnUIQml\n" +
                                      "QML module:    import FinnUI 1.0"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontMono
                                font.pixelSize: 12
                                lineHeight: 1.45
                            }
                        }
                    }
                }
            }
        }
    }
}
