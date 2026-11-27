import QtQuick
import FinnUI 1.0

// Exact Figma Desktop Taskbar (4:58):
// Four frosted-glass pills separated by 15px gaps, height 44px, radius 12px, cornerSmoothing 0.6:
//   [Apps 254x44]  [Home Buttons 130x44]  [Hardware Options 130x44]  [Time 154x44]
Row {
    id: root
    spacing: 15
    height: 44

    readonly property var dockList: ShellCorona.dockItems
    property var hoveredTile: null

    // 1. Pinned & Live Open/Minimized Apps Taskbar Pill (Figma 11:46: 254x44 base, expands smoothly for unpinned open apps)
    FinnGlassPanel {
        id: appsPill
        width: Math.max(60, appsRow.implicitWidth + 12)
        height: 44
        anchors.verticalCenter: parent.verticalCenter
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        Behavior on width {
            enabled: !FinnTheme.reducedMotion
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        Row {
            id: appsRow
            anchors.centerIn: parent
            spacing: 10

            Repeater {
                model: root.dockList
                delegate: Row {
                    spacing: 10
                    anchors.verticalCenter: parent.verticalCenter

                    // Subtle vertical hairline divider separating pinned apps from dynamically-opened unpinned apps
                    Rectangle {
                        visible: modelData.separatorBefore === true
                        width: 1
                        height: 22
                        radius: 0.5
                        anchors.verticalCenter: parent.verticalCenter
                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.22) : FinnTheme.withAlpha("#000000", 0.16)
                    }

                    Item {
                        id: tileWrap
                        width: 32
                        height: 36
                        anchors.verticalCenter: parent.verticalCenter

                        FinnSpring { id: scaleSpring; preset: "bouncy" }
                        Component.onCompleted: scaleSpring.snap(1.0)

                        readonly property bool showTooltip: iconMa.containsMouse && ShellCorona.activePanel.length === 0 && (modelData.name || "").length > 0

                        onShowTooltipChanged: {
                            if (showTooltip) {
                                root.hoveredTile = tileWrap
                                const pt = tooltipBubble.mapToItem(null, 0, 0)
                                ShellCorona.setHoverBlurRect(pt.x, pt.y, tooltipBubble.width, tooltipBubble.height, tooltipBubble.radius)
                            } else if (root.hoveredTile === tileWrap) {
                                root.hoveredTile = null
                                ShellCorona.clearHoverBlurRect()
                            }
                        }

                        Component.onDestruction: {
                            if (root.hoveredTile === tileWrap) {
                                root.hoveredTile = null
                                ShellCorona.clearHoverBlurRect()
                            }
                        }

                        // Floating app-name bubble above the hovered dock icon
                        FinnGlassPanel {
                            id: tooltipBubble
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.top
                            anchors.bottomMargin: 12
                            width: tooltipText.implicitWidth + 20
                            height: 28
                            radius: FinnTheme.radiusMd
                            fillColor: FinnTheme.dark ? "#D6161922" : "#E6F8FAFC"
                            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.16) : "#1A000000"
                            borderWidth: 0.6
                            shadow: false
                            backdropBlur: !ShellCorona.waylandSession
                            z: 50
                            visible: opacity > 0.01
                            opacity: tileWrap.showTooltip ? 1.0 : 0.0

                            Behavior on opacity {
                                enabled: !FinnTheme.reducedMotion
                                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                            }

                            Text {
                                id: tooltipText
                                anchors.centerIn: parent
                                text: modelData.name || ""
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                        }

                        // Subtle hover highlight when not active
                        Rectangle {
                            visible: iconMa.containsMouse && modelData.active !== true
                            x: -3
                            y: 0
                            width: 38
                            height: 36
                            radius: 9
                            color: FinnTheme.surfaceHover
                        }

                        // Active focused window luminous pill highlight behind icon
                        Rectangle {
                            visible: modelData.active === true
                            x: -3
                            y: 0
                            width: 38
                            height: 36
                            radius: 9
                            color: FinnTheme.dark
                                ? FinnTheme.withAlpha("#FFFFFF", 0.20)
                                : FinnTheme.withAlpha("#FFFFFF", 0.78)
                            border.color: FinnTheme.dark
                                ? FinnTheme.withAlpha("#FFFFFF", 0.12)
                                : FinnTheme.withAlpha("#000000", 0.08)
                            border.width: 0.5
                        }

                        Item {
                            width: 32
                            height: 32
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 1
                            scale: scaleSpring.value
                            opacity: modelData.minimized === true ? 0.52 : 1.0

                            Behavior on opacity {
                                enabled: !FinnTheme.reducedMotion
                                NumberAnimation { duration: 150 }
                            }

                            FinnIcon {
                                anchors.centerIn: parent
                                name: modelData.icon
                                size: 32
                                monochrome: false
                            }
                        }

                        // Running state indicator:
                        // - Active open app:   14x3px vibrant #007BFF accent pill
                        // - Inactive open app: 5x3px crisp ink pill
                        // - Minimized app:     4x4px muted hollow/soft dot
                        Rectangle {
                            visible: modelData.running === true
                            width: modelData.active === true ? 14 : (modelData.minimized === true ? 4 : 6)
                            height: 3
                            radius: 1.5
                            color: modelData.active === true
                                ? FinnTheme.accent
                                : (modelData.minimized === true
                                    ? FinnTheme.withAlpha(FinnTheme.ink, 0.30)
                                    : FinnTheme.withAlpha(FinnTheme.ink, 0.65))
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: -1

                            Behavior on width {
                                enabled: !FinnTheme.reducedMotion
                                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                            }
                        }

                        MouseArea {
                            id: iconMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: { if (!pressed) { scaleSpring.preset = "snappy"; scaleSpring.target = 1.07 } }
                            onExited: { if (!pressed) { scaleSpring.preset = "snappy"; scaleSpring.target = 1.0 } }
                            onPressed: { scaleSpring.preset = "snappy"; scaleSpring.target = 0.92 }
                            onReleased: { scaleSpring.preset = "snappy"; scaleSpring.target = containsMouse ? 1.07 : 1.0 }
                            onClicked: {
                                ShellCorona.handleDockAppClick(modelData.appId, modelData.windowId || "")
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Navigation / Home Buttons Pill (Figma 11:34: 130x44: Search, Home, Open Windows)
    FinnGlassPanel {
        id: navPill
        width: 130
        height: 44
        anchors.verticalCenter: parent.verticalCenter
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        readonly property int activeNavIdx: {
            if (ShellCorona.activePanel === "search") return 0
            if (ShellCorona.activePanel === "home") return 1
            if (ShellCorona.activePanel === "windows") return 2
            return -1
        }

        FinnSpring { id: highlightX; preset: "snappy" }
        Component.onCompleted: highlightX.snap(activeNavIdx >= 0 ? activeNavIdx * 43 : 0)
        onActiveNavIdxChanged: {
            if (activeNavIdx >= 0) highlightX.target = activeNavIdx * 43
        }

        // Sliding active button highlight (Figma 4:57: 44x44 at x=43, y=0; Figma 4:245: 44x44 at x=0, y=0)
        FinnSquircle {
            visible: navPill.activeNavIdx >= 0
            x: highlightX.value
            y: 0
            width: 44
            height: 44
            radius: 10
            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.25) : "#FFFFFF"
            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
            borderWidth: 0.5
            specularRim: false
        }

        Row {
            anchors.centerIn: parent
            spacing: 15

            FinnIconButton {
                iconName: "search-01"
                buttonSize: 28
                iconSize: 24
                radius: 8
                iconColor: FinnTheme.ink
                onClicked: ShellCorona.togglePanel("search")
            }

            FinnIconButton {
                iconName: "home-filled"
                buttonSize: 28
                iconSize: 24
                radius: 8
                iconColor: FinnTheme.ink
                onClicked: ShellCorona.togglePanel("home")
            }

            FinnIconButton {
                iconName: "view-open-windows"
                buttonSize: 28
                iconSize: 24
                radius: 8
                iconColor: FinnTheme.ink
                onClicked: ShellCorona.togglePanel("windows")
            }
        }
    }

    // 3. Hardware Options Pill (Figma 11:23: 130x44: Volume, Wi-Fi, Battery -> Control Center)
    FinnGlassPanel {
        id: hwPill
        width: 130
        height: 44
        anchors.verticalCenter: parent.verticalCenter
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        // Selected highlight inset (Figma 11:204: 124x38 at 3,3, r=9)
        FinnSquircle {
            visible: ShellCorona.activePanel === "control"
            anchors.fill: parent
            anchors.margins: 3
            radius: 9
            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.25) : "#FFFFFF"
            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
            borderWidth: 0.5
            specularRim: false
        }

        Row {
            anchors.centerIn: parent
            spacing: 15

            FinnIcon {
                name: "volume-02"
                size: 24
                tint: FinnTheme.ink
            }

            FinnIcon {
                name: "wifi"
                size: 24
                tint: FinnTheme.ink
            }

            FinnIcon {
                name: "battery"
                size: 24
                tint: FinnTheme.ink
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: ShellCorona.togglePanel("control")
        }
    }

    // 4. Date & Time Pill (Figma 11:21: 154x44 -> Notification Center)
    FinnGlassPanel {
        id: timePill
        width: 154
        height: 44
        anchors.verticalCenter: parent.verticalCenter
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        // Selected highlight inset when Notification Center is open
        FinnSquircle {
            visible: ShellCorona.activePanel === "notifications"
            anchors.fill: parent
            anchors.margins: 3
            radius: 9
            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.25) : "#FFFFFF"
            borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
            borderWidth: 0.5
            specularRim: false
        }

        Row {
            anchors.centerIn: parent
            spacing: 8

            Text {
                text: ShellCorona.dockDateText
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Normal
            }

            Text {
                text: ShellCorona.dockTimeText
                color: FinnTheme.ink
                font.family: FinnTheme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Normal
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: ShellCorona.togglePanel("notifications")
        }
    }
}
