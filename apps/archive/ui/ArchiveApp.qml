import QtQuick
import FinnUI 1.0

// Finn Archive — Ark-grade native archive inspector, extractor, and creator.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 880
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 560
    appTitle: "Finn Archive"
    pageTitle: activeNavId === "contents" ? ArchiveCtrl.archiveName : "Local Archives"
    activeNavId: "local"
    showSidebarSearch: false

    navItems: [
        { id: "local", label: "Local Archives", icon: "folder", badgeColor: "#D97706", section: "archive" },
        { id: "contents", label: "Archive Contents", icon: "layers", badgeColor: "#007BFF", section: "archive" }
    ]

    Connections {
        target: ArchiveCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    headerActions: [
        FinnButton {
            text: "Create Backup .tar.gz"
            iconName: "plus"
            implicitHeight: 28
            onClicked: {
                ArchiveCtrl.createArchiveFromHome("Documents-backup")
                win.activeNavId = "contents"
            }
        },
        FinnButton {
            text: "Extract"
            variant: "primary"
            enabled: ArchiveCtrl.archivePath.length > 0
            implicitHeight: 28
            onClicked: ArchiveCtrl.extractToDownloads()
        },
        FinnIconButton {
            iconName: "refresh"
            buttonSize: 28
            iconSize: 15
            onClicked: ArchiveCtrl.scanLocalArchives()
        }
    ]

    // Local Archives Browser
    FinnCard {
        visible: win.activeNavId === "local"
        anchors.fill: parent
        radius: FinnTheme.radiusLg

        ListView {
            visible: ArchiveCtrl.recentArchives.length > 0
            anchors.fill: parent
            anchors.margins: 10
            model: ArchiveCtrl.recentArchives
            spacing: 4
            clip: true

            delegate: FinnSquircle {
                width: ListView.view.width
                height: 44
                radius: 10
                color: aMa.containsMouse ? FinnTheme.surfaceHover : FinnTheme.surfaceCanvas
                borderWidth: 0

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    FinnIcon { name: "layers"; size: 18; tint: "#D97706"; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: modelData.name
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        width: Math.max(80, parent.width - 130)
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FinnBadge { text: modelData.sizeText; anchors.verticalCenter: parent.verticalCenter }
                }

                MouseArea {
                    id: aMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        ArchiveCtrl.openArchive(modelData.path)
                        win.activeNavId = "contents"
                    }
                }
            }
        }

        FinnEmptyState {
            visible: ArchiveCtrl.recentArchives.length === 0
            anchors.fill: parent
            iconName: "layers"
            headline: "No archives found"
            description: "Click 'Create Backup .tar.gz' or open any .zip / .tar.gz file."
        }
    }

    // Opened Archive Entries
    FinnCard {
        visible: win.activeNavId === "contents"
        anchors.fill: parent
        radius: FinnTheme.radiusLg

        ListView {
            visible: ArchiveCtrl.entries.length > 0
            anchors.fill: parent
            anchors.margins: 10
            model: ArchiveCtrl.entries
            spacing: 2
            clip: true

            delegate: Item {
                width: ListView.view.width
                height: 32
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    spacing: 8
                    FinnIcon {
                        name: modelData.isDir ? "folder" : "file-text"
                        size: 15
                        tint: modelData.isDir ? "#007BFF" : FinnTheme.dim
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.name
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontMono
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        width: parent.width - 36
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        FinnEmptyState {
            visible: ArchiveCtrl.entries.length === 0
            anchors.fill: parent
            iconName: "layers"
            headline: "No archive opened"
            description: "Select an archive from Local Archives to inspect its contents."
        }
    }
}
