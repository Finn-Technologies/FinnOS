import QtQuick
import QtQuick.Window
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell
import FinnUI 1.0
import FinnComp 1.0

// Real Linux Qt 6 Wayland Compositor Scene Graph for finn-comp.
// Renders live Wayland client buffers (ShellSurfaceItem) across our strict
// 8-layer stacking order with 120 Hz spring window choreography.
Window {
    id: outputWindow
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 1280
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 832
    visible: true
    color: FinnComp.darkTheme ? "#0B0E14" : "#E8EEF7"
    title: "FinnOS Compositor"

    onWidthChanged: FinnComp.setOutputSize(width, height)
    onHeightChanged: FinnComp.setOutputSize(width, height)
    Component.onCompleted: FinnComp.setOutputSize(width, height)

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
        color: FinnComp.darkTheme ? FinnTheme.withAlpha("#0D0F14", 0.35) : "transparent"
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: FinnTheme.motionTheme }
        }
    }

    // Overview subtle backdrop dim
    Rectangle {
        anchors.fill: parent
        z: 2900
        color: FinnTheme.withAlpha("#0B0E14", FinnComp.overviewActive ? 0.42 : 0.0)
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
    }

    // Layer 3000 & 5000: Managed Live Wayland Client Surfaces
    Repeater {
        model: FinnCompWindows
        delegate: Item {
            id: winDelegate
            property bool ready: false
            Component.onCompleted: ready = true
            visible: winOpacity > 0.01 && (desktopIndex < 0 || desktopIndex === FinnComp.currentDesktop)
            x: fullscreen ? 0 : winX
            y: fullscreen ? 0 : winY
            width: fullscreen ? outputWindow.width : winWidth
            height: fullscreen ? outputWindow.height : winHeight
            z: zIndex
            opacity: winOpacity
            scale: winScale
            transformOrigin: Item.Center
            onXChanged: if (ready && !maximized && !fullscreen && Math.abs(x - winX) > 1) FinnComp.reportWindowMoved(windowId, Math.round(x), Math.round(y))
            onYChanged: if (ready && !maximized && !fullscreen && Math.abs(y - winY) > 1) FinnComp.reportWindowMoved(windowId, Math.round(x), Math.round(y))

            // Real-time behind-window backdrop blur (blurs wallpaper + all lower windows!)
            CompositorBackdrop {
                visible: !fullscreen
                anchors.fill: parent
                z: -2
                server: FinnComp
                windowId: model.windowId
                screenX: winDelegate.x
                screenY: winDelegate.y
                radius: maximized ? 0 : FinnTheme.radiusLg
                backdropVersion: FinnComp.backdropVersion
            }

            // SSD chrome only when the client explicitly requested server-side decorations
            FinnSquircle {
                visible: serverDecorated && !fullscreen
                anchors.fill: parent
                radius: maximized ? 0 : FinnTheme.radiusLg
                color: FinnTheme.bone
                borderColor: focused ? FinnTheme.accent : FinnTheme.hair
                borderWidth: focused ? 1.5 : 1.0
                specularRim: !maximized
                shadow: !maximized
                shadowAlpha: focused ? 26 : 14
            }

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

            // Live Wayland client surface buffer: smoothly scales during spring transitions,
            // settling to exact 1:1 native pixel size once the window spring finishes!
            ShellSurfaceItem {
                id: surfaceItem
                anchors.top: parent.top
                anchors.topMargin: (serverDecorated && !fullscreen) ? 36 : 0
                anchors.left: parent.left
                shellSurface: surfaceObject
                inputEventsEnabled: true
                touchEventsEnabled: true
                moveItem: winDelegate
                onSurfaceDestroyed: FinnComp.closeWindow(windowId)

                transform: Scale {
                    origin.x: 0
                    origin.y: 0
                    xScale: (!fullscreen && surfaceItem.width > 16 && Math.abs(winDelegate.width - surfaceItem.width) > 2)
                        ? (winDelegate.width / surfaceItem.width) : 1.0
                    yScale: (!fullscreen && surfaceItem.height > 16 && Math.abs(winDelegate.height - surfaceItem.height) > 2)
                        ? ((winDelegate.height - ((serverDecorated && !fullscreen) ? 36 : 0)) / surfaceItem.height) : 1.0
                }
            }

            MouseArea {
                anchors.fill: parent
                z: -1
                enabled: !fullscreen
                propagateComposedEvents: true
                onPressed: (mouse) => {
                    if (FinnComp.overviewActive) FinnComp.toggleOverview()
                    FinnComp.focusWindow(windowId)
                    surfaceItem.takeFocus()
                    mouse.accepted = false
                }
            }
        }
    }

    // Real-time behind-taskbar & behind-popover backdrop blur (blurs wallpaper + all windows beneath!)
    Repeater {
        model: FinnComp.overlayBlurRects
        delegate: CompositorBackdrop {
            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h
            z: 4999
            server: FinnComp
            windowId: 0
            screenX: modelData.x
            screenY: modelData.y
            radius: modelData.r
            backdropVersion: FinnComp.backdropVersion
        }
    }
}
