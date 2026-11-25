import QtQuick
import FinnUI 1.0

// Finn Notes — Kate-grade native FinnOS text & Markdown editor:
// frosted notes sidebar, line numbers gutter, Find & Replace bar,
// Monospace Code / Prose toggle, live Markdown preview, and instant local save.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 940
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 590
    appTitle: "Finn Notes"
    pageTitle: NotesCtrl.activeTitle.length > 0 ? (NotesCtrl.activeTitle + (NotesCtrl.dirty ? " •" : "")) : "Untitled Note"
    navItems: NotesCtrl.notesList
    activeNavId: NotesCtrl.activeId
    mobileDetailOpen: true
    property bool previewMode: false
    property bool codeMode: false
    property bool showFindBar: false
    property string findQuery: ""
    property string replaceQuery: ""

    onNavSelected: (id) => NotesCtrl.openNote(id)

    Connections {
        target: NotesCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    function matchCount() {
        if (findQuery.length === 0) return 0
        const src = NotesCtrl.activeContent.toLowerCase()
        const q = findQuery.toLowerCase()
        let count = 0, pos = 0
        while ((pos = src.indexOf(q, pos)) !== -1) {
            count++
            pos += q.length
        }
        return count
    }

    function replaceAllMatches() {
        if (findQuery.length === 0) return
        const parts = NotesCtrl.activeContent.split(findQuery)
        if (parts.length <= 1) return
        NotesCtrl.activeContent = parts.join(replaceQuery)
        win.showToast("Replaced " + (parts.length - 1) + " occurrence(s)", "success")
    }

    headerActions: [
        FinnSquircle {
            width: 118
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
                        { label: "Edit", isPrev: false },
                        { label: "Preview", isPrev: true }
                    ]
                    delegate: FinnSquircle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: 4
                        color: win.previewMode === modelData.isPrev ? FinnTheme.navSel : "transparent"
                        borderWidth: 0
                        specularRim: false

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: win.previewMode === modelData.isPrev ? FinnTheme.ink : FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            font.weight: win.previewMode === modelData.isPrev ? Font.Medium : Font.Normal
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.previewMode = modelData.isPrev
                        }
                    }
                }
            }
        },
        FinnButton {
            text: win.codeMode ? "Prose" : "Code"
            implicitHeight: 28
            onClicked: win.codeMode = !win.codeMode
        },
        FinnButton {
            text: "Find"
            implicitHeight: 28
            variant: win.showFindBar ? "primary" : "secondary"
            onClicked: win.showFindBar = !win.showFindBar
        },
        FinnButton {
            text: "New"
            iconName: "plus"
            implicitHeight: 28
            onClicked: NotesCtrl.createNote()
        },
        FinnButton {
            text: "Save"
            iconName: "save"
            variant: NotesCtrl.dirty ? "primary" : "secondary"
            implicitHeight: 28
            onClicked: NotesCtrl.saveActive()
        }
    ]

    Column {
        anchors.fill: parent
        spacing: 8

        // Kate-style Find & Replace Strip
        FinnSquircle {
            visible: win.showFindBar
            width: parent.width
            height: 38
            radius: 10
            color: FinnTheme.surfaceCanvas
            borderColor: FinnTheme.hairSoft
            borderWidth: 0.7

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                TextInput {
                    id: findInput
                    width: 160
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    text: win.findQuery
                    onTextChanged: win.findQuery = text

                    Text {
                        visible: findInput.text.length === 0
                        text: "Find text..."
                        color: FinnTheme.mute
                        font: findInput.font
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Rectangle { width: 1; height: 18; color: FinnTheme.hairSoft; anchors.verticalCenter: parent.verticalCenter }

                TextInput {
                    id: replaceInput
                    width: 150
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 12
                    text: win.replaceQuery
                    onTextChanged: win.replaceQuery = text

                    Text {
                        visible: replaceInput.text.length === 0
                        text: "Replace with..."
                        color: FinnTheme.mute
                        font: replaceInput.font
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                FinnBadge {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.findQuery.length > 0 ? (win.matchCount() + " match(es)") : "0 matches"
                    tone: win.matchCount() > 0 ? "accent" : "neutral"
                }

                FinnButton {
                    text: "Replace All"
                    implicitHeight: 24
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: win.matchCount() > 0
                    onClicked: win.replaceAllMatches()
                }
            }
        }

        // Clean Editor / Markdown Canvas with subtle Line Numbers Gutter
        FinnCard {
            width: parent.width
            height: parent.height - 30 - (win.showFindBar ? 46 : 0)
            radius: FinnTheme.radiusSm

            Flickable {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 20
                anchors.topMargin: 14
                anchors.bottomMargin: 14
                contentWidth: width
                contentHeight: Math.max(height, editorRow.implicitHeight + 32)
                clip: true

                Row {
                    id: editorRow
                    width: parent.width
                    spacing: 12

                    // Kate-style Line Numbers Gutter
                    Column {
                        visible: !win.previewMode
                        width: 32
                        spacing: 0

                        Repeater {
                            model: Math.max(1, NotesCtrl.lineCount)
                            delegate: Text {
                                width: 32
                                horizontalAlignment: Text.AlignRight
                                text: (index + 1)
                                color: FinnTheme.mute
                                font.family: FinnTheme.fontMono
                                font.pixelSize: 12
                                height: 19
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Rectangle {
                        visible: !win.previewMode
                        width: 1
                        height: Math.max(240, editorItem.implicitHeight)
                        color: FinnTheme.hairSoft
                    }

                    Item {
                        id: editorItem
                        width: win.previewMode ? parent.width : Math.max(120, parent.width - 57)
                        implicitHeight: win.previewMode ? previewTxt.implicitHeight : Math.max(240, noteEdit.implicitHeight)

                        TextEdit {
                            id: noteEdit
                            visible: !win.previewMode
                            width: parent.width
                            text: NotesCtrl.activeContent
                            color: FinnTheme.ink
                            selectionColor: FinnTheme.withAlpha("#F59E0B", 0.24)
                            font.family: win.codeMode ? FinnTheme.fontMono : FinnTheme.fontFamily
                            font.pixelSize: win.codeMode ? 13 : 14
                            wrapMode: TextEdit.Wrap
                            onTextChanged: {
                                if (text !== NotesCtrl.activeContent) {
                                    NotesCtrl.activeContent = text
                                }
                            }
                        }

                        Text {
                            id: previewTxt
                            visible: win.previewMode
                            width: parent.width
                            textFormat: Text.MarkdownText
                            text: NotesCtrl.activeContent
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 14
                            lineHeight: 1.45
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        // Minimal Understated Status Strip
        Item {
            width: parent.width
            height: 22

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: NotesCtrl.wordCount + " words · " + NotesCtrl.lineCount + " lines · " +
                      (win.codeMode ? "Monospace" : "Prose") + " · " +
                      (NotesCtrl.dirty ? "Unsaved" : "Saved")
                color: FinnTheme.mute
                font.family: FinnTheme.fontFamily
                font.pixelSize: 11
            }

            FinnButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "Delete"
                variant: "ghost"
                implicitHeight: 22
                onClicked: NotesCtrl.deleteActive()
            }
        }
    }
}
