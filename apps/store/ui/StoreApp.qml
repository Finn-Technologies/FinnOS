// Finn Store — Native Qt 6 / FinnUI Software Center.
// Restrained, editorial native macOS/FinnOS aesthetic: borderless hairline-separated
// shelves and grids, accurate cross-OS tags, and a full-space animated App Listing view.

import QtQuick
import QtQuick.Layouts
import FinnUI 1.0

FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1080
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 700
    appTitle: "Finn Store"
    pageTitle: StoreCtrl.detailOpen
        ? ""
        : (StoreCtrl.searchQuery.trim().length > 0
            ? ("Results for \"" + StoreCtrl.searchQuery.trim() + "\"")
            : tabTitle(StoreCtrl.activeTab))
    activeNavId: StoreCtrl.activeTab
    showSidebarSearch: true
    mobileDetailOpen: true

    navItems: [
        { id: "discover", label: "Discover",      icon: "compass",    badgeColor: "#007BFF", section: "browse" },
        { id: "native",   label: "Native FinnOS", icon: "grid",       badgeColor: "#2563EB", section: "ecosystems" },
        { id: "flatpak",  label: "Linux",         icon: "package",    badgeColor: "#0284C7", section: "ecosystems" },
        { id: "android",  label: "Android",       icon: "smartphone", badgeColor: "#16A34A", section: "ecosystems" },
        { id: "windows",  label: "Windows",       icon: "window",     badgeColor: "#4F46E5", section: "ecosystems" },
        { id: "updates",  label: "Updates",       icon: "download",   badgeColor: "#D97706", section: "manage" }
    ]

    function tabTitle(tabId) {
        for (var i = 0; i < navItems.length; ++i) {
            if (navItems[i].id === tabId) return navItems[i].label
        }
        return "Discover"
    }

    function ecoTagLabel(ecoId) {
        if (ecoId === "native") return "FinnOS"
        if (ecoId === "flatpak") return "Linux"
        if (ecoId === "android") return "Android"
        if (ecoId === "windows") return "Windows"
        if (ecoId === "macos") return "macOS"
        if (ecoId === "ohos") return "OpenHarmony"
        return ""
    }

    function ecoTagColor(ecoId) {
        if (ecoId === "native") return "#007BFF"
        if (ecoId === "flatpak") return "#0284C7"
        if (ecoId === "android") return "#16A34A"
        if (ecoId === "windows") return "#4F46E5"
        if (ecoId === "macos") return "#7C3AED"
        if (ecoId === "ohos") return "#D97706"
        return "#64748B"
    }

    function ecoFallbackIcon(ecoId) {
        if (ecoId === "flatpak") return "linux-app"
        if (ecoId === "android") return "android"
        if (ecoId === "windows") return "windows"
        return "files"
    }

    onNavSelected: (id) => {
        if (StoreCtrl.detailOpen) StoreCtrl.closeAppDetail()
        StoreCtrl.activeTab = id
        StoreCtrl.searchQuery = ""
    }
    onSidebarSearchTextChanged: {
        if (StoreCtrl.detailOpen && sidebarSearchText.trim().length > 0) {
            StoreCtrl.closeAppDetail()
        }
        StoreCtrl.searchQuery = sidebarSearchText
    }

    Connections {
        target: StoreCtrl
        function onToastRequested(message, severity) {
            win.showToast(message, severity || "info")
        }
    }

    headerActions: [
        FinnButton {
            visible: !StoreCtrl.detailOpen && StoreCtrl.activeTab === "updates"
            text: "Check Updates"
            variant: "secondary"
            implicitHeight: 28
            radius: 6
            onClicked: StoreCtrl.checkForUpdates()
        },
        FinnButton {
            visible: !StoreCtrl.detailOpen && StoreCtrl.activeTab === "updates" && StoreCtrl.updatesCount > 0
            text: "Update All"
            variant: "primary"
            implicitHeight: 28
            radius: 6
            onClicked: StoreCtrl.updateAll()
        },
        FinnButton {
            visible: !StoreCtrl.detailOpen && StoreCtrl.searchQuery.length >= 2 && StoreCtrl.activeTab !== "native"
            text: StoreCtrl.busy ? "Searching..." : "Search Online"
            variant: "primary"
            implicitHeight: 28
            radius: 6
            enabled: !StoreCtrl.busy
            onClicked: StoreCtrl.searchOnline(StoreCtrl.searchQuery)
        }
    ]

    Item {
        id: storeRoot
        anchors.fill: parent
        clip: true

        Keys.onEscapePressed: {
            if (StoreCtrl.localPackageOpen) StoreCtrl.closeLocalPackage()
            else if (StoreCtrl.detailOpen) StoreCtrl.closeAppDetail()
        }

        // ── BROWSE VIEW ─────────────────────────────────────────────────────
        Flickable {
            id: scroller
            anchors.fill: parent
            contentWidth: width
            contentHeight: contentCol.implicitHeight + 28
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            enabled: !StoreCtrl.detailOpen
            opacity: StoreCtrl.detailOpen ? 0.0 : 1.0
            scale: StoreCtrl.detailOpen ? 0.985 : 1.0
            transformOrigin: Item.Center

            Behavior on opacity {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
            Behavior on scale {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            Column {
                id: contentCol
                width: parent.width
                spacing: 20

                // ── DISCOVER VIEW ───────────────────────────────────────────
                Column {
                    width: parent.width
                    spacing: 22
                    visible: StoreCtrl.activeTab === "discover" && StoreCtrl.searchQuery.trim().length === 0

                    // Quiet, restrained editorial showcase
                    FinnSquircle {
                        id: heroBanner
                        width: parent.width
                        height: 164
                        radius: FinnTheme.radiusLg
                        color: FinnTheme.dark ? "#131822" : "#F5F7FA"
                        borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#10000000"
                        borderWidth: 0.5
                        specularRim: false

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 24
                            anchors.rightMargin: 24
                            spacing: 20

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 6

                                Text {
                                    text: "FinnOS Essentials"
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 21
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: "Native Wayland apps alongside Linux, Android, and Windows software."
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    lineHeight: 1.35
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: 340
                                }
                            }

                            Item {
                                Layout.preferredWidth: 268
                                Layout.preferredHeight: 124
                                Layout.alignment: Qt.AlignVCenter
                                visible: heroBanner.width >= 620

                                Repeater {
                                    model: [
                                        { icon: "files",    x: 4,   y: 38, rot: -10 },
                                        { icon: "notes",    x: 56,  y: 22, rot: -5 },
                                        { icon: "terminal", x: 108, y: 12, rot: 0 },
                                        { icon: "viewer",   x: 160, y: 18, rot: 5 },
                                        { icon: "flourish", x: 208, y: 30, rot: 10 }
                                    ]
                                    delegate: FinnIcon {
                                        required property var modelData
                                        x: modelData.x
                                        y: modelData.y
                                        size: 58
                                        name: modelData.icon
                                        monochrome: false
                                        rotation: modelData.rot
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: StoreCtrl.activeTab = "native"
                        }
                    }

                    // Clean borderless hairline-separated shelves
                    Repeater {
                        model: StoreCtrl.discoverShelves
                        delegate: Column {
                            id: shelf
                            required property var modelData
                            required property int index
                            width: contentCol.width
                            spacing: 8

                            FinnDivider {
                                visible: shelf.index > 0
                                width: parent.width
                            }

                            RowLayout {
                                width: parent.width
                                height: 28

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
                                    font.weight: Font.Medium
                                    opacity: seeAllMa.containsMouse ? 0.72 : 1.0

                                    MouseArea {
                                        id: seeAllMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: StoreCtrl.activeTab = shelf.modelData.tabId
                                    }
                                }
                            }

                            GridLayout {
                                width: parent.width
                                columns: storeRoot.width >= 600 ? 2 : 1
                                columnSpacing: 22
                                rowSpacing: 0

                                Repeater {
                                    model: shelf.modelData.items
                                    delegate: Rectangle {
                                        id: shelfRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 68
                                        radius: 8
                                        color: shelfRowMa.containsMouse
                                            ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.045) : "#F6F8FB")
                                            : "transparent"

                                        Behavior on color {
                                            enabled: !FinnTheme.reducedMotion
                                            ColorAnimation { duration: 100 }
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            anchors.rightMargin: 6
                                            spacing: 12

                                            FinnIcon {
                                                Layout.preferredWidth: 46
                                                Layout.preferredHeight: 46
                                                size: 46
                                                name: shelfRow.modelData.iconName
                                                fallbackName: win.ecoFallbackIcon(shelfRow.modelData.ecosystemId)
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
                                                        text: shelfRow.modelData.name
                                                        color: FinnTheme.ink
                                                        font.family: FinnTheme.fontFamily
                                                        font.pixelSize: 14
                                                        font.weight: Font.Medium
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                        Layout.minimumWidth: 0
                                                    }

                                                    Rectangle {
                                                        readonly property string lbl: win.ecoTagLabel(shelfRow.modelData.ecosystemId)
                                                        readonly property color clr: win.ecoTagColor(shelfRow.modelData.ecosystemId)
                                                        visible: lbl.length > 0
                                                        radius: 4
                                                        color: FinnTheme.withAlpha(clr, FinnTheme.dark ? 0.18 : 0.09)
                                                        implicitWidth: shTagTxt.implicitWidth + 10
                                                        implicitHeight: 17

                                                        Text {
                                                            id: shTagTxt
                                                            anchors.centerIn: parent
                                                            text: parent.lbl
                                                            color: parent.clr
                                                            font.family: FinnTheme.fontFamily
                                                            font.pixelSize: 10
                                                            font.weight: Font.Medium
                                                        }
                                                    }
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
                                                Layout.preferredWidth: 60
                                                Layout.preferredHeight: 26
                                                radius: 13
                                                color: getBtnMa.containsMouse
                                                    ? (FinnTheme.dark ? "#283B57" : "#D6E7FF")
                                                    : (FinnTheme.dark ? "#1E2B40" : "#EDF4FF")

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
                                            anchors.leftMargin: 64
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                            anchors.bottom: parent.bottom
                                            height: 0.5
                                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.07) : "#0D000000"
                                        }

                                        MouseArea {
                                            id: shelfRowMa
                                            anchors.fill: parent
                                            z: -1
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: StoreCtrl.openAppDetail(shelfRow.modelData.appId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── CATEGORY FILTER BAR (Linux / Android / Windows) ──────────
                Flickable {
                    id: catFilterBar
                    readonly property var catModel: {
                        if (StoreCtrl.activeTab === "flatpak") return [
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
                        if (StoreCtrl.activeTab === "android") return [
                            { id: "popular",   label: "Popular" },
                            { id: "internet",  label: "Internet" },
                            { id: "messaging", label: "Messaging" },
                            { id: "media",     label: "Media" },
                            { id: "games",     label: "Games" },
                            { id: "maps",      label: "Maps" },
                            { id: "office",    label: "Office" },
                            { id: "utilities", label: "Utilities" }
                        ]
                        if (StoreCtrl.activeTab === "windows") return [
                            { id: "popular",   label: "Popular" },
                            { id: "developer", label: "Developer" },
                            { id: "browsers",  label: "Browsers" },
                            { id: "media",     label: "Media" },
                            { id: "graphics",  label: "Graphics" },
                            { id: "gaming",    label: "Gaming" },
                            { id: "utilities", label: "Utilities" },
                            { id: "security",  label: "Security" }
                        ]
                        return []
                    }
                    width: parent.width
                    height: 30
                    visible: catModel.length > 0 && StoreCtrl.searchQuery.trim().length === 0
                    contentWidth: catRow.implicitWidth
                    clip: true

                    Row {
                        id: catRow
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Repeater {
                            model: catFilterBar.catModel
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool active: (StoreCtrl.activeTab === "flatpak" && StoreCtrl.flathubCollection === modelData.id)
                                    || (StoreCtrl.activeTab === "android" && StoreCtrl.androidCollection === modelData.id)
                                    || (StoreCtrl.activeTab === "windows" && StoreCtrl.wingetCollection === modelData.id)
                                width: catTxt.implicitWidth + 20
                                height: 26
                                radius: 13
                                color: active
                                    ? FinnTheme.accent
                                    : (catMa.containsMouse ? FinnTheme.surfaceHover : (FinnTheme.dark ? "#181D28" : "#F2F4F7"))

                                Behavior on color {
                                    enabled: !FinnTheme.reducedMotion
                                    ColorAnimation { duration: 100 }
                                }

                                Text {
                                    id: catTxt
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? "#FFFFFF" : FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: active ? Font.Medium : Font.Normal
                                }

                                MouseArea {
                                    id: catMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (StoreCtrl.activeTab === "flatpak") StoreCtrl.flathubCollection = modelData.id
                                        else if (StoreCtrl.activeTab === "android") StoreCtrl.androidCollection = modelData.id
                                        else if (StoreCtrl.activeTab === "windows") StoreCtrl.wingetCollection = modelData.id
                                    }
                                }
                            }
                        }
                    }
                }

                // ── ECOSYSTEM / SEARCH / UPDATES APP LIST (BORDERLESS) ──────
                GridLayout {
                    width: parent.width
                    visible: StoreCtrl.activeTab !== "discover" || StoreCtrl.searchQuery.trim().length > 0
                    columns: storeRoot.width >= 620 ? 2 : 1
                    columnSpacing: 22
                    rowSpacing: 0

                    Repeater {
                        model: StoreCtrl
                        delegate: Rectangle {
                            id: appRow
                            Layout.fillWidth: true
                            implicitHeight: model.taskProgress >= 0 ? 84 : 70
                            radius: 8
                            color: cardMa.containsMouse
                                ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.045) : "#F6F8FB")
                                : "transparent"

                            Behavior on color {
                                enabled: !FinnTheme.reducedMotion
                                ColorAnimation { duration: 100 }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    FinnIcon {
                                        Layout.preferredWidth: 46
                                        Layout.preferredHeight: 46
                                        size: 46
                                        name: model.iconName
                                        fallbackName: win.ecoFallbackIcon(model.ecosystemId)
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
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                            }

                                            // Show accurate platform tag in global Search & Updates
                                            Rectangle {
                                                readonly property string lbl: win.ecoTagLabel(model.ecosystemId)
                                                readonly property color clr: win.ecoTagColor(model.ecosystemId)
                                                visible: lbl.length > 0 && (StoreCtrl.searchQuery.trim().length > 0 || StoreCtrl.activeTab === "updates")
                                                radius: 4
                                                color: FinnTheme.withAlpha(clr, FinnTheme.dark ? 0.18 : 0.09)
                                                implicitWidth: ecoTagTxt.implicitWidth + 10
                                                implicitHeight: 17

                                                Text {
                                                    id: ecoTagTxt
                                                    anchors.centerIn: parent
                                                    text: parent.lbl
                                                    color: parent.clr
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
                                        radius: 13
                                        color: actMa.containsMouse
                                            ? (FinnTheme.dark ? "#283B57" : "#D6E7FF")
                                            : (FinnTheme.dark ? "#1E2B40" : "#EDF4FF")

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
                                            Behavior on width {
                                                enabled: !FinnTheme.reducedMotion
                                                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                            }
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
                                anchors.leftMargin: 64
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.bottom: parent.bottom
                                height: 0.5
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.07) : "#0D000000"
                            }

                            MouseArea {
                                id: cardMa
                                anchors.fill: parent
                                z: -1
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: StoreCtrl.openAppDetail(model.appId)
                            }
                        }
                    }
                }

                // ── LOAD MORE FLATHUB FOOTER ────────────────────────────────
                RowLayout {
                    width: parent.width
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

        // ── FULL-SPACE ANIMATED APP LISTING VIEW (NOT A POPUP) ──────────────
        Rectangle {
            id: detailView
            anchors.fill: parent
            z: 30
            color: FinnTheme.dark ? FinnTheme.bone : "#FFFFFF"
            visible: opacity > 0.01
            opacity: StoreCtrl.detailOpen ? 1.0 : 0.0
            scale: StoreCtrl.detailOpen ? 1.0 : 0.985
            transformOrigin: Item.Center

            property real slideOffset: StoreCtrl.detailOpen ? 0 : 16
            transform: Translate { y: detailView.slideOffset }

            Behavior on opacity {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }
            Behavior on scale {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
            }
            Behavior on slideOffset {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
            }

            readonly property var app: StoreCtrl.selectedApp || {}
            readonly property string ecoId: app.ecosystemId || "native"
            readonly property string ecoLbl: win.ecoTagLabel(ecoId)
            readonly property color ecoClr: win.ecoTagColor(ecoId)

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Clean Back Navigation Row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    spacing: 8

                    Rectangle {
                        width: backRow.implicitWidth + 16
                        height: 28
                        radius: 6
                        color: backMa.containsMouse ? FinnTheme.surfaceHover : "transparent"

                        Row {
                            id: backRow
                            anchors.centerIn: parent
                            spacing: 4
                            FinnIcon {
                                name: "chevron-left"
                                size: 15
                                tint: FinnTheme.accent
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: win.tabTitle(StoreCtrl.activeTab)
                                color: FinnTheme.accent
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: backMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: StoreCtrl.closeAppDetail()
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                FinnDivider { Layout.fillWidth: true }

                // Full-Space Scrollable Listing Content
                Flickable {
                    id: detailFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: detailCol.implicitHeight + 36
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Connections {
                        target: StoreCtrl
                        function onSelectedAppChanged() {
                            if (StoreCtrl.detailOpen) detailFlick.contentY = 0
                        }
                    }

                    ColumnLayout {
                        id: detailCol
                        width: parent.width
                        anchors.top: parent.top
                        anchors.topMargin: 22
                        spacing: 22

                        // 1. App Header
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 18

                            FinnIcon {
                                Layout.preferredWidth: 84
                                Layout.preferredHeight: 84
                                size: 84
                                name: detailView.app.iconName || win.ecoFallbackIcon(detailView.ecoId)
                                fallbackName: win.ecoFallbackIcon(detailView.ecoId)
                                monochrome: false
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: detailView.app.name || ""
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 22
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: detailView.app.developer || ""
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                }

                                RowLayout {
                                    spacing: 8
                                    Layout.topMargin: 3

                                    Rectangle {
                                        visible: detailView.ecoLbl.length > 0
                                        radius: 4
                                        color: FinnTheme.withAlpha(detailView.ecoClr, FinnTheme.dark ? 0.20 : 0.10)
                                        implicitWidth: dEcoTxt.implicitWidth + 12
                                        implicitHeight: 20

                                        Text {
                                            id: dEcoTxt
                                            anchors.centerIn: parent
                                            text: detailView.ecoLbl
                                            color: detailView.ecoClr
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }

                                    Text {
                                        visible: !!detailView.app.verified && (detailView.app.verifiedSource || "").length > 0
                                        text: detailView.app.verifiedSource || ""
                                        color: FinnTheme.mute
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 11
                                    }
                                }
                            }

                            // Primary Action Column
                            ColumnLayout {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 6

                                RowLayout {
                                    spacing: 8

                                    FinnButton {
                                        visible: (detailView.app.taskProgress === undefined || detailView.app.taskProgress < 0)
                                        text: detailView.app.updateAvailable ? "Update" : (detailView.app.installed ? "Open" : "Get")
                                        variant: "primary"
                                        implicitWidth: 92
                                        implicitHeight: 32
                                        radius: 16
                                        onClicked: {
                                            if (detailView.app.installed && !detailView.app.updateAvailable)
                                                StoreCtrl.launchApp(detailView.app.appId)
                                            else
                                                StoreCtrl.installApp(detailView.app.appId)
                                        }
                                    }

                                    FinnButton {
                                        visible: !!detailView.app.installed && (detailView.app.taskProgress === undefined || detailView.app.taskProgress < 0)
                                        text: "Uninstall"
                                        variant: "secondary"
                                        implicitHeight: 32
                                        radius: 16
                                        onClicked: StoreCtrl.uninstallApp(detailView.app.appId)
                                    }
                                }

                                ColumnLayout {
                                    visible: detailView.app.taskProgress !== undefined && detailView.app.taskProgress >= 0
                                    Layout.preferredWidth: 180
                                    spacing: 4

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 5
                                        radius: 2.5
                                        color: FinnTheme.surfaceInput
                                        Rectangle {
                                            width: parent.width * Math.max(0.08, Math.min(1.0, detailView.app.taskProgress || 0))
                                            height: parent.height
                                            radius: 2.5
                                            color: FinnTheme.accent
                                        }
                                    }
                                    Text {
                                        text: detailView.app.taskState || "Installing..."
                                        color: FinnTheme.mute
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        FinnDivider { Layout.fillWidth: true }

                        // 2. Clean Hairline Metadata Strip
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            spacing: 0

                            Repeater {
                                model: [
                                    { k: "Platform", v: detailView.app.ecosystemLabel || detailView.ecoLbl || "—" },
                                    { k: "Category", v: detailView.app.category || "Apps" },
                                    { k: "Version",  v: detailView.app.version || "Latest" },
                                    { k: "Size",     v: detailView.app.sizeLabel || "—" }
                                ]
                                delegate: Item {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true

                                    Rectangle {
                                        visible: index > 0
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 0.5
                                        height: 28
                                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#12000000"
                                    }

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 2
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.k
                                            color: FinnTheme.mute
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 11
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.v
                                            color: FinnTheme.ink
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                            }
                        }

                        FinnDivider { Layout.fillWidth: true }

                        // 3. Screenshots Gallery (when provided by Flathub AppStream)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: !!(detailView.app.screenshots && detailView.app.screenshots.length > 0)

                            Flickable {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 224
                                contentWidth: shotRow.implicitWidth
                                clip: true

                                Row {
                                    id: shotRow
                                    spacing: 12
                                    Repeater {
                                        model: detailView.app.screenshots || []
                                        delegate: FinnSquircle {
                                            required property var modelData
                                            width: 364
                                            height: 220
                                            radius: FinnTheme.radiusMd
                                            color: FinnTheme.dark ? "#141923" : "#F2F4F8"
                                            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#10000000"
                                            borderWidth: 0.5
                                            specularRim: false
                                            clip: true

                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                source: modelData
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                smooth: true
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 4. About / Full Description
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "About"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 15
                                font.weight: Font.Medium
                            }

                            Text {
                                text: detailView.app.description || detailView.app.subtitle || "No description provided."
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                lineHeight: 1.5
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // 5. Permissions & Package Info
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: !!(detailView.app.permissions && detailView.app.permissions.length > 0)

                            FinnDivider { Layout.fillWidth: true }

                            Text {
                                text: "Information"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 15
                                font.weight: Font.Medium
                            }

                            Text {
                                visible: (detailView.app.installRef || "").length > 0
                                text: "Package ID: " + (detailView.app.installRef || "")
                                color: FinnTheme.mute
                                font.family: FinnTheme.monoFontFamily
                                font.pixelSize: 11
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8

                                Repeater {
                                    model: detailView.app.permissions || []
                                    delegate: Rectangle {
                                        required property var modelData
                                        radius: 5
                                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#F4F6FA"
                                        implicitWidth: permTxt.implicitWidth + 16
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
                    }
                }
            }
        }

        // ── Local Package Sideload Sheet (--install <path> via Finn Files) ──
        Rectangle {
            anchors.fill: parent
            visible: StoreCtrl.localPackageOpen
            color: Qt.rgba(0, 0, 0, 0.38)
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
}
