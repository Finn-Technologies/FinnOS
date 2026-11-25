import QtQuick
import FinnUI 1.0

// Finn Viewer — Cohesive FinnOS split-pane with real-time frosted sidebar,
// spacious gallery lightbox canvas, floating glass controls, and image inspector.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 940
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 590
    appTitle: "Finn Viewer"
    pageTitle: activeNavId === "gallery"
        ? (ViewerCtrl.currentName.length > 0 ? ViewerCtrl.currentName : "Gallery")
        : "Image Details"
    activeNavId: "gallery"
    showSidebarSearch: false
    property bool slideshowActive: false

    Timer {
        interval: 3000
        running: win.slideshowActive && ViewerCtrl.gallery.length > 1
        repeat: true
        onTriggered: ViewerCtrl.nextImage()
    }

    navItems: [
        { id: "gallery", label: "Gallery", icon: "image", badgeColor: "#E11D48", section: "view" },
        { id: "details", label: "Details", icon: "info", badgeColor: "#7C3AED", section: "view" }
    ]

    headerActions: [
        FinnIconButton {
            iconName: "chevron-left"
            buttonSize: 28
            iconSize: 15
            onClicked: ViewerCtrl.prevImage()
        },
        FinnIconButton {
            iconName: "chevron-right"
            buttonSize: 28
            iconSize: 15
            onClicked: ViewerCtrl.nextImage()
        },
        FinnButton {
            text: win.slideshowActive ? "Stop" : "Slideshow"
            variant: win.slideshowActive ? "primary" : "secondary"
            implicitHeight: 28
            onClicked: win.slideshowActive = !win.slideshowActive
        },
        FinnButton {
            text: "Flip"
            implicitHeight: 28
            onClicked: ViewerCtrl.toggleFlipH()
        }
    ]

    // Gallery Lightbox View
    Column {
        visible: win.activeNavId === "gallery"
        anchors.fill: parent
        spacing: 12

        // Main Image Stage with Floating Control Pill
        FinnSquircle {
            width: parent.width
            height: parent.height - 86
            radius: FinnTheme.radiusLg
            color: FinnTheme.dark ? "#13161E" : "#F3F5F9"
            borderColor: FinnTheme.hairSoft
            borderWidth: 0.7
            clip: true

            Image {
                id: mainImg
                visible: ViewerCtrl.currentUrl.length > 0
                anchors.centerIn: parent
                width: parent.width - 36
                height: parent.height - 56
                source: ViewerCtrl.currentUrl
                fillMode: Image.PreserveAspectFit
                smooth: true
                mirror: ViewerCtrl.flipH
                mirrorVertically: ViewerCtrl.flipV
                scale: ViewerCtrl.zoom
                rotation: ViewerCtrl.rotation

                Behavior on scale {
                    enabled: !FinnTheme.reducedMotion
                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                }
                Behavior on rotation {
                    enabled: !FinnTheme.reducedMotion
                    NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                }
            }

            // Gwenview-style mouse-wheel zoom on canvas
            WheelHandler {
                onWheel: (event) => {
                    if (event.angleDelta.y > 0) ViewerCtrl.zoomIn()
                    else if (event.angleDelta.y < 0) ViewerCtrl.zoomOut()
                }
            }

            FinnEmptyState {
                visible: ViewerCtrl.currentUrl.length === 0
                anchors.fill: parent
                iconName: "image"
                headline: "No images found"
                description: "Open an image or folder to inspect it in Finn Viewer."
            }

            // Floating Glass Controls Pill at Bottom Center
            FinnSquircle {
                visible: ViewerCtrl.currentUrl.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                width: ctrlRow.implicitWidth + 16
                height: 36
                radius: 18
                color: FinnTheme.withAlpha(FinnTheme.glass, 0.86)
                borderColor: FinnTheme.hair
                borderWidth: 0.8

                Row {
                    id: ctrlRow
                    anchors.centerIn: parent
                    spacing: 4

                    FinnButton { text: "−"; implicitHeight: 26; onClicked: ViewerCtrl.zoomOut() }
                    FinnButton { text: Math.round(ViewerCtrl.zoom * 100) + "%"; implicitHeight: 26; onClicked: ViewerCtrl.resetView() }
                    FinnButton { text: "+"; implicitHeight: 26; onClicked: ViewerCtrl.zoomIn() }
                    FinnButton { text: "90°"; implicitHeight: 26; onClicked: ViewerCtrl.rotateRight() }
                }
            }
        }

        // Bottom Thumbnail Strip + Metadata Pills
        Row {
            width: parent.width
            height: 74
            spacing: 12

            Flickable {
                width: parent.width - 178
                height: 74
                contentWidth: thumbRow.implicitWidth
                clip: true

                Row {
                    id: thumbRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Repeater {
                        model: ViewerCtrl.gallery
                        delegate: FinnSquircle {
                            width: 88
                            height: 62
                            radius: 10
                            color: FinnTheme.tile
                            borderColor: index === ViewerCtrl.currentIndex ? "#E11D48" : FinnTheme.hairSoft
                            borderWidth: index === ViewerCtrl.currentIndex ? 2 : 0.8

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: modelData.url
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ViewerCtrl.currentIndex = index
                            }
                        }
                    }
                }
            }

            Column {
                width: 166
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                FinnBadge { text: ViewerCtrl.dimensionsText || "—"; tone: "accent" }
                FinnBadge { text: ViewerCtrl.fileSizeText || "—"; tone: "neutral" }
            }
        }
    }

    // Image Metadata Inspector View
    Column {
        visible: win.activeNavId === "details"
        anchors.fill: parent
        spacing: 12

        FinnCard {
            width: parent.width
            implicitHeight: metaCol.implicitHeight + 16
            radius: FinnTheme.radiusLg

            Column {
                id: metaCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                FinnSettingRow {
                    width: parent.width
                    title: "File Name"
                    subtitle: ViewerCtrl.currentName || "No image loaded"
                    iconName: "image"
                }
                FinnDivider { width: parent.width }
                FinnSettingRow {
                    width: parent.width
                    title: "Resolution"
                    subtitle: "Native pixel dimensions"
                    iconName: "desktop"
                    FinnBadge { text: ViewerCtrl.dimensionsText || "—"; tone: "accent" }
                }
                FinnDivider { width: parent.width }
                FinnSettingRow {
                    width: parent.width
                    title: "File Size"
                    subtitle: "Disk footprint"
                    iconName: "hard-drive"
                    FinnBadge { text: ViewerCtrl.fileSizeText || "—"; tone: "neutral" }
                }
                FinnDivider { width: parent.width }
                FinnSettingRow {
                    width: parent.width
                    title: "Transform"
                    subtitle: "Current zoom & rotation"
                    iconName: "refresh"
                    Row {
                        spacing: 6
                        FinnBadge { text: Math.round(ViewerCtrl.zoom * 100) + "%"; tone: "neutral" }
                        FinnBadge { text: ViewerCtrl.rotation + "°"; tone: "neutral" }
                    }
                }
            }
        }

        Row {
            spacing: 8
            FinnButton { text: "Reset View"; variant: "primary"; onClicked: ViewerCtrl.resetView() }
            FinnButton { text: "Rotate 90°"; onClicked: ViewerCtrl.rotateRight() }
        }
    }
}
