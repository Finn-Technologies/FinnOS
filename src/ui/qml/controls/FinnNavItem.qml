import QtQuick
import FinnUI 1.0

// Exact Figma Settings Sidebar row (21:131: 226x36, r=5, 26px colored badge circle, 14px Regular)
Item {
    id: root
    property string label: ""
    property string iconName: ""
    property color badgeColor: FinnTheme.accent
    property color iconTint: "transparent"
    property bool useBadgeCircle: true
    property bool selected: false
    property bool compactRail: false
    property bool tvMode: false

    signal clicked()

    implicitWidth: compactRail ? 52 : 226
    implicitHeight: tvMode ? 52 : (compactRail ? 48 : 36)
    activeFocusOnTab: true

    FinnSquircle {
        anchors.fill: parent
        radius: FinnTheme.radiusNav
        borderColor: root.activeFocus ? FinnTheme.accent : "transparent"
        borderWidth: root.activeFocus ? (root.tvMode ? 3.0 : 1.5) : 0
        color: root.selected
            ? FinnTheme.navSel
            : (ma.containsPress ? FinnTheme.surfacePressed : (ma.containsMouse ? FinnTheme.surfaceHover : "transparent"))
    }

    // Left accent bar in compact tablet rail mode
    Rectangle {
        visible: root.compactRail && root.selected
        width: 3
        height: 20
        radius: 1.5
        color: FinnTheme.accent
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: root.compactRail ? (parent.width - 26) / 2 : 5
        anchors.right: parent.right
        anchors.rightMargin: root.compactRail ? 0 : 8
        spacing: 10

        Item {
            width: root.tvMode ? 34 : 26
            height: root.tvMode ? 34 : 26
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                visible: root.useBadgeCircle
                anchors.fill: parent
                radius: width / 2
                color: root.badgeColor
            }

            FinnIcon {
                anchors.centerIn: parent
                name: root.iconName
                size: root.useBadgeCircle ? (root.tvMode ? 20 : 16) : (root.tvMode ? 22 : 17)
                tint: root.useBadgeCircle
                    ? "#FFFFFF"
                    : (root.iconTint.a > 0 ? root.iconTint : (root.selected ? FinnTheme.accent : FinnTheme.dim))
            }
        }

        Text {
            visible: !root.compactRail
            text: root.label
            color: FinnTheme.ink
            font.family: FinnTheme.fontFamily
            font.pixelSize: root.tvMode ? 20 : 14
            font.weight: Font.Normal
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(20, parent.width - (root.tvMode ? 46 : 38))
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
}
