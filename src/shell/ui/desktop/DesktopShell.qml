import QtQuick
import FinnUI 1.0
import FinnShell 1.0

// Desktop Form-Factor Containment (Plasma-style + 1:1 Figma FinnOS - Abhi):
// - Centered 4-pill frosted glass bottom dock (713x44, y=height-59)
// - Home Menu (585x460), Spotlight Search (300x44 + 350x228),
//   Control Center (444x300), and Window Overview popovers.
Item {
    id: root
    anchors.fill: parent

    // Click outside open popover to dismiss cleanly
    MouseArea {
        anchors.fill: parent
        enabled: ShellCorona.activePanel.length > 0
        onClicked: ShellCorona.closePanels()
    }

    // Home Menu Popover (Figma 4:24 / 4:60: 585x460, 15px above dock)
    HomeMenuPopover {
        id: homeMenu
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: dock.top
        anchors.bottomMargin: 15
        shown: ShellCorona.activePanel === "home"
        z: 20
    }

    // Spotlight Search Popover (Figma 4:226 / 5:16 + 5:2 centered)
    SpotlightPopover {
        id: spotlight
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(96, Math.round(parent.height * 0.28))
        shown: ShellCorona.activePanel === "search"
        z: 20
    }

    // Control Center Popover (Figma 11:19 / 11:216: 444x300 aligned right of dock, 15px gap above dock)
    ControlCenterPopover {
        id: controlCenter
        x: Math.min(parent.width - width - 16, Math.max(16, dock.x + dock.width - width))
        anchors.bottom: dock.top
        anchors.bottomMargin: 15
        shown: ShellCorona.activePanel === "control"
        z: 20
    }

    // Open Windows Overview Popover
    OverviewPopover {
        id: overview
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: dock.top
        anchors.bottomMargin: 12
        shown: ShellCorona.activePanel === "windows"
        z: 20
    }

    // Exact 4-Pill Glass Dock (Figma 4:58: 713x44 at bottom-center, 15px margin)
    DesktopDock {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 15
        z: 30
    }
}
