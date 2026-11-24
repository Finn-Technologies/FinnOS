// Finn Store — macOS App Store-inspired Native Qt 6 / FinnUI Software Center.
// Clearly separates Native FinnOS, Linux (Flatpak), Android (Aurora/Play),
// Windows (winget/finn-win), and Updates & Installed, with rich detail sheets
// and local --install cross-OS package sideloading.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import FinnUI 1.0

Window {
    id: win
    title: "Finn Store"
    width: 1180
    height: 760
    minimumWidth: 880
    minimumHeight: 580
    visible: true
    color: FinnTheme.bone

    readonly property var navItems: [
        { id: "discover", label: "Discover",            icon: "search-01",  badge: "" },
        { id: "native",   label: "Native FinnOS",       icon: "app-grid",   badge: "Core" },
        { id: "flatpak",  label: "Linux (Flatpak)",     icon: "linux",      badge: "Flathub" },
        { id: "android",  label: "Android",             icon: "android",    badge: "Aurora" },
        { id: "windows",  label: "Windows",             icon: "windows",    badge: "winget" },
        { id: "updates",  label: "Updates & Installed", icon: "download",   badge: StoreCtrl.updatesCount > 0 ? String(StoreCtrl.updatesCount) : "" }
    ]

    function tabTitle(tabId) {
        for (var i = 0; i < navItems.length; ++i) {
            if (navItems[i].id === tabId) return navItems[i].label
        }
        return "Discover"
    }

    function tabSubtitle(tabId) {
        if (tabId === "native")  return "First-party & FinnSDK Wayland applications built natively for FinnOS"
        if (tabId === "flatpak") return "Sandboxed Linux desktop applications powered by Flathub API v2"
        if (tabId === "android") return "Android apps via Aurora anonymous Play API into finn-android"
        if (tabId === "windows") return "Windows apps via microsoft/winget-pkgs (SHA-256 verified) into finn-win"
        if (tabId === "updates") return StoreCtrl.installedCount + " installed apps · " + StoreCtrl.updatesCount + " updates available"
        return "Curated native & cross-OS apps across every ecosystem"
    }

    Connections {
        target: StoreCtrl
        function onToastRequested(message, severity) {
            toast.showToast(message, severity || "info", 2800)
        }
    }

    // Main split layout: macOS App Store frosted sidebar + editorial content
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left Frosted Sidebar ─────────────────────────────────────────────
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            color: FinnTheme.tile

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 1
                color: FinnTheme.hair
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        width: 28; height: 28; radius: 8
                        color: FinnTheme.accent
                        FinnIcon {
                            anchors.centerIn: parent
                            name: "app-grid"
                            size: 16
                            tint: "#ffffff"
                        }
                    }
                    Text {
                        text: "Finn Store"
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 16
                        font.weight: Font.Bold
                    }
                }

                // Store search bar
                FinnSearchField {
                    id: searchBox
                    Layout.fillWidth: true
                    implicitHeight: 34
                    placeholderText: "Search all stores..."
                    text: StoreCtrl.searchQuery
                    onTextEdited: function(t) { StoreCtrl.searchQuery = t }
                    onAccepted: StoreCtrl.searchOnline(text)
                }

                // Navigation list
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: win.navItems
                        delegate: Rectangle {
                            id: navRow
                            required property var modelData
                            readonly property bool active: StoreCtrl.activeTab === modelData.id
                            Layout.fillWidth: true
                            height: 38
                            radius: 10
                            color: active
                                ? Qt.rgba(FinnTheme.accent.r, FinnTheme.accent.g, FinnTheme.accent.b, 0.18)
                                : (navMouse.containsMouse ? FinnTheme.surfaceHover : "transparent")

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 10

                                FinnIcon {
                                    name: navRow.modelData.icon
                                    size: 17
                                    tint: navRow.active ? FinnTheme.accent : FinnTheme.dim
                                }

                                Text {
                                    text: navRow.modelData.label
                                    color: navRow.active ? FinnTheme.ink : FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: navRow.active ? Font.DemiBold : Font.Medium
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    visible: navRow.modelData.badge.length > 0
                                    radius: 8
                                    color: navRow.active ? FinnTheme.accent : FinnTheme.surfaceInput
                                    implicitWidth: badgeTxt.implicitWidth + 12
                                    implicitHeight: 19

                                    Text {
                                        id: badgeTxt
                                        anchors.centerIn: parent
                                        text: navRow.modelData.badge
                                        color: navRow.active ? "#ffffff" : FinnTheme.mute
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }

                            MouseArea {
                                id: navMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.activeTab = navRow.modelData.id
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Runtime status card (honest probe of Flatpak / Android / Windows / macOS)
                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: FinnTheme.surfaceInput
                    border.width: 1
                    border.color: FinnTheme.hair
                    implicitHeight: rtCol.implicitHeight + 20

                    ColumnLayout {
                        id: rtCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        Text {
                            text: "COMPATIBILITY RUNTIMES"
                            color: FinnTheme.mute
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Repeater {
                            model: [
                                { name: "Flatpak", ok: !!StoreCtrl.runtimeStatus.flatpakReady },
                                { name: "Android", ok: !!StoreCtrl.runtimeStatus.androidReady },
                                { name: "Windows", ok: !!StoreCtrl.runtimeStatus.windowsReady },
                                { name: "macOS",   ok: !!StoreCtrl.runtimeStatus.macosReady }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    color: modelData.ok ? "#22c55e" : "#64748b"
                                }
                                Text {
                                    text: modelData.name
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData.ok ? "Ready" : "Offline"
                                    color: modelData.ok ? "#22c55e" : FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Main Content Area ────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: scroller
                anchors.fill: parent
                contentWidth: width
                contentHeight: contentCol.implicitHeight + 56
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: contentCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 28
                    spacing: 24

                    // Page header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: win.tabTitle(StoreCtrl.activeTab)
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 30
                                font.weight: Font.Bold
                            }

                            Text {
                                text: win.tabSubtitle(StoreCtrl.activeTab)
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                            }
                        }

                        FinnButton {
                            visible: StoreCtrl.activeTab === "updates"
                            text: "Check Updates"
                            variant: "secondary"
                            onClicked: StoreCtrl.checkForUpdates()
                        }

                        FinnButton {
                            visible: StoreCtrl.activeTab === "updates" && StoreCtrl.updatesCount > 0
                            text: "Update All"
                            variant: "primary"
                            onClicked: StoreCtrl.updateAll()
                        }

                        FinnButton {
                            visible: StoreCtrl.searchQuery.length >= 2 && StoreCtrl.activeTab !== "native"
                            text: StoreCtrl.busy ? "Searching..." : "Search Online"
                            variant: "primary"
                            enabled: !StoreCtrl.busy
                            onClicked: StoreCtrl.searchOnline(StoreCtrl.searchQuery)
                        }
                    }

                    // ── DISCOVER EDITORIAL VIEW ──────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 24
                        visible: StoreCtrl.activeTab === "discover" && StoreCtrl.searchQuery.trim().length === 0

                        // Featured Editorial Hero Row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 18

                            Repeater {
                                model: StoreCtrl.heroItems
                                delegate: Rectangle {
                                    id: heroCard
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 204
                                    radius: 18
                                    clip: true
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: Qt.darker(heroCard.modelData.accentHex || "#3b82f6", 1.7) }
                                        GradientStop { position: 1.0; color: Qt.darker(heroCard.modelData.accentHex || "#3b82f6", 2.9) }
                                    }
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.14)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 22
                                        spacing: 8

                                        Text {
                                            text: heroCard.modelData.editorialTag || "FEATURED"
                                            color: Qt.rgba(1, 1, 1, 0.78)
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                            font.letterSpacing: 1.0
                                        }

                                        Text {
                                            text: heroCard.modelData.name
                                            color: "#ffffff"
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 24
                                            font.weight: Font.Bold
                                        }

                                        Text {
                                            text: heroCard.modelData.subtitle
                                            color: Qt.rgba(1, 1, 1, 0.85)
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Item { Layout.fillHeight: true }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Rectangle {
                                                radius: 8
                                                color: Qt.rgba(1, 1, 1, 0.16)
                                                implicitWidth: ecoTag.implicitWidth + 14
                                                implicitHeight: 24
                                                Text {
                                                    id: ecoTag
                                                    anchors.centerIn: parent
                                                    text: heroCard.modelData.ecosystemLabel
                                                    color: "#ffffff"
                                                    font.family: FinnTheme.fontFamily
                                                    font.pixelSize: 11
                                                    font.weight: Font.Medium
                                                }
                                            }

                                            Item { Layout.fillWidth: true }

                                            FinnButton {
                                                text: heroCard.modelData.installed ? "Open" : "Get"
                                                variant: "primary"
                                                onClicked: {
                                                    if (heroCard.modelData.installed)
                                                        StoreCtrl.launchApp(heroCard.modelData.appId)
                                                    else
                                                        StoreCtrl.installApp(heroCard.modelData.appId)
                                                }
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        z: -1
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: StoreCtrl.openAppDetail(heroCard.modelData.appId)
                                    }
                                }
                            }
                        }

                        // Separated Ecosystem Shelves
                        Repeater {
                            model: StoreCtrl.discoverShelves
                            delegate: ColumnLayout {
                                id: shelf
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: shelf.modelData.title
                                            color: FinnTheme.ink
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 18
                                            font.weight: Font.Bold
                                        }
                                        Text {
                                            text: shelf.modelData.subtitle
                                            color: FinnTheme.mute
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 12
                                        }
                                    }
                                    FinnButton {
                                        text: "See All"
                                        variant: "ghost"
                                        onClicked: StoreCtrl.activeTab = shelf.modelData.tabId
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: win.width >= 1060 ? 3 : 2
                                    columnSpacing: 14
                                    rowSpacing: 12

                                    Repeater {
                                        model: shelf.modelData.items
                                        delegate: Rectangle {
                                            id: shelfCard
                                            required property var modelData
                                            Layout.fillWidth: true
                                            implicitHeight: 82
                                            radius: 14
                                            clip: true
                                            color: shelfHover.containsMouse ? FinnTheme.surfaceHover : FinnTheme.tile
                                            border.width: 1
                                            border.color: FinnTheme.hair

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 12
                                                spacing: 10

                                                Rectangle {
                                                    width: 46; height: 46; radius: 12
                                                    Layout.preferredWidth: 46
                                                    Layout.preferredHeight: 46
                                                    color: FinnTheme.surfaceInput
                                                    FinnIcon {
                                                        anchors.centerIn: parent
                                                        name: shelfCard.modelData.iconName
                                                        size: 26
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    spacing: 2
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        Text {
                                                            text: shelfCard.modelData.name
                                                            color: FinnTheme.ink
                                                            font.family: FinnTheme.fontFamily
                                                            font.pixelSize: 13
                                                            font.weight: Font.DemiBold
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                            Layout.minimumWidth: 0
                                                        }
                                                        Text {
                                                            visible: !!shelfCard.modelData.verified
                                                            text: "✓"
                                                            color: "#38bdf8"
                                                            font.pixelSize: 12
                                                            font.weight: Font.Bold
                                                        }
                                                    }
                                                    Text {
                                                        text: shelfCard.modelData.subtitle
                                                        color: FinnTheme.mute
                                                        font.family: FinnTheme.fontFamily
                                                        font.pixelSize: 11
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                    Text {
                                                        text: (shelfCard.modelData.rating > 0 ? "★ " + Number(shelfCard.modelData.rating).toFixed(1) + " · " : "") +
                                                              (shelfCard.modelData.sizeLabel || shelfCard.modelData.developer)
                                                        color: FinnTheme.dim
                                                        font.family: FinnTheme.fontFamily
                                                        font.pixelSize: 10
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }

                                                FinnButton {
                                                    Layout.preferredWidth: 62
                                                    text: shelfCard.modelData.installed ? "Open" : "Get"
                                                    variant: shelfCard.modelData.installed ? "secondary" : "primary"
                                                    onClicked: {
                                                        if (shelfCard.modelData.installed)
                                                            StoreCtrl.launchApp(shelfCard.modelData.appId)
                                                        else
                                                            StoreCtrl.installApp(shelfCard.modelData.appId)
                                                    }
                                                }
                                            }

                                            MouseArea {
                                                id: shelfHover
                                                anchors.fill: parent
                                                z: -1
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: StoreCtrl.openAppDetail(shelfCard.modelData.appId)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── ECOSYSTEM / SEARCH / UPDATES GRID VIEW ───────────────
                    GridLayout {
                        Layout.fillWidth: true
                        visible: StoreCtrl.activeTab !== "discover" || StoreCtrl.searchQuery.trim().length > 0
                        columns: win.width >= 1060 ? 3 : 2
                        columnSpacing: 16
                        rowSpacing: 14

                        Repeater {
                            model: StoreCtrl
                            delegate: Rectangle {
                                id: appCard
                                Layout.fillWidth: true
                                implicitHeight: 112
                                radius: 14
                                clip: true
                                color: cardArea.containsMouse ? FinnTheme.surfaceHover : FinnTheme.tile
                                border.width: 1
                                border.color: FinnTheme.hair

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 10

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Rectangle {
                                            width: 46; height: 46; radius: 12
                                            Layout.preferredWidth: 46
                                            Layout.preferredHeight: 46
                                            color: FinnTheme.surfaceInput
                                            FinnIcon {
                                                anchors.centerIn: parent
                                                name: model.iconName
                                                size: 26
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 2

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 4
                                                Text {
                                                    text: model.name
                                                    color: FinnTheme.ink
                                                    font.family: FinnTheme.fontFamily
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                }
                                                Text {
                                                    visible: model.verified
                                                    text: "✓"
                                                    color: "#38bdf8"
                                                    font.pixelSize: 12
                                                    font.weight: Font.Bold
                                                }
                                            }

                                            Text {
                                                text: model.subtitle
                                                color: FinnTheme.mute
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: model.ecosystemLabel + (model.developer.length > 0 ? " · " + model.developer : "")
                                                color: FinnTheme.dim
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        FinnButton {
                                            visible: model.taskProgress < 0
                                            Layout.preferredWidth: 62
                                            text: model.updateAvailable ? "Update" : (model.installed ? "Open" : "Get")
                                            variant: model.installed && !model.updateAvailable ? "secondary" : "primary"
                                            onClicked: {
                                                if (model.installed && !model.updateAvailable)
                                                    StoreCtrl.launchApp(model.appId)
                                                else
                                                    StoreCtrl.installApp(model.appId)
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: model.taskProgress >= 0
                                        spacing: 8
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 6
                                            radius: 3
                                            color: FinnTheme.surfaceInput
                                            Rectangle {
                                                width: parent.width * Math.max(0.08, Math.min(1.0, model.taskProgress))
                                                height: parent.height
                                                radius: 3
                                                color: FinnTheme.accent
                                            }
                                        }
                                        Text {
                                            text: model.taskState
                                            color: FinnTheme.dim
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 10
                                        }
                                    }
                                }

                                MouseArea {
                                    id: cardArea
                                    anchors.fill: parent
                                    z: -1
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: StoreCtrl.openAppDetail(model.appId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Rich App Detail Modal Sheet ──────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: StoreCtrl.detailOpen
        color: Qt.rgba(0, 0, 0, 0.56)
        z: 50

        MouseArea {
            anchors.fill: parent
            onClicked: StoreCtrl.closeAppDetail()
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 680)
            height: Math.min(parent.height - 80, 560)
            radius: 18
            color: FinnTheme.tile
            border.width: 1
            border.color: FinnTheme.hair

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    Rectangle {
                        width: 72; height: 72; radius: 16
                        color: FinnTheme.surfaceInput
                        FinnIcon {
                            anchors.centerIn: parent
                            name: StoreCtrl.selectedApp.iconName || "app-grid"
                            size: 44
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        RowLayout {
                            spacing: 8
                            Text {
                                text: StoreCtrl.selectedApp.name || ""
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 22
                                font.weight: Font.Bold
                            }
                            Rectangle {
                                visible: !!StoreCtrl.selectedApp.verified
                                radius: 6
                                color: Qt.rgba(0.22, 0.74, 0.97, 0.16)
                                implicitWidth: verTxt.implicitWidth + 12
                                implicitHeight: 20
                                Text {
                                    id: verTxt
                                    anchors.centerIn: parent
                                    text: "✓ " + (StoreCtrl.selectedApp.verifiedSource || "Verified")
                                    color: "#38bdf8"
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }
                        }

                        Text {
                            text: (StoreCtrl.selectedApp.developer || "") + " · " + (StoreCtrl.selectedApp.ecosystemLabel || "")
                            color: FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                        }

                        RowLayout {
                            spacing: 10
                            FinnButton {
                                text: StoreCtrl.selectedApp.installed ? "Open" : "Install"
                                variant: "primary"
                                onClicked: {
                                    if (StoreCtrl.selectedApp.installed)
                                        StoreCtrl.launchApp(StoreCtrl.selectedApp.appId)
                                    else
                                        StoreCtrl.installApp(StoreCtrl.selectedApp.appId)
                                }
                            }
                            FinnButton {
                                visible: !!StoreCtrl.selectedApp.installed
                                text: "Uninstall"
                                variant: "secondary"
                                onClicked: StoreCtrl.uninstallApp(StoreCtrl.selectedApp.appId)
                            }
                        }
                    }

                    FinnButton {
                        text: "Close"
                        variant: "ghost"
                        Layout.alignment: Qt.AlignTop
                        onClicked: StoreCtrl.closeAppDetail()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 56
                    radius: 12
                    color: FinnTheme.surfaceInput

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        Repeater {
                            model: [
                                { k: "RATING",   v: (StoreCtrl.selectedApp.rating > 0) ? ("★ " + Number(StoreCtrl.selectedApp.rating).toFixed(1)) : "—" },
                                { k: "CATEGORY", v: StoreCtrl.selectedApp.category || "App" },
                                { k: "SIZE",     v: StoreCtrl.selectedApp.sizeLabel || "—" },
                                { k: "VERSION",  v: StoreCtrl.selectedApp.version || "Latest" }
                            ]
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: modelData.k
                                    color: FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: modelData.v
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }
                }

                Text {
                    text: StoreCtrl.selectedApp.description || StoreCtrl.selectedApp.subtitle || ""
                    color: FinnTheme.dim
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: (StoreCtrl.selectedApp.permissions || []).length > 0

                    Text {
                        text: "PERMISSIONS & RUNTIME"
                        color: FinnTheme.mute
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: StoreCtrl.selectedApp.permissions || []
                            delegate: Rectangle {
                                required property string modelData
                                radius: 8
                                color: FinnTheme.surfaceInput
                                border.width: 1
                                border.color: FinnTheme.hair
                                implicitWidth: permTxt.implicitWidth + 14
                                implicitHeight: 24
                                Text {
                                    id: permTxt
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // ── Local Package Sideload Sheet (--install <path>) ──────────────────────
    Rectangle {
        anchors.fill: parent
        visible: StoreCtrl.localPackageOpen
        color: Qt.rgba(0, 0, 0, 0.58)
        z: 60

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 520)
            implicitHeight: pkgCol.implicitHeight + 48
            radius: 18
            color: FinnTheme.tile
            border.width: 1
            border.color: FinnTheme.hair

            ColumnLayout {
                id: pkgCol
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Text {
                    text: "Install Package"
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }

                Text {
                    text: StoreCtrl.localPackage.appName || StoreCtrl.localPackage.filename || ""
                    color: FinnTheme.accent
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Text {
                    text: (StoreCtrl.localPackage.formatLabel || "") + " · " + (StoreCtrl.localPackage.ecosystemLabel || "")
                    color: FinnTheme.dim
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                }

                Text {
                    text: StoreCtrl.localPackage.valid
                        ? (StoreCtrl.localPackage.summary + "\n" + StoreCtrl.localPackage.runtimeNote)
                        : (StoreCtrl.localPackage.error || "Unsupported package")
                    color: StoreCtrl.localPackage.valid ? FinnTheme.dim : FinnTheme.danger
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FinnButton {
                        text: "Cancel"
                        variant: "secondary"
                        onClicked: StoreCtrl.closeLocalPackage()
                    }
                    FinnButton {
                        text: "Install"
                        variant: "primary"
                        enabled: !!StoreCtrl.localPackage.valid && !!StoreCtrl.localPackage.runtimeReady
                        onClicked: StoreCtrl.confirmInstallLocalPackage()
                    }
                }
            }
        }
    }

    FinnToast { id: toast }
}
