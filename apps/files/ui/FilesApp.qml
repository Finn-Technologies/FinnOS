import QtQuick
import FinnUI 1.0

// Finn Files — Cohesive FinnOS split-pane with real-time frosted sidebar,
// crisp pill breadcrumbs, Grid/List switcher, and right-hand file inspector.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 940
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 590
    appTitle: "Finn Files"
    pageTitle: FilesCtrl.currentFolderName
    activeNavId: "home"
    mobileDetailOpen: true
    property bool gridView: false
    property string inlinePromptMode: "" // "", "mkdir", "rename"
    property string inlinePromptText: ""

    ListModel {
        id: fileTabs
        ListElement { title: "Home"; path: "" }
    }
    property int activeTabIdx: 0

    function openNewTab() {
        fileTabs.append({ title: FilesCtrl.currentFolderName, path: FilesCtrl.currentPath })
        activeTabIdx = fileTabs.count - 1
    }

    function switchTab(idx) {
        if (idx < 0 || idx >= fileTabs.count) return
        activeTabIdx = idx
        const p = fileTabs.get(idx).path
        if (p && p.length > 0 && p !== FilesCtrl.currentPath) {
            FilesCtrl.navigateTo(p)
        }
    }

    function closeTab(idx) {
        if (fileTabs.count <= 1) return
        fileTabs.remove(idx)
        if (activeTabIdx >= fileTabs.count) activeTabIdx = fileTabs.count - 1
        switchTab(activeTabIdx)
    }

    navItems: [
        { id: "home", label: "Home", icon: "home-09", badgeColor: "#007BFF", section: "places" },
        { id: "desktop", label: "Desktop", icon: "desktop", badgeColor: "#7C5CFF", section: "places" },
        { id: "documents", label: "Documents", icon: "file-text", badgeColor: "#2563EB", section: "places" },
        { id: "downloads", label: "Downloads", icon: "download", badgeColor: "#059669", section: "places" },
        { id: "pictures", label: "Pictures", icon: "image", badgeColor: "#E11D48", section: "places" },
        { id: "music", label: "Music", icon: "volume-02", badgeColor: "#D97706", section: "places" },
        { id: "root", label: "Filesystem", icon: "hard-drive", badgeColor: "#475569", section: "devices" }
    ]

    onNavSelected: (id) => FilesCtrl.navigatePlace(id)
    onSidebarSearchTextChanged: FilesCtrl.filterText = sidebarSearchText

    Connections {
        target: FilesCtrl
        function onPathChanged() {
            if (activeTabIdx >= 0 && activeTabIdx < fileTabs.count) {
                fileTabs.setProperty(activeTabIdx, "title", FilesCtrl.currentFolderName)
                fileTabs.setProperty(activeTabIdx, "path", FilesCtrl.currentPath)
            }
        }
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    headerActions: [
        FinnIconButton {
            iconName: "chevron-left"
            buttonSize: 28
            iconSize: 15
            enabled: FilesCtrl.canGoBack
            onClicked: FilesCtrl.goBack()
        },
        FinnIconButton {
            iconName: "chevron-right"
            buttonSize: 28
            iconSize: 15
            enabled: FilesCtrl.canGoForward
            onClicked: FilesCtrl.goForward()
        },
        FinnIconButton {
            iconName: "refresh"
            buttonSize: 28
            iconSize: 15
            onClicked: FilesCtrl.refresh()
        },
        FinnButton {
            text: "+ Folder"
            implicitHeight: 28
            onClicked: {
                win.inlinePromptMode = "mkdir"
                win.inlinePromptText = ""
            }
        },
        FinnButton {
            text: FilesCtrl.showHidden ? "Hidden: On" : "Hidden"
            implicitHeight: 28
            variant: FilesCtrl.showHidden ? "primary" : "secondary"
            onClicked: FilesCtrl.showHidden = !FilesCtrl.showHidden
        },
        FinnButton {
            text: "Sort: " + FilesCtrl.sortBy
            implicitHeight: 28
            onClicked: {
                FilesCtrl.sortBy = (FilesCtrl.sortBy === "name") ? "date"
                                 : (FilesCtrl.sortBy === "date") ? "size" : "name"
            }
        },
        FinnSquircle {
            width: 88
            height: 28
            radius: 14
            color: FinnTheme.surfaceCanvas
            borderColor: FinnTheme.hairSoft
            borderWidth: 0.7

            Row {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                Repeater {
                    model: [
                        { label: "List", isGrid: false },
                        { label: "Grid", isGrid: true }
                    ]
                    delegate: FinnSquircle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: 12
                        color: win.gridView === modelData.isGrid ? FinnTheme.surface : "transparent"
                        borderColor: win.gridView === modelData.isGrid ? FinnTheme.hairSoft : "transparent"
                        borderWidth: win.gridView === modelData.isGrid ? 0.6 : 0

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: win.gridView === modelData.isGrid ? FinnTheme.ink : FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            font.weight: win.gridView === modelData.isGrid ? Font.Medium : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.gridView = modelData.isGrid
                        }
                    }
                }
            }
        },
        FinnButton {
            text: "Terminal"
            implicitHeight: 28
            onClicked: FilesCtrl.openInTerminal()
        }
    ]

    Column {
        anchors.fill: parent
        spacing: 8

        // Dolphin-style Multi-Tab Strip + Paste/Clipboard Actions
        Row {
            width: parent.width
            height: 28
            spacing: 6

            Repeater {
                model: fileTabs
                delegate: FinnSquircle {
                    width: Math.min(148, Math.max(78, tLbl.implicitWidth + 36))
                    height: 28
                    radius: 8
                    color: index === win.activeTabIdx ? FinnTheme.surface : FinnTheme.surfaceCanvas
                    borderColor: index === win.activeTabIdx ? FinnTheme.withAlpha(FinnTheme.accent, 0.45) : FinnTheme.hairSoft
                    borderWidth: 0.7

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.switchTab(index)
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 6

                        Text {
                            id: tLbl
                            text: model.title
                            color: index === win.activeTabIdx ? FinnTheme.ink : FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            font.weight: index === win.activeTabIdx ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                            width: Math.max(36, parent.width - 22)
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            visible: fileTabs.count > 1
                            text: "×"
                            color: FinnTheme.mute
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: win.closeTab(index)
                            }
                        }
                    }
                }
            }

            FinnButton {
                text: "+"
                implicitHeight: 28
                onClicked: win.openNewTab()
            }

            FinnButton {
                visible: FilesCtrl.hasClipboard
                text: FilesCtrl.clipboardIsCut ? "Move Here" : "Paste Here"
                variant: "primary"
                implicitHeight: 28
                onClicked: FilesCtrl.pasteHere()
            }
        }

        // Inline New Folder / Rename Bar
        FinnSquircle {
            visible: win.inlinePromptMode.length > 0
            width: parent.width
            height: 36
            radius: 10
            color: FinnTheme.surfaceCanvas
            borderColor: FinnTheme.accent
            borderWidth: 1

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Text {
                    text: win.inlinePromptMode === "mkdir" ? "New Folder:" : "Rename to:"
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: promptField
                    width: Math.max(120, parent.width - 230)
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    text: win.inlinePromptText
                    onTextChanged: win.inlinePromptText = text
                    onAccepted: {
                        if (win.inlinePromptMode === "mkdir") FilesCtrl.createFolder(text)
                        else FilesCtrl.renameSelected(text)
                        win.inlinePromptMode = ""
                    }
                }

                FinnButton {
                    text: "Apply"
                    variant: "primary"
                    implicitHeight: 24
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: {
                        if (win.inlinePromptMode === "mkdir") FilesCtrl.createFolder(win.inlinePromptText)
                        else FilesCtrl.renameSelected(win.inlinePromptText)
                        win.inlinePromptMode = ""
                    }
                }

                FinnButton {
                    text: "Cancel"
                    implicitHeight: 24
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: win.inlinePromptMode = ""
                }
            }
        }

        // Breadcrumb Trail + Item Count Pill
        FinnSquircle {
            width: parent.width
            height: 36
            radius: 10
            color: FinnTheme.surfaceCanvas
            borderColor: FinnTheme.hairSoft
            borderWidth: 0.7

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: countBadge.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                clip: true

                FinnIcon {
                    name: "folder"
                    size: 14
                    tint: FinnTheme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }

                Repeater {
                    model: FilesCtrl.breadcrumbs
                    delegate: Row {
                        spacing: 5
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            visible: index > 1
                            text: "›"
                            color: FinnTheme.mute
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: modelData.label
                            color: index === FilesCtrl.breadcrumbs.length - 1 ? FinnTheme.ink : FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                            font.weight: index === FilesCtrl.breadcrumbs.length - 1 ? Font.Medium : Font.Normal
                            anchors.verticalCenter: parent.verticalCenter

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: FilesCtrl.navigateTo(modelData.path)
                            }
                        }
                    }
                }
            }

            FinnBadge {
                id: countBadge
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: FilesCtrl.itemCount + (FilesCtrl.itemCount === 1 ? " item" : " items")
            }
        }

        // Main File Surface + Right Inspector
        Row {
            width: parent.width
            height: parent.height - 80 - (win.inlinePromptMode.length > 0 ? 44 : 0)
            spacing: 12

            // Directory Listing Card
            FinnCard {
                width: (win.isDesktop && !win.gridView) ? parent.width - 224 : parent.width
                height: parent.height
                radius: FinnTheme.radiusLg

                // List Mode Header + List
                Column {
                    visible: !win.gridView && FilesCtrl.itemCount > 0
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    // Subtle column header row
                    Item {
                        width: parent.width
                        height: 24

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Text {
                                text: "Name"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                width: Math.max(80, parent.width - 216)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Modified"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                width: 120
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Size"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                width: 66
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        FinnDivider {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                        }
                    }

                    ListView {
                        width: parent.width
                        height: parent.height - 28
                        model: FilesCtrl
                        clip: true
                        spacing: 2

                        delegate: Item {
                            width: ListView.view.width
                            height: 38
                            readonly property bool isSel: FilesCtrl.selectedIndex === index

                            FinnSquircle {
                                anchors.fill: parent
                                radius: 8
                                color: isSel ? FinnTheme.withAlpha(FinnTheme.accent, 0.13)
                                             : (rowMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                                borderColor: isSel ? FinnTheme.withAlpha(FinnTheme.accent, 0.42) : "transparent"
                                borderWidth: isSel ? 0.9 : 0
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10

                                FinnSquircle {
                                    width: 26
                                    height: 26
                                    radius: 7
                                    color: isDir ? FinnTheme.withAlpha("#007BFF", 0.12) : FinnTheme.surfaceCanvas
                                    borderWidth: 0
                                    anchors.verticalCenter: parent.verticalCenter

                                    FinnIcon {
                                        anchors.centerIn: parent
                                        name: iconName
                                        size: 15
                                        tint: isDir ? "#007BFF" : FinnTheme.dim
                                    }
                                }

                                Text {
                                    text: name
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: isSel ? Font.Medium : Font.Normal
                                    elide: Text.ElideRight
                                    width: Math.max(80, parent.width - 242)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: modifiedText
                                    color: FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                    width: 120
                                    horizontalAlignment: Text.AlignRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: sizeText
                                    color: FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                    width: 66
                                    horizontalAlignment: Text.AlignRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: rowMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: FilesCtrl.selectedIndex = index
                                onDoubleClicked: FilesCtrl.activateIndex(index)
                            }
                        }
                    }
                }

                // Grid Mode
                GridView {
                    visible: win.gridView && FilesCtrl.itemCount > 0
                    anchors.fill: parent
                    anchors.margins: 12
                    cellWidth: 116
                    cellHeight: 106
                    model: FilesCtrl
                    clip: true

                    delegate: Item {
                        width: GridView.view.cellWidth - 8
                        height: GridView.view.cellHeight - 8
                        readonly property bool isSel: FilesCtrl.selectedIndex === index

                        FinnSquircle {
                            anchors.fill: parent
                            radius: 12
                            color: isSel ? FinnTheme.withAlpha(FinnTheme.accent, 0.13)
                                         : (gMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                            borderColor: isSel ? FinnTheme.withAlpha(FinnTheme.accent, 0.45) : "transparent"
                            borderWidth: isSel ? 1 : 0
                        }

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            spacing: 7

                            FinnSquircle {
                                width: 46
                                height: 46
                                radius: 12
                                anchors.horizontalCenter: parent.horizontalCenter
                                color: isDir ? FinnTheme.withAlpha("#007BFF", 0.12) : FinnTheme.surfaceCanvas
                                borderWidth: 0

                                FinnIcon {
                                    anchors.centerIn: parent
                                    name: iconName
                                    size: 24
                                    tint: isDir ? "#007BFF" : FinnTheme.ink
                                }
                            }

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: name
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: isSel ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: gMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: FilesCtrl.selectedIndex = index
                            onDoubleClicked: FilesCtrl.activateIndex(index)
                        }
                    }
                }

                FinnEmptyState {
                    visible: FilesCtrl.itemCount === 0
                    anchors.fill: parent
                    iconName: "folder"
                    headline: "Folder is empty"
                    description: "No files match your current view."
                }
            }

            // Desktop File Inspector Pane
            FinnCard {
                visible: win.isDesktop && !win.gridView
                width: 212
                height: parent.height
                radius: FinnTheme.radiusLg

                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    visible: FilesCtrl.selectedIndex >= 0

                    FinnSquircle {
                        width: 64
                        height: 64
                        radius: 16
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: FilesCtrl.selectedItem.isDir
                            ? FinnTheme.withAlpha("#007BFF", 0.12)
                            : FinnTheme.surfaceCanvas
                        borderColor: FinnTheme.hairSoft
                        borderWidth: 0.6

                        FinnIcon {
                            anchors.centerIn: parent
                            name: FilesCtrl.selectedItem.iconName || "file-text"
                            size: 30
                            tint: FilesCtrl.selectedItem.isDir ? "#007BFF" : FinnTheme.ink
                        }
                    }

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: FilesCtrl.selectedItem.name || ""
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    FinnDivider { width: parent.width }

                    Column {
                        width: parent.width
                        spacing: 10

                        Column {
                            width: parent.width
                            spacing: 2
                            Text { text: "Kind"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium }
                            Text { text: FilesCtrl.selectedItem.isDir ? "Folder" : "File"; color: FinnTheme.ink; font.family: FinnTheme.fontFamily; font.pixelSize: 12 }
                        }

                        Column {
                            width: parent.width
                            spacing: 2
                            Text { text: "Size"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium }
                            Text { text: FilesCtrl.selectedItem.sizeText || "—"; color: FinnTheme.ink; font.family: FinnTheme.fontFamily; font.pixelSize: 12 }
                        }

                        Column {
                            width: parent.width
                            spacing: 2
                            Text { text: "Modified"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium }
                            Text { text: FilesCtrl.selectedItem.modifiedText || "—"; color: FinnTheme.ink; font.family: FinnTheme.fontFamily; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
                        }

                        Column {
                            width: parent.width
                            spacing: 2
                            Text { text: "Permissions"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium }
                            Text { text: FilesCtrl.selectedItem.permissions || "—"; color: FinnTheme.dim; font.family: FinnTheme.fontMono; font.pixelSize: 11 }
                        }
                    }

                    Item { width: 1; height: 4 }

                    FinnButton {
                        width: parent.width
                        text: FilesCtrl.selectedItem.isDir ? "Open Folder" : "Open File"
                        variant: "primary"
                        onClicked: FilesCtrl.activateIndex(FilesCtrl.selectedIndex)
                    }

                    Grid {
                        width: parent.width
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6
                        readonly property real bw: (width - 6) / 2

                        FinnButton { width: parent.bw; text: "Copy"; implicitHeight: 26; onClicked: FilesCtrl.copySelected() }
                        FinnButton { width: parent.bw; text: "Cut"; implicitHeight: 26; onClicked: FilesCtrl.cutSelected() }
                        FinnButton {
                            width: parent.bw
                            text: "Rename"
                            implicitHeight: 26
                            onClicked: {
                                win.inlinePromptMode = "rename"
                                win.inlinePromptText = FilesCtrl.selectedItem.name || ""
                            }
                        }
                        FinnButton { width: parent.bw; text: "Delete"; variant: "danger"; implicitHeight: 26; onClicked: FilesCtrl.deleteSelected() }
                    }
                }

                FinnEmptyState {
                    visible: FilesCtrl.selectedIndex < 0
                    anchors.fill: parent
                    iconName: "info"
                    headline: "No selection"
                    description: "Select a file to inspect details."
                }
            }
        }
    }
}
