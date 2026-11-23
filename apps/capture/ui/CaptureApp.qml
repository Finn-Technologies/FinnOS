import QtQuick
import FinnUI 1.0

// Finn Capture — Spectacle-grade native screenshot tool with delay timer,
// live preview stage, clipboard copy, and instant save to ~/Pictures/Screenshots.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 880
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 560
    appTitle: "Finn Capture"
    pageTitle: "Screenshot"
    activeNavId: "screen"
    showSidebarSearch: false
    property int delaySecs: 0

    navItems: [
        { id: "screen", label: "Full Screen", icon: "desktop", badgeColor: "#EC4899", section: "capture" }
    ]

    Connections {
        target: CaptureCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    headerActions: [
        FinnButton {
            text: "Delay: " + win.delaySecs + "s"
            implicitHeight: 28
            onClicked: win.delaySecs = (win.delaySecs === 0) ? 3 : (win.delaySecs === 3 ? 5 : 0)
        },
        FinnButton {
            text: CaptureCtrl.capturing ? "Capturing..." : "Take Screenshot"
            variant: "primary"
            enabled: !CaptureCtrl.capturing
            implicitHeight: 28
            onClicked: CaptureCtrl.captureNow(win.delaySecs)
        },
        FinnButton {
            text: "Copy"
            enabled: CaptureCtrl.lastImagePath.length > 0
            implicitHeight: 28
            onClicked: CaptureCtrl.copyToClipboard()
        }
    ]

    Column {
        anchors.fill: parent
        spacing: 10

        FinnSquircle {
            width: parent.width
            height: parent.height - 34
            radius: FinnTheme.radiusLg
            color: FinnTheme.dark ? "#13161E" : "#F3F5F9"
            borderColor: FinnTheme.hairSoft
            borderWidth: 0.7
            clip: true

            Image {
                visible: CaptureCtrl.lastImageUrl.length > 0
                anchors.fill: parent
                anchors.margins: 16
                source: CaptureCtrl.lastImageUrl
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            FinnEmptyState {
                visible: CaptureCtrl.lastImageUrl.length === 0
                anchors.fill: parent
                iconName: "desktop"
                headline: "Ready to Capture"
                description: "Click 'Take Screenshot' to save a full-resolution PNG to ~/Pictures/Screenshots."
            }
        }

        Row {
            width: parent.width
            height: 24
            spacing: 8

            FinnBadge {
                text: CaptureCtrl.lastImagePath.length > 0 ? CaptureCtrl.lastImagePath : "Output: ~/Pictures/Screenshots"
                tone: CaptureCtrl.lastImagePath.length > 0 ? "success" : "neutral"
            }
        }
    }
}
