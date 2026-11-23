import QtQuick
import QtQuick.Window
import FinnUI 1.0

// Kirigami-inspired unified adaptive FinnOS Application Window.
// Automatically adapts chrome, navigation, and spacing across:
//   - Desktop: Figma split-pane (250px glass sidebar + floating 88x28 WindowOptions pill, zero 48px waste)
//   - Tablet:  Collapsible 68px NavRail + split content pane + touch-friendly controls
//   - Mobile:  Single-column master/detail stack + 56dp bottom navigation bar
//   - TV:      10-foot overscan-safe header + 1.5x typography + D-pad focus navigation
Window {
    id: root
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 880
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 540
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    property string appTitle: "FinnOS App"
    property string pageTitle: ""
    property bool splitChrome: true
    property var navItems: [] // [{ id, label, icon, badgeColor, section }]
    property string activeNavId: navItems.length > 0 ? navItems[0].id : ""
    property bool showSidebarSearch: true
    property string sidebarSearchText: ""
    property bool mobileDetailOpen: false
    property bool fullBleedContent: false
    property real customSidebarWidth: 0
    property color windowContentColor: FinnTheme.dark ? FinnTheme.bone : "#FFFFFF"
    property color windowSidebarTint: FinnTheme.dark ? "#9E161922" : "#96F8FAFC"

    default property alias pageContent: contentArea.data
    property alias headerActions: headerActionRow.data

    signal navSelected(string id)

    readonly property string formFactor: (FinnTheme.formFactorSetting === "tablet" || FinnTheme.formFactorSetting === "mobile" || FinnTheme.formFactorSetting === "tv")
        ? FinnTheme.formFactorSetting
        : "desktop"
    readonly property var ffMetrics: FinnTheme.formFactorMetrics(formFactor, width, height)
    readonly property bool isDesktop: formFactor === "desktop"
    readonly property bool isTablet: formFactor === "tablet"
    readonly property bool isMobile: formFactor === "mobile"
    readonly property bool isTv: formFactor === "tv"
    readonly property bool isEdgeToEdge: isMobile || isTv || root.visibility === Window.Maximized || root.visibility === Window.FullScreen

    function showToast(msg, sev) {
        toast.showToast(msg, sev || "info", 2800)
    }

    function selectNav(id) {
        activeNavId = id
        if (isMobile) mobileDetailOpen = true
        navSelected(id)
    }

    // All 4 corners strictly rounded (r=12); finn-comp provides real-time behind-window blur
    FinnSquircle {
        id: frameBg
        anchors.fill: parent
        radius: root.isEdgeToEdge ? 0 : FinnTheme.radiusLg
        color: root.windowContentColor
        borderColor: root.isEdgeToEdge ? "transparent" : (FinnTheme.dark ? FinnTheme.hair : "#14000000")
        borderWidth: root.isEdgeToEdge ? 0 : 0.5
        specularRim: false
        shadow: false
        sidebarWidth: sidebarPane.visible ? sidebarPane.width : root.customSidebarWidth
        sidebarColor: root.windowSidebarTint
        sidebarBlur: false
    }

    Item {
        id: frameBody
        anchors.fill: parent
        clip: true

        // Top 36px interactive drag region for moving frameless CSD windows across the desktop
        // Sits at z: -1 so interactive top-bar controls (URL bar, tabs, buttons) always receive clicks first!
        MouseArea {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: 100
            height: 36
            z: -1
            onPressed: root.startSystemMove()
            onDoubleClicked: {
                if (root.visibility === Window.Maximized) {
                    root.showNormal()
                } else {
                    root.showMaximized()
                }
            }
        }

        // ---- Desktop & Tablet & TV Split Sidebar / NavRail ----
        Item {
            id: sidebarPane
            visible: root.navItems.length > 0 && (!root.isMobile || !root.mobileDetailOpen)
            anchors.top: parent.top
            anchors.bottom: mobileBottomBar.visible ? mobileBottomBar.top : parent.bottom
            anchors.left: parent.left
            width: root.isMobile
                ? parent.width
                : (root.isTablet && root.width < 840 ? 68 : (root.isTv ? 300 : 250))

            Behavior on width {
                enabled: !FinnTheme.reducedMotion
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            Column {
                anchors.fill: parent
                anchors.leftMargin: (root.isTablet && root.width < 760) ? 8 : 12
                anchors.rightMargin: (root.isTablet && root.width < 760) ? 8 : 12
                anchors.topMargin: root.isTv ? 28 : 32
                anchors.bottomMargin: 12
                spacing: 0

                // App header title (Figma 71:172: x=12, y=32, 63x22, 16px Regular)
                Text {
                    visible: !(root.isTablet && root.width < 760)
                    text: root.appTitle
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: root.isTv ? 26 : 16
                    font.weight: Font.Normal
                    height: root.isTv ? 34 : 22
                    verticalAlignment: Text.AlignVCenter
                }

                Item {
                    visible: !(root.isTablet && root.width < 760)
                    width: 1
                    height: 10
                }

                // Sidebar search field (Figma 71:167: x=12, y=64, 226x30 pill)
                FinnSearchField {
                    visible: root.showSidebarSearch && !(root.isTablet && root.width < 760)
                    width: parent.width
                    height: root.isTv ? 44 : (root.isMobile ? 40 : 30)
                    fontSize: root.isTv ? 16 : 12
                    placeholderText: "Search anything"
                    onTextEdited: (t) => root.sidebarSearchText = t
                }

                Item {
                    visible: root.showSidebarSearch && !(root.isTablet && root.width < 760)
                    width: 1
                    height: 20
                }

                Flickable {
                    width: parent.width
                    height: Math.max(40, parent.height - y)
                    contentHeight: navCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.DragOverBounds

                    Column {
                        id: navCol
                        width: parent.width
                        spacing: root.isTv ? 6 : 5

                        Repeater {
                            model: root.navItems
                            delegate: Column {
                                width: navCol.width
                                readonly property var itemData: modelData
                                readonly property bool matchesFilter: root.sidebarSearchText.trim().length === 0 ||
                                    itemData.label.toLowerCase().indexOf(root.sidebarSearchText.trim().toLowerCase()) >= 0
                                visible: matchesFilter

                                Item {
                                    visible: index > 0 && itemData.section !== undefined &&
                                             root.navItems[index - 1].section !== itemData.section
                                    width: 1
                                    height: root.isTv ? 18 : 15
                                }

                                FinnNavItem {
                                    width: parent.width
                                    label: itemData.label
                                    iconName: itemData.icon || "info"
                                    badgeColor: itemData.badgeColor || FinnTheme.accent
                                    useBadgeCircle: itemData.badgeColor !== undefined
                                    selected: root.activeNavId === itemData.id
                                    compactRail: root.isTablet && root.width < 760
                                    tvMode: root.isTv
                                    onClicked: root.selectNav(itemData.id)
                                }
                            }
                        }
                    }
                }
            }

            // Hairline right divider
            FinnDivider {
                visible: !root.isMobile
                vertical: true
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
        }

        // ---- Main Content Pane ----
        Item {
            id: mainPane
            visible: !root.isMobile || root.mobileDetailOpen || root.navItems.length === 0
            anchors.top: parent.top
            anchors.bottom: mobileBottomBar.visible ? mobileBottomBar.top : parent.bottom
            anchors.left: (sidebarPane.visible && !root.isMobile) ? sidebarPane.right : parent.left
            anchors.right: parent.right

            // Window drag area on Desktop/Tablet top band (z: -1 so fullBleedContent controls receive clicks)
            MouseArea {
                visible: !root.isMobile && !root.isTv
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 40
                z: -1
                onPressed: root.startSystemMove()
                onDoubleClicked: {
                    if (root.visibility === Window.Maximized) root.showNormal()
                    else root.showMaximized()
                }
            }

            // Floating Window Controls Pill (Figma 20:106 at top-right: x=w-94, y=6)
            FinnWindowControls {
                id: winControls
                visible: root.isDesktop || root.isTablet
                anchors.top: parent.top
                anchors.topMargin: 6
                anchors.right: parent.right
                anchors.rightMargin: 6
                z: 20
                maximized: root.visibility === Window.Maximized
                onMinimizeRequested: root.showMinimized()
                onMaximizeRequested: {
                    if (root.visibility === Window.Maximized) root.showNormal()
                    else root.showMaximized()
                }
                onCloseRequested: root.close()
            }

            // Page header row (Figma 71:108: x=12, y=32, h=22, 16px Regular)
            Row {
                id: headerRow
                visible: !root.fullBleedContent
                anchors.top: parent.top
                anchors.topMargin: root.isTv ? 28 : 32
                anchors.left: parent.left
                anchors.leftMargin: root.isTv ? 32 : 12
                anchors.right: parent.right
                anchors.rightMargin: (winControls.visible ? 100 : 12)
                spacing: 10
                height: root.isTv ? 36 : 22

                FinnIconButton {
                    visible: root.isMobile && root.mobileDetailOpen && root.navItems.length > 0
                    iconName: "chevron-left"
                    buttonSize: 32
                    iconSize: 18
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.mobileDetailOpen = false
                }

                Text {
                    text: root.pageTitle.length > 0 ? root.pageTitle : root.appTitle
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: root.isTv ? 28 : 16
                    font.weight: Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                }

                Row {
                    id: headerActionRow
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Application Page Body (Figma 71:88 / 20:102: x=12, y=64, w=526)
            Item {
                id: contentArea
                anchors.top: root.fullBleedContent ? parent.top : headerRow.bottom
                anchors.topMargin: root.fullBleedContent ? 0 : (root.isTv ? 18 : 10)
                anchors.left: parent.left
                anchors.leftMargin: root.fullBleedContent ? 0 : (root.isTv ? 32 : 12)
                anchors.right: parent.right
                anchors.rightMargin: root.fullBleedContent ? 0 : (root.isTv ? 32 : 12)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.fullBleedContent ? 0 : (root.isTv ? 24 : 12)
            }
        }

        // ---- Mobile 56dp Bottom Navigation Bar ----
        Rectangle {
            id: mobileBottomBar
            visible: root.isMobile && root.navItems.length > 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 56
            color: FinnTheme.glassPanel

            FinnDivider {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
            }

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.navItems.slice(0, 5)
                    delegate: Item {
                        width: mobileBottomBar.width / Math.max(1, Math.min(5, root.navItems.length))
                        height: mobileBottomBar.height
                        readonly property bool isSel: root.activeNavId === modelData.id

                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            FinnIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: modelData.icon || "info"
                                size: 20
                                tint: isSel ? FinnTheme.accent : FinnTheme.dim
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: isSel ? FinnTheme.accent : FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectNav(modelData.id)
                        }
                    }
                }
            }
        }

        // ---- Floating Toast Notification ----
        FinnToast {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: mobileBottomBar.visible ? mobileBottomBar.top : parent.bottom
            anchors.bottomMargin: 16
            z: 50
        }
    }
}
