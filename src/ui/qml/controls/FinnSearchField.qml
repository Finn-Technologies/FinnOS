import QtQuick
import FinnUI 1.0

// Exact Figma pill search bar:
// - Settings sidebar 21:132: 226x30, r=pill, glass fill + hair border
// - Spotlight 5:16: 300x44, r=pill, glass fill
Item {
    id: root
    property alias text: input.text
    property string placeholderText: "Search anything"
    property int fontSize: 12
    property bool glassStyle: true
    property bool backdropBlur: false
    property bool showClear: true

    signal accepted()
    signal textEdited(string text)

    implicitWidth: 226
    implicitHeight: 30

    function focusInput() {
        input.forceActiveFocus()
    }

    FinnSquircle {
        anchors.fill: parent
        radius: root.height / 2
        backdropBlur: root.backdropBlur
        specularRim: root.glassStyle
        color: root.glassStyle
            ? FinnTheme.withAlpha(FinnTheme.dark ? "#0E121B" : "#FFFFFF", FinnTheme.dark ? 0.48 : 0.38)
            : FinnTheme.surfaceInput
        borderColor: input.activeFocus && input.text.length > 0
            ? FinnTheme.accent
            : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : FinnTheme.withAlpha("#000000", 0.06))
        borderWidth: input.activeFocus && input.text.length > 0 ? 1.2 : 0.5
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Math.round(root.height * 0.28)
        anchors.rightMargin: Math.round(root.height * 0.25)
        spacing: 8

        FinnIcon {
            name: "search-01"
            size: Math.min(22, Math.round(root.height * 0.56))
            tint: FinnTheme.ink
            anchors.verticalCenter: parent.verticalCenter
        }

        Item {
            width: Math.max(20, parent.width - (root.height * 0.56) - (clearBtn.visible ? 28 : 8))
            height: parent.height

            Text {
                visible: input.text.length === 0
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                text: root.placeholderText
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.fontSize
                elide: Text.ElideRight
            }

            TextInput {
                id: input
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                color: FinnTheme.ink
                selectionColor: FinnTheme.accent
                selectedTextColor: "#FFFFFF"
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.fontSize
                cursorVisible: input.text.length > 0
                clip: true
                onAccepted: root.accepted()
                onTextEdited: root.textEdited(text)
            }
        }

        FinnIconButton {
            id: clearBtn
            visible: root.showClear && input.text.length > 0
            iconName: "x"
            buttonSize: 20
            iconSize: 12
            radius: 10
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                input.text = ""
                root.textEdited("")
            }
        }
    }
}
