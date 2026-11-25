import QtQuick
import QtQuick.Window
import FinnUI 1.0

// GPU-accelerated Qt Quick Scene Graph for finn-comp.
// Maintains the strict 8-layer Plasma/FinnOS stacking order:
//   0: Background
//   1000: LayerBackground (wallpaper)
//   2000: LayerBottom
//   3000: Toplevels (spring-animated XDG & Xwayland surfaces + optional FinnOS SSD)
//   4000: LayerTop (dock / status bars)
//   5000: Fullscreen
//   6000: LayerOverlay (popovers / spotlight / notifications)
//   7000: SessionLock
Window {
    id: outputWindow
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1280
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 832
    visible: true
    color: FinnComp.darkTheme ? "#0B0E14" : "#E8EEF7"
    title: "FinnOS Compositor"

    // Layer 0 + 1000: Background wallpaper fallback
    Image {
        anchors.fill: parent
        z: 1000
        source: FinnTheme.wallpaperUrl
        fillMode: Image.PreserveAspectCrop
        smooth: true
    }

    Rectangle {
        anchors.fill: parent
        z: 1001
        color: FinnComp.darkTheme ? FinnTheme.withAlpha("#090B10", 0.62) : "transparent"
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: FinnTheme.motionTheme }
        }
    }

    // Overview subtle backdrop dim when KWin-style Present Windows is active
    Rectangle {
        anchors.fill: parent
        z: 2900
        color: FinnTheme.withAlpha("#0B0E14", FinnComp.overviewActive ? 0.42 : 0.0)
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
    }

    // Layer 3000 & 5000: Managed Toplevels & Fullscreen Surfaces
    Repeater {
        model: FinnCompWindows
        delegate: Item {
            id: winDelegate
            visible: winOpacity > 0.01 && (desktopIndex < 0 || desktopIndex === FinnComp.currentDesktop)
            x: winX
            y: winY
            width: winWidth
            height: winHeight
            z: zIndex
            opacity: winOpacity
            scale: winScale
            transformOrigin: Item.Center

            // Lightweight FinnOS SSD frame (only for clients requesting server decorations)
            FinnSquircle {
                anchors.fill: parent
                radius: fullscreen || maximized ? 0 : FinnTheme.radiusLg
                color: FinnTheme.bone
                borderColor: focused ? FinnTheme.accent : FinnTheme.hair
                borderWidth: focused ? 1.5 : 1.0
                specularRim: !fullscreen && !maximized
                shadow: !fullscreen && !maximized
                shadowAlpha: focused ? 26 : 14
            }

            // SSD Header band when serverDecorated is true (or preview mode)
            Item {
                visible: serverDecorated && !fullscreen
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 36

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: title
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                FinnWindowControls {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    maximized: model.maximized
                    onMinimizeRequested: FinnComp.minimizeWindow(windowId)
                    onMaximizeRequested: FinnComp.toggleMaximize(windowId)
                    onCloseRequested: FinnComp.closeWindow(windowId)
                }

                FinnDivider {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                }
            }

            // Click anywhere on window to focus (or exit Overview and activate)
            MouseArea {
                anchors.fill: parent
                z: -1
                onPressed: {
                    if (FinnComp.overviewActive) {
                        FinnComp.toggleOverview()
                    }
                    FinnComp.focusWindow(windowId)
                }
            }
        }
    }

    // Virtual desktop indicator pill (shown cleanly in Overview)
    FinnGlassPanel {
        visible: FinnComp.overviewActive && FinnComp.desktopCount > 1
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: desktopRow.implicitWidth + 32
        height: 40
        radius: FinnTheme.radiusPill
        z: 6100

        Row {
            id: desktopRow
            anchors.centerIn: parent
            spacing: 8
            Repeater {
                model: FinnComp.desktopCount
                delegate: FinnButton {
                    text: "Desktop " + (index + 1)
                    variant: index === FinnComp.currentDesktop ? "primary" : "ghost"
                    implicitHeight: 28
                    onClicked: FinnComp.switchDesktop(index)
                }
            }
        }
    }
}
