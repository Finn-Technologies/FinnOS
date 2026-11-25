// Finn Store — Settings-consistent Native Qt 6 / FinnUI Software Center.
// Uses FinnApplicationWindow directly for 100% consistent sidebar (250px, flat right
// edge, 26px circular badge Hugeicons, 14px Regular labels), bare full-color app
// icons with zero background boxes, and clean hairline-separated shelves.

import QtQuick
import QtQuick.Layouts
import FinnUI 1.0

FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1060
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 680
    appTitle: "Finn Store"
    pageTitle: StoreCtrl.searchQuery.trim().length > 0
        ? ("Results for \"" + StoreCtrl.searchQuery.trim() + "\"")
        : tabTitle(StoreCtrl.activeTab)
    activeNavId: StoreCtrl.activeTab
    showSidebarSearch: true
    mobileDetailOpen: true

    readonly property var primaryHero: StoreCtrl.heroItems.length > 0 ? StoreCtrl.heroItems[0] : null

    // Exact Settings sidebar badge style (26px circular colored badge + white Hugeicons + 14px Regular)
    navItems: [
        { id: "discover", label: "Discover",      icon: "compass",    badgeColor: "#007BFF", section: "browse" },
        { id: "native",   label: "Native FinnOS", icon: "grid",       badgeColor: "#6366F1", section: "ecosystems" },
        { id: "flatpak",  label: "Linux",         icon: "package",    badgeColor: "#0EA5E9", section: "ecosystems" },
        { id: "android",  label: "Android",       icon: "smartphone", badgeColor: "#10B981", section: "ecosystems" },
        { id: "windows",  label: "Windows",       icon: "window",     badgeColor: "#2563EB", section: "ecosystems" },
        { id: "updates",  label: "Updates",       icon: "download",   badgeColor: "#F59E0B", section: "manage" }
    ]

    function tabTitle(tabId) {
        for (var i = 0; i < navItems.length; ++i) {
            if (navItems[i].id === tabId) return navItems[i].label
        }
        return "Discover"
    }

    onNavSelected: (id) => {
        StoreCtrl.activeTab = id
        StoreCtrl.searchQuery = ""
    }
    onSidebarSearchTextChanged: StoreCtrl.searchQuery = sidebarSearchText

    Connections {
        target: StoreCtrl
        function onToastRequested(message, severity) {
            win.showToast(message, severity || "info")
        }
    }

    headerActions: [
        FinnButton {
            text: "Install File"
            iconName: "plus"
            variant: "secondary"
            implicitHeight: 28
            radius: 6
            onClicked: StoreCtrl.browseLocalPackage()
        },
        FinnButton {
            visible: StoreCtrl.activeTab === "updates"
            text: "Check Updates"
            variant: "secondary"
            implicitHeight: 28
            radius: 6
            onClicked: StoreCtrl.checkForUpdates()
        },
        FinnButton {
            visible: StoreCtrl.activeTab === "updates" && StoreCtrl.updatesCount > 0
            text: "Update All"
            variant: "primary"
            implicitHeight: 28
            radius: 6
            onClicked: StoreCtrl.updateAll()
        },
        FinnButton {
            visible: StoreCtrl.searchQuery.length >= 2 && StoreCtrl.activeTab !== "native"
            text: StoreCtrl.busy ? "Searching..." : "Search Online"
            variant: "primary"
            implicitHeight: 28
            radius: 6
            enabled: !StoreCtrl.busy
            onClicked: StoreCtrl.searchOnline(StoreCtrl.searchQuery)
        }
    ]

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: 18

            // ── DISCOVER VIEW ────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 16
                visible: StoreCtrl.activeTab === "discover" && StoreCtrl.searchQuery.trim().length === 0

                // 1. Editorial Hero Banner with actual bare FinnOS app icons
                Rectangle {
                    id: heroBanner
                    Layout.fillWidth: true
                    Layout.preferredHeight: 204
                    radius: FinnTheme.radiusSm
                    clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0;  color: "#0066FF" }
                        GradientStop { position: 0.55; color: "#2563EB" }
                        GradientStop { position: 1.0;  color: "#4F46E5" }
                    }

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 22
                        width: parent.width * 0.46
                        spacing: 5

                        Text {
                            text: "Featured"
                            color: "#CCFFFFFF"
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Normal
                        }

                        Text {
                            text: "FinnOS Essentials"
                            color: "#FFFFFF"
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 22
                            font.weight: Font.Medium
                        }

                        Text {
                            text: "Native Wayland apps built for everyday work on FinnOS."
                            color: "#E0FFFFFF"
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Item { Layout.fillHeight: true }

                        Rectangle {
                            implicitWidth: heroBtnTxt.implicitWidth + 22
                            implicitHeight: 28
                            radius: 6
                            color: heroBtnMa.containsMouse ? "#45FFFFFF" : "#2AFFFFFF"

                            Text {
                                id: heroBtnTxt
                                anchors.centerIn: parent
                                text: "View All"
                                color: "#FFFFFF"
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                id: heroBtnMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.activeTab = "native"
                            }
                        }
                    }

                    // Actual FinnOS app icons fanned cleanly (zero wrapper boxes)
                    Item {
                        anchors.right: parent.right
                        anchors.rightMargin: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 284
                        height: 156

                        Repeater {
                            model: [
                                { icon: "files",    x: 4,   y: 58, rot: -14 },
                                { icon: "notes",    x: 58,  y: 34, rot: -7 },
                                { icon: "terminal", x: 112, y: 18, rot: 0 },
                                { icon: "viewer",   x: 166, y: 16, rot: 7 },
                                { icon: "flourish", x: 214, y: 30, rot: 14 }
                            ]
                            delegate: FinnIcon {
                                required property var modelData
                                x: modelData.x
                                y: modelData.y
                                size: 64
                                name: modelData.icon
                                monochrome: false
                                rotation: modelData.rot
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (win.primaryHero) StoreCtrl.openAppDetail(win.primaryHero.appId)
                            else StoreCtrl.activeTab = "native"
                        }
                    }
                }

                // 2. Two-Column Ecosystem Cards with actual bare app icons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: [
                            {
                                eyebrow: "Cross-OS",
                                title: "Linux, Android & Windows",
                                body: "Install Flathub, Aurora Play, and winget packages side by side.",
                                icons: ["linux-app", "android", "windows"],
                                targetTab: "flatpak"
                            },
                            {
                                eyebrow: "First-Party",
                                title: "Built for FinnOS",
                                body: "Files, Terminal, Notes, Viewer, Monitor, and Calculator.",
                                icons: ["files", "notes", "monitor"],
                                targetTab: "native"
                            }
                        ]
                        delegate: FinnSquircle {
                            id: storyCard
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            radius: FinnTheme.radiusSm
                            color: storyMa.containsMouse
                                ? (FinnTheme.dark ? "#1C2230" : "#F5F6F8")
                                : (FinnTheme.dark ? FinnTheme.tile : "#FFFFFF")
                            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                            borderWidth: 0.5
                            specularRim: false

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 3

                                    Text {
                                        text: storyCard.modelData.eyebrow
                                        color: FinnTheme.mute
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.Normal
                                    }

                                    Text {
                                        text: storyCard.modelData.title
                                        color: FinnTheme.ink
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 15
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: storyCard.modelData.body
                                        color: FinnTheme.dim
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    Item { Layout.fillHeight: true }
                                }

                                // Bare app icon cluster (no background box)
                                Item {
                                    Layout.preferredWidth: 102
                                    Layout.preferredHeight: 54

                                    FinnIcon {
                                        x: 0; y: 10; size: 38; rotation: -10
                                        name: storyCard.modelData.icons[0]
                                        monochrome: false
                                    }
                                    FinnIcon {
                                        x: 30; y: 2; size: 44
                                        name: storyCard.modelData.icons[1]
                                        monochrome: false
                                    }
                                    FinnIcon {
                                        x: 64; y: 10; size: 38; rotation: 10
                                        name: storyCard.modelData.icons[2]
                                        monochrome: false
                                    }
                                }
                            }

                            MouseArea {
                                id: storyMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.activeTab = storyCard.modelData.targetTab
                            }
                        }
                    }
                }

                // 3. Borderless App Shelves & Ranked Charts (bare app icons, zero backgrounds)
                Repeater {
                    model: StoreCtrl.discoverShelves
                    delegate: ColumnLayout {
                        id: shelf
                        required property var modelData
                        required property int index
                        readonly property bool rankedStyle: index >= 2
                        Layout.fillWidth: true
                        spacing: 8

                        FinnDivider { Layout.fillWidth: true }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: shelf.modelData.title
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 16
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "See All"
                                color: FinnTheme.accent
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: StoreCtrl.activeTab = shelf.modelData.tabId
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 22
                            rowSpacing: 0

                            Repeater {
                                model: shelf.modelData.items
                                delegate: Item {
                                    id: shelfRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: 66

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.topMargin: 8
                                        anchors.bottomMargin: 8
                                        spacing: 12

                                        // Bare actual app icon — zero background box or border
                                        FinnIcon {
                                            Layout.preferredWidth: 46
                                            Layout.preferredHeight: 46
                                            size: 46
                                            name: shelfRow.modelData.iconName
                                            fallbackName: shelfRow.modelData.ecosystemId === "flatpak" ? "linux-app"
                                                : (shelfRow.modelData.ecosystemId === "android" ? "android"
                                                : (shelfRow.modelData.ecosystemId === "windows" ? "windows" : "files"))
                                            monochrome: false
                                        }

                                        Text {
                                            visible: shelf.rankedStyle
                                            text: String(shelfRow.index + 1)
                                            color: FinnTheme.ink
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                            Layout.preferredWidth: 16
                                            horizontalAlignment: Text.AlignHCenter
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 2

                                            Text {
                                                text: shelfRow.modelData.name
                                                color: FinnTheme.ink
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 14
                                                font.weight: Font.Normal
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: shelfRow.modelData.subtitle
                                                color: FinnTheme.mute
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        Rectangle {
                                            Layout.preferredWidth: 58
                                            Layout.preferredHeight: 26
                                            radius: 6
                                            color: getBtnMa.containsMouse
                                                ? (FinnTheme.dark ? "#283954" : "#DCEBFF")
                                                : (FinnTheme.dark ? "#1E2B40" : "#EEF5FF")

                                            Text {
                                                anchors.centerIn: parent
                                                text: shelfRow.modelData.installed ? "Open" : "Get"
                                                color: FinnTheme.accent
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                            }

                                            MouseArea {
                                                id: getBtnMa
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (shelfRow.modelData.installed)
                                                        StoreCtrl.launchApp(shelfRow.modelData.appId)
                                                    else
                                                        StoreCtrl.installApp(shelfRow.modelData.appId)
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 58
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 0.5
                                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        z: -1
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: StoreCtrl.openAppDetail(shelfRow.modelData.appId)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── FLATHUB CATEGORY / COLLECTION FILTER BAR (Linux tab) ─────────
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                visible: StoreCtrl.activeTab === "flatpak" && StoreCtrl.searchQuery.trim().length === 0
                contentWidth: fhCats.implicitWidth
                clip: true

                Row {
                    id: fhCats
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Repeater {
                        model: [
                            { id: "popular",          label: "Popular" },
                            { id: "trending",         label: "Trending" },
                            { id: "recently-updated", label: "Updated" },
                            { id: "Development",      label: "Developer" },
                            { id: "Graphics",         label: "Graphics" },
                            { id: "Game",             label: "Games" },
                            { id: "Network",          label: "Internet" },
                            { id: "AudioVideo",       label: "Media" },
                            { id: "Office",           label: "Office" },
                            { id: "Utility",          label: "Utilities" }
                        ]
                        delegate: FinnSquircle {
                            required property var modelData
                            readonly property bool active: StoreCtrl.flathubCollection === modelData.id
                            width: catTxt.implicitWidth + 18
                            height: 26
                            radius: 6
                            color: active
                                ? FinnTheme.accent
                                : (catMa.containsMouse ? FinnTheme.surfaceHover : (FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"))
                            borderColor: active ? "transparent" : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000")
                            borderWidth: active ? 0 : 0.5
                            specularRim: false

                            Text {
                                id: catTxt
                                anchors.centerIn: parent
                                text: modelData.label
                                color: active ? "#FFFFFF" : FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: active ? Font.Medium : Font.Normal
                            }

                            MouseArea {
                                id: catMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.flathubCollection = modelData.id
                            }
                        }
                    }
                }
            }

            // ── ANDROID CATEGORY FILTER BAR ──────────────────────────────────
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                visible: StoreCtrl.activeTab === "android" && StoreCtrl.searchQuery.trim().length === 0
                contentWidth: andCats.implicitWidth
                clip: true

                Row {
                    id: andCats
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Repeater {
                        model: [
                            { id: "popular",   label: "Popular" },
                            { id: "internet",  label: "Internet" },
                            { id: "messaging", label: "Messaging" },
                            { id: "media",     label: "Media" },
                            { id: "games",     label: "Games" },
                            { id: "maps",      label: "Maps" },
                            { id: "office",    label: "Office" },
                            { id: "utilities", label: "Utilities" }
                        ]
                        delegate: FinnSquircle {
                            required property var modelData
                            readonly property bool active: StoreCtrl.androidCollection === modelData.id
                            width: andCatTxt.implicitWidth + 18
                            height: 26
                            radius: 6
                            color: active
                                ? FinnTheme.accent
                                : (andCatMa.containsMouse ? FinnTheme.surfaceHover : (FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"))
                            borderColor: active ? "transparent" : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000")
                            borderWidth: active ? 0 : 0.5
                            specularRim: false

                            Text {
                                id: andCatTxt
                                anchors.centerIn: parent
                                text: modelData.label
                                color: active ? "#FFFFFF" : FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: active ? Font.Medium : Font.Normal
                            }

                            MouseArea {
                                id: andCatMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.androidCollection = modelData.id
                            }
                        }
                    }
                }
            }

            // ── WINDOWS CATEGORY FILTER BAR ──────────────────────────────────
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                visible: StoreCtrl.activeTab === "windows" && StoreCtrl.searchQuery.trim().length === 0
                contentWidth: winCats.implicitWidth
                clip: true

                Row {
                    id: winCats
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Repeater {
                        model: [
                            { id: "popular",   label: "Popular" },
                            { id: "developer", label: "Developer" },
                            { id: "browsers",  label: "Browsers" },
                            { id: "media",     label: "Media" },
                            { id: "graphics",  label: "Graphics" },
                            { id: "gaming",    label: "Gaming" },
                            { id: "utilities", label: "Utilities" },
                            { id: "security",  label: "Security" }
                        ]
                        delegate: FinnSquircle {
                            required property var modelData
                            readonly property bool active: StoreCtrl.wingetCollection === modelData.id
                            width: winCatTxt.implicitWidth + 18
                            height: 26
                            radius: 6
                            color: active
                                ? FinnTheme.accent
                                : (winCatMa.containsMouse ? FinnTheme.surfaceHover : (FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"))
                            borderColor: active ? "transparent" : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000")
                            borderWidth: active ? 0 : 0.5
                            specularRim: false

                            Text {
                                id: winCatTxt
                                anchors.centerIn: parent
                                text: modelData.label
                                color: active ? "#FFFFFF" : FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: active ? Font.Medium : Font.Normal
                            }

                            MouseArea {
                                id: winCatMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.wingetCollection = modelData.id
                            }
                        }
                    }
                }
            }

            // ── ECOSYSTEM / SEARCH / UPDATES LIST VIEW ───────────────────────
            GridLayout {
                Layout.fillWidth: true
                visible: StoreCtrl.activeTab !== "discover" || StoreCtrl.searchQuery.trim().length > 0
                columns: 2
                columnSpacing: 22
                rowSpacing: 0

                Repeater {
                    model: StoreCtrl
                    delegate: Item {
                        id: appRow
                        Layout.fillWidth: true
                        implicitHeight: model.taskProgress >= 0 ? 80 : 66

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                // Bare actual app icon — zero background box or border
                                FinnIcon {
                                    Layout.preferredWidth: 46
                                    Layout.preferredHeight: 46
                                    size: 46
                                    name: model.iconName
                                    fallbackName: model.ecosystemId === "flatpak" ? "linux-app"
                                        : (model.ecosystemId === "android" ? "android"
                                        : (model.ecosystemId === "windows" ? "windows" : "files"))
                                    monochrome: false
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Text {
                                            text: model.name
                                            color: FinnTheme.ink
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 14
                                            font.weight: Font.Normal
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                        }

                                        // Ecosystem tag shown during search (FinnOS / Linux / Windows / Android)
                                        Rectangle {
                                            visible: StoreCtrl.searchQuery.trim().length > 0
                                            readonly property color tagAccent: model.ecosystemId === "native" ? "#007BFF"
                                                : (model.ecosystemId === "flatpak" ? "#0EA5E9"
                                                : (model.ecosystemId === "android" ? "#10B981" : "#6366F1"))
                                            radius: 4
                                            color: FinnTheme.withAlpha(tagAccent, FinnTheme.dark ? 0.22 : 0.12)
                                            implicitWidth: ecoTagTxt.implicitWidth + 10
                                            implicitHeight: 18

                                            Text {
                                                id: ecoTagTxt
                                                anchors.centerIn: parent
                                                text: model.ecosystemId === "native" ? "FinnOS"
                                                    : (model.ecosystemId === "flatpak" ? "Linux"
                                                    : (model.ecosystemId === "android" ? "Android" : "Windows"))
                                                color: parent.tagAccent
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 10
                                                font.weight: Font.Medium
                                            }
                                        }
                                    }

                                    Text {
                                        text: model.subtitle
                                        color: FinnTheme.mute
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                Rectangle {
                                    visible: model.taskProgress < 0
                                    Layout.preferredWidth: 62
                                    Layout.preferredHeight: 26
                                    radius: 6
                                    color: actMa.containsMouse
                                        ? (FinnTheme.dark ? "#283954" : "#DCEBFF")
                                        : (FinnTheme.dark ? "#1E2B40" : "#EEF5FF")

                                    Text {
                                        anchors.centerIn: parent
                                        text: model.updateAvailable ? "Update" : (model.installed ? "Open" : "Get")
                                        color: FinnTheme.accent
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                    }

                                    MouseArea {
                                        id: actMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (model.installed && !model.updateAvailable)
                                                StoreCtrl.launchApp(model.appId)
                                            else
                                                StoreCtrl.installApp(model.appId)
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: model.taskProgress >= 0
                                spacing: 8
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 4
                                    radius: 2
                                    color: FinnTheme.surfaceInput
                                    Rectangle {
                                        width: parent.width * Math.max(0.08, Math.min(1.0, model.taskProgress))
                                        height: parent.height
                                        radius: 2
                                        color: FinnTheme.accent
                                    }
                                }
                                Text {
                                    text: model.taskState
                                    color: FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: 58
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 0.5
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: StoreCtrl.openAppDetail(model.appId)
                        }
                    }
                }
            }

            // ── LOAD MORE FLATHUB FOOTER (browse entire Flathub library) ─────
            RowLayout {
                Layout.fillWidth: true
                visible: StoreCtrl.activeTab === "flatpak" && StoreCtrl.searchQuery.trim().length === 0 && StoreCtrl.flathubTotalHits > 0
                spacing: 12

                Text {
                    text: "Showing " + StoreCtrl.totalCount + " of " + StoreCtrl.flathubTotalHits + " Flathub apps"
                    color: FinnTheme.mute
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                FinnButton {
                    visible: StoreCtrl.flathubHasMore
                    text: StoreCtrl.busy ? "Loading..." : "Load More"
                    variant: "secondary"
                    implicitHeight: 28
                    radius: 6
                    enabled: !StoreCtrl.busy
                    onClicked: StoreCtrl.loadMoreFlathub()
                }
            }
        }
    }

    // ── App Detail Modal Sheet ───────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: StoreCtrl.detailOpen
        color: Qt.rgba(0, 0, 0, 0.42)
        z: 50

        MouseArea {
            anchors.fill: parent
            onClicked: StoreCtrl.closeAppDetail()
        }

        FinnSquircle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 64, 580)
            height: Math.min(parent.height - 64, 460)
            radius: FinnTheme.radiusLg
            color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
            borderWidth: 0.5
            specularRim: false

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    FinnIcon {
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 60
                        size: 60
                        name: StoreCtrl.selectedApp.iconName || "files"
                        monochrome: false
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            text: StoreCtrl.selectedApp.name || ""
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.Medium
                        }

                        Text {
                            text: (StoreCtrl.selectedApp.developer || "") + " · " + (StoreCtrl.selectedApp.ecosystemLabel || "")
                            color: FinnTheme.mute
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                        }

                        RowLayout {
                            spacing: 8
                            Layout.topMargin: 4
                            FinnButton {
                                text: StoreCtrl.selectedApp.installed ? "Open" : "Get"
                                variant: "primary"
                                implicitHeight: 28
                                radius: 6
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
                                implicitHeight: 28
                                radius: 6
                                onClicked: StoreCtrl.uninstallApp(StoreCtrl.selectedApp.appId)
                            }
                        }
                    }

                    FinnIconButton {
                        iconName: "x"
                        buttonSize: 28
                        iconSize: 14
                        radius: 6
                        Layout.alignment: Qt.AlignTop
                        onClicked: StoreCtrl.closeAppDetail()
                    }
                }

                FinnDivider { Layout.fillWidth: true }

                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [
                            { k: "Rating",   v: (StoreCtrl.selectedApp.rating > 0) ? Number(StoreCtrl.selectedApp.rating).toFixed(1) : "—" },
                            { k: "Category", v: StoreCtrl.selectedApp.category || "App" },
                            { k: "Size",     v: StoreCtrl.selectedApp.sizeLabel || "—" },
                            { k: "Version",  v: StoreCtrl.selectedApp.version || "Latest" }
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.k
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: modelData.v
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }

                FinnDivider { Layout.fillWidth: true }

                Text {
                    text: StoreCtrl.selectedApp.description || StoreCtrl.selectedApp.subtitle || ""
                    color: FinnTheme.dim
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 13
                    lineHeight: 1.4
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // ── Local Package Sideload Sheet (--install <path>) ──────────────────────
    Rectangle {
        anchors.fill: parent
        visible: StoreCtrl.localPackageOpen
        color: Qt.rgba(0, 0, 0, 0.42)
        z: 60

        FinnSquircle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 64, 480)
            implicitHeight: pkgCol.implicitHeight + 40
            radius: FinnTheme.radiusLg
            color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
            borderWidth: 0.5
            specularRim: false

            ColumnLayout {
                id: pkgCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "Install Package"
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                Text {
                    text: StoreCtrl.localPackage.appName || StoreCtrl.localPackage.filename || ""
                    color: FinnTheme.accent
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 14
                }

                Text {
                    text: (StoreCtrl.localPackage.formatLabel || "") + " · " + (StoreCtrl.localPackage.ecosystemLabel || "")
                    color: FinnTheme.mute
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
                        implicitHeight: 28
                        radius: 6
                        onClicked: StoreCtrl.closeLocalPackage()
                    }
                    FinnButton {
                        text: "Install"
                        variant: "primary"
                        implicitHeight: 28
                        radius: 6
                        enabled: !!StoreCtrl.localPackage.valid && !!StoreCtrl.localPackage.runtimeReady
                        onClicked: StoreCtrl.confirmInstallLocalPackage()
                    }
                }
            }
        }
    }
}
