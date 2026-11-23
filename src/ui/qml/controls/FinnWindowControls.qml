import QtQuick
import FinnUI 1.0

// Exact Figma Settings App / Window Options (20:106: 88x28, r=6, 0.5px hair_soft)
Item {
    id: root
    property bool maximized: false

    signal maximizeRequested()
    signal minimizeRequested()
    signal closeRequested()

    implicitWidth: 88
    implicitHeight: 28
    width: 88
    height: 28

    // Exact Figma Settings Window Options (71:109: 88x28 at x=456, y=6, r=6, fill #FFFFFF, stroke #0000000D)
    FinnSquircle {
        anchors.fill: parent
        radius: 6
        color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
        borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
        borderWidth: 0.5
        specularRim: false
    }

    Row {
        anchors.centerIn: parent
        spacing: 10

        FinnIconButton {
            iconName: "expand"
            buttonSize: 20
            iconSize: 14
            radius: FinnTheme.radiusXs
            onClicked: root.maximizeRequested()
        }

        FinnIconButton {
            iconName: "minimise"
            buttonSize: 20
            iconSize: 14
            radius: FinnTheme.radiusXs
            onClicked: root.minimizeRequested()
        }

        FinnIconButton {
            iconName: "x"
            buttonSize: 20
            iconSize: 13
            radius: FinnTheme.radiusXs
            dangerHover: true
            onClicked: root.closeRequested()
        }
    }
}
