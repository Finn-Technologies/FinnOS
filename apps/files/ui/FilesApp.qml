import QtQuick
import FinnUI 1.0

// Finn Files — Settings-consistent FinnUI file manager with flat-edge Liquid Glass
// sidebar, WindowControls-aligned top toolbar (y=6, h=28, r=6), Hugeicons filled
// folders (empty vs non-empty), custom folder colors & emblems via Properties.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 980
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 620
    appTitle: "Finn Files"
    pageTitle: ""
    activeNavId: "home"
    mobileDetailOpen: true
    fullBleedContent: true
    showSidebarSearch: true

    property bool gridView: true
    property bool showInspector: false
    property string inlinePromptMode: "" // "", "mkdir", "rename"
    property string inlinePromptText: ""

    readonly property var folderColors: [
        "#007BFF", "#6366F1", "#8B5CF6", "#EC4899",
        "#F43F5E", "#F59E0B", "#10B981", "#64748B"
    ]

    readonly property var folderEmblems: [
        "none", "home-09", "desktop", "file-text", "download",
        "image", "volume-02", "code-01", "briefcase-01", "lock",
        "globe", "layers", "cog", "compass", "package"
    ]

    // Settings-consistent 26px circular colored badge icons + Hugeicons + 14px Regular labels
    navItems: [
        { id: "home",      label: "Home",       icon: "home-09",    badgeColor: "#007BFF", section: "favorites" },
        { id: "desktop",   label: "Desktop",    icon: "desktop",    badgeColor: "#007BFF", section: "favorites" },
        { id: "documents", label: "Documents",  icon: "file-text",  badgeColor: "#007BFF", section: "favorites" },
        { id: "downloads", label: "Downloads",  icon: "download",   badgeColor: "#10B981", section: "favorites" },
        { id: "pictures",  label: "Pictures",   icon: "image",      badgeColor: "#EC4899", section: "favorites" },
        { id: "music",     label: "Music",      icon: "volume-02",  badgeColor: "#F59E0B", section: "favorites" },
        { id: "root",      label: "Filesystem", icon: "hard-drive", badgeColor: "#64748B", section: "locations" }
    ]

    onNavSelected: (id) => FilesCtrl.navigatePlace(id)
    onSidebarSearchTextChanged: FilesCtrl.filterText = sidebarSearchText

    Connections {
        target: FilesCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- Top Toolbar strictly aligned with FinnWindowControls (y=6, h=28, r=6) ----
        Item {
            id: toolbar
            width: parent.width
            height: 44

            // Left: Back/Forward control (y=6, h=28, r=6) + Folder Name (16px Regular, matching Settings)
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                y: 6
                height: 28
                spacing: 10

                FinnSquircle {
                    width: 58
                    height: 28
                    radius: 6
                    color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
                    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                    borderWidth: 0.5
                    specularRim: false

                    Row {
                        anchors.fill: parent
                        spacing: 0

                        Item {
                            width: 28
                            height: parent.height
                            opacity: FilesCtrl.canGoBack ? 1.0 : 0.35

                            FinnIcon {
                                anchors.centerIn: parent
                                name: "chevron-left"
                                size: 14
                                tint: FinnTheme.ink
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: FilesCtrl.canGoBack
                                cursorShape: Qt.PointingHandCursor
                                onClicked: FilesCtrl.goBack()
                            }
                        }

                        Rectangle {
                            width: 0.5
                            height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#12000000"
                        }

                        Item {
                            width: 28
                            height: parent.height
                            opacity: FilesCtrl.canGoForward ? 1.0 : 0.35

                            FinnIcon {
                                anchors.centerIn: parent
                                name: "chevron-right"
                                size: 14
                                tint: FinnTheme.ink
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: FilesCtrl.canGoForward
                                cursorShape: Qt.PointingHandCursor
                                onClicked: FilesCtrl.goForward()
                            }
                        }
                    }
                }

                Text {
                    text: FilesCtrl.currentFolderName
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 200)
                }
            }

            // Right: View switcher + Actions aligned at y=6, h=28, r=6 directly left of WindowControls
            Row {
                anchors.right: parent.right
                anchors.rightMargin: (win.isDesktop || win.isTablet) ? 100 : 12
                y: 6
                height: 28
                spacing: 6

                // Grid / List segmented pill (h=28, r=6)
                FinnSquircle {
                    width: 60
                    height: 28
                    radius: 6
                    color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
                    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                    borderWidth: 0.5
                    specularRim: false

                    Row {
                        anchors.fill: parent
                        anchors.margins: 2
                        spacing: 2

                        Repeater {
                            model: [
                                { icon: "grid", isGrid: true },
                                { icon: "list", isGrid: false }
                            ]
                            delegate: FinnSquircle {
                                width: 27
                                height: 24
                                radius: 4
                                color: win.gridView === modelData.isGrid
                                    ? FinnTheme.navSel
                                    : "transparent"
                                borderWidth: 0
                                specularRim: false

                                FinnIcon {
                                    anchors.centerIn: parent
                                    name: modelData.icon
                                    size: 14
                                    tint: win.gridView === modelData.isGrid ? FinnTheme.ink : FinnTheme.dim
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.gridView = modelData.isGrid
                                }
                            }
                        }
                    }
                }

                // Actions pill (h=28, r=6): New Folder, Sort, Hidden, Properties, Terminal
                FinnSquircle {
                    width: 144
                    height: 28
                    radius: 6
                    color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
                    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                    borderWidth: 0.5
                    specularRim: false

                    Row {
                        anchors.centerIn: parent
                        spacing: 4

                        FinnIconButton {
                            iconName: "folder-plus"
                            buttonSize: 24
                            iconSize: 14
                            radius: 4
                            onClicked: {
                                win.inlinePromptMode = "mkdir"
                                win.inlinePromptText = ""
                                promptField.forceActiveFocus()
                            }
                        }

                        FinnIconButton {
                            iconName: "sliders"
                            buttonSize: 24
                            iconSize: 14
                            radius: 4
                            onClicked: {
                                FilesCtrl.sortBy = (FilesCtrl.sortBy === "name") ? "date"
                                                 : (FilesCtrl.sortBy === "date") ? "size" : "name"
                                win.showToast("Sorted by " + FilesCtrl.sortBy, "info")
                            }
                        }

                        FinnIconButton {
                            iconName: "eye"
                            buttonSize: 24
                            iconSize: 14
                            radius: 4
                            iconColor: FilesCtrl.showHidden ? FinnTheme.accent : FinnTheme.ink
                            onClicked: FilesCtrl.showHidden = !FilesCtrl.showHidden
                        }

                        FinnIconButton {
                            iconName: "info"
                            buttonSize: 24
                            iconSize: 14
                            radius: 4
                            iconColor: win.showInspector ? FinnTheme.accent : FinnTheme.ink
                            onClicked: win.showInspector = !win.showInspector
                        }

                        FinnIconButton {
                            iconName: "terminal-square"
                            buttonSize: 24
                            iconSize: 14
                            radius: 4
                            onClicked: FilesCtrl.openInTerminal()
                        }
                    }
                }
            }
        }

        // ---- Inline New Folder / Rename Bar ----
        Item {
            visible: win.inlinePromptMode.length > 0
            width: parent.width
            height: visible ? 40 : 0

            FinnSquircle {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.bottomMargin: 6
                radius: 6
                color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
                borderColor: FinnTheme.accent
                borderWidth: 1
                specularRim: false

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        text: win.inlinePromptMode === "mkdir" ? "New folder" : "Rename"
                        color: FinnTheme.dim
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextInput {
                        id: promptField
                        width: Math.max(120, parent.width - 210)
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
                        variant: "ghost"
                        implicitHeight: 24
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: win.inlinePromptMode = ""
                    }
                }
            }
        }

        // ---- Main Spacious File Canvas + Collapsible Properties Drawer ----
        Row {
            width: parent.width
            height: parent.height - toolbar.height - (win.inlinePromptMode.length > 0 ? 40 : 0) - pathBar.height
            spacing: 0

            Item {
                width: (win.isDesktop && win.showInspector) ? parent.width - 248 : parent.width
                height: parent.height

                // Spacious Grid View with Filled Hugeicons Folders (empty vs non-empty + custom color & emblem)
                GridView {
                    visible: win.gridView && FilesCtrl.itemCount > 0
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 10
                    anchors.bottomMargin: 8
                    cellWidth: 124
                    cellHeight: 112
                    model: FilesCtrl
                    clip: true

                    delegate: Item {
                        width: GridView.view.cellWidth - 6
                        height: GridView.view.cellHeight - 6
                        readonly property bool isSel: FilesCtrl.selectedIndex === index

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 4
                            spacing: 5

                            Item {
                                width: 66
                                height: 60
                                anchors.horizontalCenter: parent.horizontalCenter

                                // Subtle selection/hover backdrop behind icon
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 64
                                    height: 56
                                    radius: 8
                                    color: isSel
                                        ? FinnTheme.withAlpha(FinnTheme.accent, 0.12)
                                        : (gMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                                }

                                // Actual Hugeicons Filled Folder (no outline, no box!)
                                Item {
                                    visible: isDir
                                    anchors.centerIn: parent
                                    width: 58
                                    height: 58

                                    FinnIcon {
                                        anchors.centerIn: parent
                                        name: hasItems ? "folder-filled-items" : "folder-filled"
                                        size: 56
                                        tint: folderColor && folderColor.length > 0 ? folderColor : FinnTheme.accent
                                    }

                                    // Optional Hugeicons emblem inside the folder body
                                    FinnIcon {
                                        visible: folderEmblem && folderEmblem.length > 0
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.verticalCenterOffset: 5
                                        name: folderEmblem || ""
                                        size: 18
                                        tint: "#FFFFFF"
                                        opacity: 0.88
                                    }
                                }

                                // File Document Sheet (Hugeicons glyph)
                                FinnSquircle {
                                    visible: !isDir
                                    anchors.centerIn: parent
                                    width: 44
                                    height: 50
                                    radius: 6
                                    color: FinnTheme.dark ? "#1B202C" : "#FAFAFC"
                                    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#14000000"
                                    borderWidth: 0.5
                                    specularRim: false

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 3

                                        FinnIcon {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            name: iconName
                                            size: 20
                                            tint: FinnTheme.dim
                                        }

                                        Text {
                                            visible: extension.length > 0 && extension.length <= 5
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: extension
                                            color: FinnTheme.mute
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 8
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                            }

                            // Filename with selection pill (12px Regular matching Settings)
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(parent.width, nameTxt.implicitWidth + 12)
                                height: nameTxt.implicitHeight + 4
                                radius: 5
                                color: isSel ? FinnTheme.accent : "transparent"

                                Text {
                                    id: nameTxt
                                    anchors.centerIn: parent
                                    width: Math.min(implicitWidth, parent.parent.width - 10)
                                    horizontalAlignment: Text.AlignHCenter
                                    text: name
                                    color: isSel ? "#FFFFFF" : FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.Normal
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideMiddle
                                }
                            }
                        }

                        MouseArea {
                            id: gMa
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => {
                                FilesCtrl.selectedIndex = index
                                if (mouse.button === Qt.RightButton) {
                                    win.showInspector = true
                                }
                            }
                            onDoubleClicked: FilesCtrl.activateIndex(index)
                        }
                    }
                }

                // Clean Borderless List View
                Column {
                    visible: !win.gridView && FilesCtrl.itemCount > 0
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 0

                    Item {
                        width: parent.width
                        height: 28

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Text {
                                text: "Name"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                width: Math.max(80, parent.width - 216)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Modified"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                width: 120
                                horizontalAlignment: Text.AlignRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Size"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
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
                        spacing: 1

                        delegate: Item {
                            width: ListView.view.width
                            height: 34
                            readonly property bool isSel: FilesCtrl.selectedIndex === index

                            Rectangle {
                                anchors.fill: parent
                                radius: 5
                                color: isSel ? FinnTheme.accent
                                             : (rowMa.containsMouse ? FinnTheme.surfaceHover : "transparent")
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10

                                FinnIcon {
                                    name: isDir ? (hasItems ? "folder-filled-items" : "folder-filled") : iconName
                                    size: 18
                                    tint: isSel ? "#FFFFFF" : (isDir ? (folderColor || FinnTheme.accent) : FinnTheme.dim)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: name
                                    color: isSel ? "#FFFFFF" : FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.Normal
                                    elide: Text.ElideRight
                                    width: Math.max(80, parent.width - 234)
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: modifiedText
                                    color: isSel ? FinnTheme.withAlpha("#FFFFFF", 0.85) : FinnTheme.mute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    width: 120
                                    horizontalAlignment: Text.AlignRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: sizeText
                                    color: isSel ? FinnTheme.withAlpha("#FFFFFF", 0.85) : FinnTheme.dim
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                    width: 66
                                    horizontalAlignment: Text.AlignRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: rowMa
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    FilesCtrl.selectedIndex = index
                                    if (mouse.button === Qt.RightButton) {
                                        win.showInspector = true
                                    }
                                }
                                onDoubleClicked: FilesCtrl.activateIndex(index)
                            }
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

            // ---- Collapsible Properties / Inspector Panel (with Folder Color & Icon Customization) ----
            Item {
                visible: win.isDesktop && win.showInspector
                width: visible ? 248 : 0
                height: parent.height

                FinnDivider {
                    vertical: true
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 16
                    contentWidth: width
                    contentHeight: propsCol.implicitHeight + 12
                    clip: true
                    visible: FilesCtrl.selectedIndex >= 0

                    Column {
                        id: propsCol
                        width: parent.width
                        spacing: 12

                        // Live Icon / Filled Folder Preview
                        Item {
                            width: 68
                            height: 64
                            anchors.horizontalCenter: parent.horizontalCenter

                            Item {
                                visible: !!FilesCtrl.selectedItem.isDir
                                anchors.centerIn: parent
                                width: 64
                                height: 64

                                FinnIcon {
                                    anchors.centerIn: parent
                                    name: FilesCtrl.selectedItem.hasItems ? "folder-filled-items" : "folder-filled"
                                    size: 62
                                    tint: FilesCtrl.selectedItem.folderColor || FinnTheme.accent
                                }

                                FinnIcon {
                                    visible: (FilesCtrl.selectedItem.folderEmblem || "").length > 0
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.verticalCenterOffset: 5
                                    name: FilesCtrl.selectedItem.folderEmblem || ""
                                    size: 20
                                    tint: "#FFFFFF"
                                    opacity: 0.90
                                }
                            }

                            FinnSquircle {
                                visible: !FilesCtrl.selectedItem.isDir
                                anchors.centerIn: parent
                                width: 52
                                height: 58
                                radius: 6
                                color: FinnTheme.surfaceCanvas
                                borderColor: FinnTheme.hairSoft
                                borderWidth: 0.5
                                specularRim: false

                                FinnIcon {
                                    anchors.centerIn: parent
                                    name: FilesCtrl.selectedItem.iconName || "file-text"
                                    size: 26
                                    tint: FinnTheme.ink
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: FilesCtrl.selectedItem.name || ""
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.Normal
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        FinnDivider { width: parent.width }

                        // Folder Appearance Customization (Color + Emblem Icon)
                        Column {
                            visible: !!FilesCtrl.selectedItem.isDir
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "Folder color"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                            }

                            Flow {
                                width: parent.width
                                spacing: 8

                                Repeater {
                                    model: win.folderColors
                                    delegate: Item {
                                        width: 22
                                        height: 22
                                        readonly property bool active: (FilesCtrl.selectedItem.folderColor || "#007BFF").toLowerCase() === modelData.toLowerCase()

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: active ? 22 : 18
                                            height: active ? 22 : 18
                                            radius: width / 2
                                            color: modelData
                                            border.width: active ? 2 : 0
                                            border.color: FinnTheme.ink
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: FilesCtrl.setFolderStyle(
                                                FilesCtrl.selectedItem.path,
                                                modelData,
                                                FilesCtrl.selectedItem.folderEmblem || "none"
                                            )
                                        }
                                    }
                                }
                            }

                            Text {
                                text: "Folder icon"
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                            }

                            Grid {
                                width: parent.width
                                columns: 5
                                columnSpacing: 6
                                rowSpacing: 6

                                Repeater {
                                    model: win.folderEmblems
                                    delegate: FinnSquircle {
                                        width: 36
                                        height: 30
                                        radius: 6
                                        readonly property string curEmblem: FilesCtrl.selectedItem.folderEmblem || ""
                                        readonly property bool active: (modelData === "none" && curEmblem.length === 0) || (curEmblem === modelData)
                                        color: active ? FinnTheme.withAlpha(FinnTheme.accent, 0.16) : FinnTheme.surfaceCanvas
                                        borderColor: active ? FinnTheme.accent : FinnTheme.hairSoft
                                        borderWidth: active ? 1 : 0.5
                                        specularRim: false

                                        Text {
                                            visible: modelData === "none"
                                            anchors.centerIn: parent
                                            text: "—"
                                            color: active ? FinnTheme.accent : FinnTheme.dim
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 12
                                        }

                                        FinnIcon {
                                            visible: modelData !== "none"
                                            anchors.centerIn: parent
                                            name: modelData === "none" ? "" : modelData
                                            size: 15
                                            tint: active ? FinnTheme.accent : FinnTheme.ink
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: FilesCtrl.setFolderStyle(
                                                FilesCtrl.selectedItem.path,
                                                FilesCtrl.selectedItem.folderColor || "#007BFF",
                                                modelData
                                            )
                                        }
                                    }
                                }
                            }

                            FinnDivider { width: parent.width }
                        }

                        // Metadata
                        Column {
                            width: parent.width
                            spacing: 8

                            Row {
                                width: parent.width
                                Text { text: "Kind"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 12; width: 72 }
                                Text {
                                    text: FilesCtrl.selectedItem.isDir
                                        ? (FilesCtrl.selectedItem.hasItems ? "Folder (items)" : "Folder (empty)")
                                        : "Document"
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 12
                                }
                            }

                            Row {
                                width: parent.width
                                Text { text: "Size"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 12; width: 72 }
                                Text { text: FilesCtrl.selectedItem.sizeText || "—"; color: FinnTheme.ink; font.family: FinnTheme.fontFamily; font.pixelSize: 12 }
                            }

                            Row {
                                width: parent.width
                                Text { text: "Modified"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 12; width: 72 }
                                Text { text: FilesCtrl.selectedItem.modifiedText || "—"; color: FinnTheme.ink; font.family: FinnTheme.fontFamily; font.pixelSize: 12; width: parent.width - 72; elide: Text.ElideRight }
                            }

                            Row {
                                width: parent.width
                                Text { text: "Access"; color: FinnTheme.mute; font.family: FinnTheme.fontFamily; font.pixelSize: 12; width: 72 }
                                Text { text: FilesCtrl.selectedItem.permissions || "—"; color: FinnTheme.dim; font.family: FinnTheme.fontMono; font.pixelSize: 11 }
                            }
                        }

                        FinnButton {
                            width: parent.width
                            text: FilesCtrl.selectedItem.isDir ? "Open Folder" : "Open"
                            variant: "primary"
                            implicitHeight: 28
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
                                    promptField.forceActiveFocus()
                                }
                            }
                            FinnButton { width: parent.bw; text: "Delete"; variant: "danger"; implicitHeight: 26; onClicked: FilesCtrl.deleteSelected() }
                        }
                    }
                }

                FinnEmptyState {
                    visible: FilesCtrl.selectedIndex < 0
                    anchors.fill: parent
                    iconName: "info"
                    headline: "No selection"
                    description: "Select an item to view properties."
                }
            }
        }

        // ---- Bottom Path Bar ----
        Item {
            id: pathBar
            width: parent.width
            height: 28

            FinnDivider {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: rightPathRow.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                clip: true

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
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: modelData.label
                            color: index === FilesCtrl.breadcrumbs.length - 1 ? FinnTheme.ink : FinnTheme.mute
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Normal
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

            Row {
                id: rightPathRow
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Text {
                    visible: FilesCtrl.hasClipboard
                    text: FilesCtrl.clipboardIsCut ? "Move Here" : "Paste Here"
                    color: FinnTheme.accent
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: FilesCtrl.pasteHere()
                    }
                }

                Text {
                    text: FilesCtrl.itemCount + (FilesCtrl.itemCount === 1 ? " item" : " items")
                    color: FinnTheme.mute
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
