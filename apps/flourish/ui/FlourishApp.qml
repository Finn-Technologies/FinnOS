import QtQuick
import QtWebEngine
import FinnUI 1.0

// Flourish — Zen Browser-inspired Chromium browser built natively with FinnUI.
// Continuous L-shaped real-time frosted glass blur flowing seamlessly from the
// vertical sidebar across the entire 40px top bar, with symmetric 6px padding
// around the 28px WindowOptions pill (top, bottom, right) and real website favicons.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1100
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 680
    appTitle: "Flourish"
    pageTitle: ""
    navItems: []
    showSidebarSearch: false
    fullBleedContent: true
    customSidebarWidth: 0
    // Continuous Liquid Glass tint across the entire L-shaped sidebar + top bar + outer frame
    windowContentColor: FinnTheme.dark ? "#850E121B" : "#78FFFFFF"

    property bool sidebarCollapsed: false
    property bool splitView: false
    property bool showFindBar: false
    property bool showCommandBar: false
    property int activeTabIdx: 0
    property int nextTabId: 2
    property string findQuery: ""
    property string currentTabUrl: "https://duckduckgo.com"
    property string currentTabIcon: ""

    onActiveTabIdxChanged: {
        if (activeTabIdx >= 0 && activeTabIdx < tabsModel.count) {
            const row = tabsModel.get(activeTabIdx)
            currentTabUrl = row.url || ""
            currentTabIcon = row.iconUrl || ""
        }
    }

    ListModel {
        id: tabsModel
        ListElement {
            tabId: 1
            title: "New Tab"
            url: "https://duckduckgo.com"
            iconUrl: ""
        }
    }

    Component.onCompleted: {
        if (FlourishInitialUrl && FlourishInitialUrl.length > 0) {
            const u = normalizeInput(FlourishInitialUrl)
            tabsModel.setProperty(0, "url", u)
            tabsModel.setProperty(0, "title", displayHost(u))
            currentTabUrl = u
        }
    }

    function normalizeInput(raw) {
        const s = raw.trim()
        if (s.length === 0) return "https://duckduckgo.com"
        if (/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(s)) return s
        if (s.indexOf(" ") === -1 && s.indexOf(".") > 0) return "https://" + s
        return "https://duckduckgo.com/?q=" + encodeURIComponent(s)
    }

    function displayHost(u) {
        if (!u || u.length === 0) return "Search or enter URL"
        const m = u.toString().match(/^https?:\/\/(?:www\.)?([^\/?#]+)/i)
        return m ? m[1] : u.toString()
    }

    function faviconFallback(u) {
        const host = displayHost(u)
        if (!host || host === "Search or enter URL" || host.indexOf(".") < 0) return ""
        return "https://www.google.com/s2/favicons?sz=64&domain=" + encodeURIComponent(host)
    }

    function activeView() {
        return webRepeater.itemAt(activeTabIdx)
    }

    function openTab(targetUrl, label) {
        const id = nextTabId++
        const u = normalizeInput(targetUrl || "https://duckduckgo.com")
        tabsModel.append({ tabId: id, title: label || displayHost(u), url: u, iconUrl: "" })
        activeTabIdx = tabsModel.count - 1
    }

    function closeTab(idx) {
        if (tabsModel.count <= 1) {
            const v = activeView()
            if (v) v.url = "https://duckduckgo.com"
            return
        }
        tabsModel.remove(idx)
        if (activeTabIdx >= tabsModel.count) activeTabIdx = tabsModel.count - 1
    }

    function navigateActive(raw) {
        const u = normalizeInput(raw)
        const v = activeView()
        if (v) v.url = u
        tabsModel.setProperty(activeTabIdx, "url", u)
        tabsModel.setProperty(activeTabIdx, "iconUrl", "")
        currentTabUrl = u
        currentTabIcon = ""
    }

    // Keyboard shortcuts (Zen / Arc ergonomics)
    Shortcut { sequence: "Ctrl+T"; onActivated: win.showCommandBar = true }
    Shortcut { sequence: "Ctrl+K"; onActivated: win.showCommandBar = true }
    Shortcut { sequence: "Ctrl+L"; onActivated: { urlInput.forceActiveFocus(); urlInput.selectAll() } }
    Shortcut { sequence: "Ctrl+S"; onActivated: win.sidebarCollapsed = !win.sidebarCollapsed }
    Shortcut { sequence: "Ctrl+W"; onActivated: win.closeTab(win.activeTabIdx) }
    Shortcut { sequence: "Ctrl+F"; onActivated: { win.showFindBar = !win.showFindBar; if (win.showFindBar) findInput.forceActiveFocus() } }
    Shortcut { sequence: "Ctrl+R"; onActivated: { const v = win.activeView(); if (v) v.reload() } }
    Shortcut { sequence: "Escape"; onActivated: { win.showCommandBar = false; win.showFindBar = false } }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- Continuous Frosted Top Bar (height 40 = 6px top + 28px WindowOptions/URL pill + 6px bottom!) ----
        Item {
            id: topBar
            width: parent.width
            height: 40

            // Left navigation controls aligned cleanly with the sidebar
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                FinnIconButton {
                    iconName: "layers"
                    buttonSize: 28
                    iconSize: 14
                    onClicked: win.sidebarCollapsed = !win.sidebarCollapsed
                }
                FinnIconButton {
                    iconName: "chevron-left"
                    buttonSize: 28
                    iconSize: 14
                    enabled: win.activeView() ? win.activeView().canGoBack : false
                    onClicked: { const v = win.activeView(); if (v) v.goBack() }
                }
                FinnIconButton {
                    iconName: "chevron-right"
                    buttonSize: 28
                    iconSize: 14
                    enabled: win.activeView() ? win.activeView().canGoForward : false
                    onClicked: { const v = win.activeView(); if (v) v.goForward() }
                }
                FinnIconButton {
                    iconName: "refresh"
                    buttonSize: 28
                    iconSize: 14
                    onClicked: {
                        const v = win.activeView()
                        if (!v) return
                        if (v.loading) v.stop()
                        else v.reload()
                    }
                }
            }

            // Centered URL / Search Bar (28px tall, radius 6 matching WindowOptions exactly!)
            FinnSquircle {
                id: urlPill
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(150, (parent.width - width) * 0.5)
                width: Math.min(480, Math.max(240, parent.width - 360))
                height: 28
                radius: 6
                color: urlInput.activeFocus
                    ? FinnTheme.surface
                    : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : FinnTheme.withAlpha("#FFFFFF", 0.80))
                borderColor: urlInput.activeFocus ? FinnTheme.accent : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000")
                borderWidth: urlInput.activeFocus ? 1.2 : 0.5

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: {
                        urlInput.forceActiveFocus()
                        urlInput.selectAll()
                    }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 7

                    Item {
                        width: 14
                        height: 14
                        anchors.verticalCenter: parent.verticalCenter

                        Image {
                            id: barFav
                            anchors.fill: parent
                            source: win.currentTabIcon.length > 0
                                ? win.currentTabIcon
                                : win.faviconFallback(win.currentTabUrl)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                            visible: !urlInput.activeFocus && status === Image.Ready
                        }

                        FinnIcon {
                            visible: urlInput.activeFocus || barFav.status !== Image.Ready
                            anchors.centerIn: parent
                            name: "search-01"
                            size: 13
                            tint: urlInput.activeFocus ? FinnTheme.accent : FinnTheme.mute
                        }
                    }

                    TextInput {
                        id: urlInput
                        width: parent.width - 22
                        height: parent.height
                        verticalAlignment: TextInput.AlignVCenter
                        horizontalAlignment: activeFocus ? TextInput.AlignLeft : TextInput.AlignHCenter
                        color: FinnTheme.ink
                        selectionColor: FinnTheme.withAlpha(FinnTheme.accent, 0.25)
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 12
                        selectByMouse: true
                        clip: true
                        text: activeFocus
                            ? win.currentTabUrl
                            : win.displayHost(win.currentTabUrl)
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                text = win.currentTabUrl
                                selectAll()
                            } else {
                                text = win.displayHost(win.currentTabUrl)
                            }
                        }
                        onAccepted: {
                            win.navigateActive(text)
                            focus = false
                        }
                    }
                }
            }

            // Subtle loading progress hairline along bottom of top bar
            Rectangle {
                visible: win.activeView() ? win.activeView().loading : false
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * ((win.activeView() ? win.activeView().loadProgress : 0) / 100.0)
                color: FinnTheme.accent
            }
        }

        // ---- Main Body: Seamless Frosted Sidebar + Rounded Framed Chromium Canvas ----
        Row {
            width: parent.width
            height: parent.height - topBar.height
            spacing: 0

            // Left Frosted Vertical Sidebar (no URL bar clutter!)
            Item {
                id: zenSidebar
                width: win.sidebarCollapsed ? 0 : 236
                height: parent.height
                visible: width > 2
                clip: true

                Behavior on width {
                    enabled: !FinnTheme.reducedMotion
                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                }

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.topMargin: 2
                    anchors.bottomMargin: 8
                    spacing: 10

                    // Zen Essentials (3 frosted tiles with real website favicons)
                    Row {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: [
                                { label: "DuckDuckGo", glyph: "D", color: "#DE5833", url: "https://duckduckgo.com", icon: "https://duckduckgo.com/favicon.ico" },
                                { label: "GitHub",     glyph: "G", color: "#24292F", url: "https://github.com",     icon: "https://github.com/fluidicon.png" },
                                { label: "Wikipedia",  glyph: "W", color: "#1E293B", url: "https://wikipedia.org",  icon: "https://en.wikipedia.org/static/favicon/wikipedia.ico" }
                            ]
                            delegate: FinnSquircle {
                                width: (parent.width - 16) / 3
                                height: 46
                                radius: 12
                                color: eMa.containsMouse
                                    ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.18) : FinnTheme.withAlpha("#FFFFFF", 0.94))
                                    : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : FinnTheme.withAlpha("#FFFFFF", 0.64))
                                borderColor: eMa.containsMouse ? FinnTheme.hair : FinnTheme.hairSoft
                                borderWidth: 0.6

                                Image {
                                    id: essIcon
                                    anchors.centerIn: parent
                                    width: 22
                                    height: 22
                                    source: modelData.icon
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    asynchronous: true
                                    visible: status === Image.Ready
                                }

                                Rectangle {
                                    visible: essIcon.status !== Image.Ready
                                    anchors.centerIn: parent
                                    width: 22
                                    height: 22
                                    radius: 6
                                    color: modelData.color

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.glyph
                                        color: "#FFFFFF"
                                        font.family: FinnTheme.fontFamily
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                }

                                MouseArea {
                                    id: eMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.navigateActive(modelData.url)
                                }
                            }
                        }
                    }

                    FinnDivider { width: parent.width }

                    // + New Tab Row
                    FinnSquircle {
                        width: parent.width
                        height: 32
                        radius: 9
                        color: newTabMa.containsMouse
                            ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : FinnTheme.withAlpha("#000000", 0.05))
                            : "transparent"
                        borderWidth: 0

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 8

                            Text {
                                text: "+"
                                color: FinnTheme.dim
                                font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "New Tab"
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: newTabMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.openTab("https://duckduckgo.com", "New Tab")
                        }
                    }

                    // Vertical Tabs List with real website favicons
                    ListView {
                        width: parent.width
                        height: Math.max(80, parent.height - 154)
                        model: tabsModel
                        spacing: 4
                        clip: true

                        delegate: Item {
                            width: ListView.view.width
                            height: 34
                            readonly property bool isSel: index === win.activeTabIdx
                            readonly property string resolvedIcon: (model.iconUrl && model.iconUrl.length > 0)
                                ? model.iconUrl
                                : win.faviconFallback(model.url)

                            FinnSquircle {
                                anchors.fill: parent
                                radius: 9
                                color: isSel
                                    ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.16) : FinnTheme.withAlpha("#FFFFFF", 0.90))
                                    : (tMa.containsMouse
                                        ? (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : FinnTheme.withAlpha("#FFFFFF", 0.48))
                                        : "transparent")
                                borderColor: isSel ? FinnTheme.hairSoft : "transparent"
                                borderWidth: isSel ? 0.6 : 0
                            }

                            Rectangle {
                                visible: isSel
                                x: 3
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3
                                height: 16
                                radius: 1.5
                                color: FinnTheme.accent
                            }

                            MouseArea {
                                id: tMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: win.activeTabIdx = index
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                anchors.rightMargin: 8
                                spacing: 8

                                Item {
                                    width: 16
                                    height: 16
                                    anchors.verticalCenter: parent.verticalCenter

                                    Image {
                                        id: tabFav
                                        anchors.fill: parent
                                        source: resolvedIcon
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        asynchronous: true
                                        visible: status === Image.Ready
                                    }

                                    FinnIcon {
                                        visible: tabFav.status !== Image.Ready
                                        anchors.centerIn: parent
                                        name: "compass"
                                        size: 14
                                        tint: isSel ? FinnTheme.accent : FinnTheme.mute
                                    }
                                }

                                Text {
                                    text: model.title
                                    color: isSel ? FinnTheme.ink : FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: isSel ? Font.Medium : Font.Normal
                                    elide: Text.ElideRight
                                    width: Math.max(40, parent.width - 44)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    visible: tabsModel.count > 1 && (tMa.containsMouse || isSel)
                                    text: "×"
                                    color: cMa.containsMouse ? FinnTheme.ink : FinnTheme.mute
                                    font.pixelSize: 14
                                    anchors.verticalCenter: parent.verticalCenter

                                    MouseArea {
                                        id: cMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: win.closeTab(index)
                                    }
                                }
                            }
                        }
                    }

                    // Bottom sleek Zen utility pill
                    FinnSquircle {
                        width: parent.width
                        height: 32
                        radius: 10
                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : FinnTheme.withAlpha("#FFFFFF", 0.56)
                        borderColor: FinnTheme.hairSoft
                        borderWidth: 0.5

                        Row {
                            anchors.centerIn: parent
                            spacing: 14

                            FinnIconButton {
                                iconName: "search-01"
                                buttonSize: 24
                                iconSize: 14
                                onClicked: { win.showFindBar = !win.showFindBar; if (win.showFindBar) findInput.forceActiveFocus() }
                            }

                            FinnIconButton {
                                iconName: "grid"
                                buttonSize: 24
                                iconSize: 14
                                onClicked: win.splitView = !win.splitView
                            }

                            FinnIconButton {
                                iconName: "plus"
                                buttonSize: 24
                                iconSize: 14
                                onClicked: win.showCommandBar = true
                            }
                        }
                    }
                }
            }

            // Right Rounded Framed Chromium Viewport (nestled seamlessly inside the L-shaped frosted glass!)
            Item {
                width: parent.width - zenSidebar.width
                height: parent.height

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: win.sidebarCollapsed ? 8 : 0
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 8

                    FinnSquircle {
                        width: win.splitView ? (parent.width - 8) * 0.5 : parent.width
                        height: parent.height
                        radius: 10
                        color: "#FFFFFF"
                        borderColor: FinnTheme.hairSoft
                        borderWidth: 0.7
                        clip: true

                        Repeater {
                            id: webRepeater
                            model: tabsModel
                            delegate: WebEngineView {
                                anchors.fill: parent
                                visible: index === win.activeTabIdx
                                url: model.url
                                onTitleChanged: {
                                    if (title && title.length > 0) {
                                        tabsModel.setProperty(index, "title", title)
                                    }
                                }
                                onUrlChanged: {
                                    tabsModel.setProperty(index, "url", url.toString())
                                    if (index === win.activeTabIdx) {
                                        win.currentTabUrl = url.toString()
                                    }
                                }
                                onIconChanged: {
                                    if (icon && icon.toString().length > 0) {
                                        tabsModel.setProperty(index, "iconUrl", icon.toString())
                                        if (index === win.activeTabIdx) {
                                            win.currentTabIcon = icon.toString()
                                        }
                                    }
                                }
                                onNewWindowRequested: (request) => {
                                    win.openTab(request.requestedUrl.toString(), "New Tab")
                                }
                            }
                        }
                    }

                    // Optional Split View Second Live Chromium Pane
                    FinnSquircle {
                        visible: win.splitView
                        width: win.splitView ? (parent.width - 8) * 0.5 : 0
                        height: parent.height
                        radius: 10
                        color: "#FFFFFF"
                        borderColor: FinnTheme.hairSoft
                        borderWidth: 0.7
                        clip: true

                        WebEngineView {
                            id: splitWebView
                            anchors.fill: parent
                            url: "https://wikipedia.org"
                        }
                    }
                }

                // Floating Find-in-Page Pill (Ctrl+F)
                FinnSquircle {
                    visible: win.showFindBar
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    width: 280
                    height: 36
                    radius: 10
                    color: FinnTheme.surface
                    borderColor: FinnTheme.hair
                    borderWidth: 0.8
                    z: 30

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6

                        TextInput {
                            id: findInput
                            width: parent.width - 60
                            height: parent.height
                            verticalAlignment: TextInput.AlignVCenter
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                            text: win.findQuery
                            onTextChanged: {
                                win.findQuery = text
                                const v = win.activeView()
                                if (v) v.findText(text)
                            }
                            onAccepted: {
                                const v = win.activeView()
                                if (v) v.findText(text)
                            }

                            Text {
                                visible: findInput.text.length === 0
                                text: "Find in page..."
                                color: FinnTheme.mute
                                font: findInput.font
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        FinnButton {
                            text: "×"
                            implicitHeight: 22
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                win.showFindBar = false
                                const v = win.activeView()
                                if (v) v.findText("")
                            }
                        }
                    }
                }
            }
        }
    }

    // Floating Zen Command Bar Overlay (Ctrl+T / Ctrl+K)
    Item {
        visible: win.showCommandBar
        anchors.fill: parent
        z: 50

        Rectangle {
            anchors.fill: parent
            color: FinnTheme.withAlpha("#0B0E14", 0.35)
            MouseArea {
                anchors.fill: parent
                onClicked: win.showCommandBar = false
            }
        }

        FinnSquircle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 110
            width: Math.min(540, parent.width - 80)
            height: 52
            radius: 14
            color: FinnTheme.surface
            borderColor: FinnTheme.accent
            borderWidth: 1.2

            Row {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 10

                FinnIcon {
                    name: "search-01"
                    size: 18
                    tint: FinnTheme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: cmdInput
                    width: parent.width - 36
                    height: parent.height
                    verticalAlignment: TextInput.AlignVCenter
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 15
                    focus: win.showCommandBar
                    onAccepted: {
                        win.openTab(text, "")
                        text = ""
                        win.showCommandBar = false
                    }

                    Text {
                        visible: cmdInput.text.length === 0
                        text: "Search or enter URL to open in a new tab..."
                        color: FinnTheme.mute
                        font: cmdInput.font
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
