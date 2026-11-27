import QtQuick
import FinnUI 1.0

// FinnOS System Settings (adaptive Desktop / Tablet / Mobile / TV).
// Exact Figma About (20:98) & Sound (71:54) + Form Factor, Appearance, Network,
// Displays, Privacy & Lock Screen Wallpaper, Updates, and Compatibility.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 800
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 540
    appTitle: "Settings"
    pageTitle: activeNavId === "about" ? "About this device" : titleForPage(activeNavId)
    activeNavId: InitialPage && InitialPage.length > 0 ? InitialPage : "about"
    mobileDetailOpen: InitialPage && InitialPage.length > 0 && InitialPage !== "about"

    // Exact 8 Figma sidebar items (71:117 / 20:101: Wi-Fi, Bluetooth, Network | General, Appearance, Display, Sound | About)
    navItems: [
        { id: "wifi", label: "Wi-Fi", icon: "wifi", badgeColor: "#007BFF", section: "network" },
        { id: "bluetooth", label: "Bluetooth", icon: "bluetooth", badgeColor: "#007BFF", section: "network" },
        { id: "network", label: "Network", icon: "globe", badgeColor: "#007BFF", section: "network" },
        { id: "general", label: "General", icon: "cog", badgeColor: "#AEAEAE", section: "system" },
        { id: "users", label: "Users", icon: "user", badgeColor: "#007BFF", section: "system" },
        { id: "appearance", label: "Appearance", icon: "swatch-book", badgeColor: "#000000", section: "system" },
        { id: "displays", label: "Display", icon: "sun-dim", badgeColor: "#007BFF", section: "system" },
        { id: "sound", label: "Sound", icon: "volume-02", badgeColor: "#FF0000", section: "system" },
        { id: "about", label: "About", icon: "info", badgeColor: "#007BFF", section: "info" }
    ]

    function titleForPage(id) {
        if (id === "privacy") return "Privacy & Lock"
        if (id === "updates") return "Updates"
        if (id === "compat") return "Compatibility"
        for (var i = 0; i < navItems.length; ++i) {
            if (navItems[i].id === id) return navItems[i].label
        }
        return "Settings"
    }

    Connections {
        target: SettingsCtrl
        function onToastRequested(msg, sev) { win.showToast(msg, sev) }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: pageStack.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.DragOverBounds

        Item {
            id: pageStack
            width: parent.width
            implicitHeight: {
                if (win.activeNavId === "about") return aboutCol.implicitHeight
                if (win.activeNavId === "sound") return soundCol.implicitHeight
                if (win.activeNavId === "appearance") return appearanceCol.implicitHeight
                if (win.activeNavId === "displays") return displaysCol.implicitHeight
                if (win.activeNavId === "wifi") return wifiCol.implicitHeight
                if (win.activeNavId === "bluetooth") return btCol.implicitHeight
                if (win.activeNavId === "users") return usersCol.implicitHeight
                if (win.activeNavId === "privacy" || win.activeNavId === "general") return privacyCol.implicitHeight
                if (win.activeNavId === "updates") return updatesCol.implicitHeight
                return compatCol.implicitHeight
            }

            // ================================================================
            // 1. ABOUT PAGE (Exact Figma Settings - About 20:98 Bento Layout)
            // ================================================================
            Column {
                id: aboutCol
                visible: win.activeNavId === "about"
                width: parent.width
                spacing: 10

                // Top 2-Column Figma Bento: Left 258x150 OS Hero Card | Right Stacked RAM + Storage (2x70 + 10 = 150)
                Grid {
                    width: parent.width
                    columns: win.isMobile ? 1 : 2
                    columnSpacing: 10
                    rowSpacing: 10
                    readonly property real colW: columns === 1 ? width : (width - 10) / 2

                    FinnHeroCard {
                        width: parent.colW
                        height: win.isTv ? 186 : 150
                        productName: SettingsCtrl.osName
                        editionName: SettingsCtrl.osVersion
                    }

                    Column {
                        width: parent.colW
                        spacing: 10

                        FinnStatCard {
                            width: parent.width
                            height: win.isTv ? 88 : 70
                            label: "RAM"
                            primaryValue: SettingsCtrl.memoryPrimary
                            mutedSuffix: SettingsCtrl.memorySuffix
                            tvMode: win.isTv
                        }

                        FinnStatCard {
                            width: parent.width
                            height: win.isTv ? 88 : 70
                            label: "Storage"
                            primaryValue: SettingsCtrl.storagePrimary
                            mutedSuffix: SettingsCtrl.storageSuffix
                            tvMode: win.isTv
                        }
                    }
                }

                // Bottom 2-Column Hardware Row: CPU | GPU (Exact Figma 25:156, 25:164)
                Grid {
                    width: parent.width
                    columns: win.isMobile ? 1 : 2
                    columnSpacing: 10
                    rowSpacing: 10
                    readonly property real cardW: columns === 1 ? width : (width - 10) / 2

                    FinnStatCard {
                        width: parent.cardW
                        height: win.isTv ? 88 : 70
                        label: "CPU"
                        primaryValue: SettingsCtrl.processorPrimary
                        mutedSuffix: SettingsCtrl.processorSuffix
                        tvMode: win.isTv
                    }

                    FinnStatCard {
                        width: parent.cardW
                        height: win.isTv ? 88 : 70
                        label: "GPU"
                        primaryValue: SettingsCtrl.graphicsPrimary
                        mutedSuffix: SettingsCtrl.graphicsSuffix
                        tvMode: win.isTv
                    }
                }

                // Exact Figma 20:64 About Bento ends right after CPU & GPU (no extra invented card below)
            }

            // ================================================================
            // 2. SOUND PAGE (Exact Figma Settings - Sound 71:54: 5x 526x32 rows at y=64..224)
            // ================================================================
            Column {
                id: soundCol
                visible: win.activeNavId === "sound"
                width: parent.width
                spacing: 0

                // Single 526x160 r=6 stacked white group with 0.5px #0000000D hairlines (Figma 71:239..71:358)
                FinnSquircle {
                    width: parent.width
                    height: 160
                    radius: 6
                    color: FinnTheme.dark ? FinnTheme.tile : "#FFFFFF"
                    borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                    borderWidth: 0.5
                    specularRim: false

                    Column {
                        anchors.fill: parent
                        spacing: 0

                        // Row 1: Alert sound (71:239: 526x32, y=0)
                        Item {
                            width: parent.width
                            height: 32

                            Text {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Alert sound"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 14

                                Item {
                                    width: 68
                                    height: 22
                                    anchors.verticalCenter: parent.verticalCenter

                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        Text {
                                            text: SettingsCtrl.alertSound
                                            color: FinnTheme.ink
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 12
                                            font.weight: Font.Normal
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        FinnIcon {
                                            name: "chevrons-up-down"
                                            size: 14
                                            tint: FinnTheme.ink
                                            opacity: 0.75
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var choices = SettingsCtrl.alertSoundChoices
                                            var idx = choices.indexOf(SettingsCtrl.alertSound)
                                            SettingsCtrl.alertSound = choices[(idx + 1) % choices.length]
                                        }
                                    }
                                }

                                FinnIconButton {
                                    iconName: "play"
                                    buttonSize: 18
                                    iconSize: 13
                                    radius: 4
                                    iconColor: FinnTheme.withAlpha(FinnTheme.ink, 0.55)
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: SettingsCtrl.previewAlertSound()
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 0.5
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                            }
                        }

                        // Row 2: Alert volume (71:337: 526x32, y=32, Volume group 206x18 at x=313, y=7)
                        Item {
                            width: parent.width
                            height: 32

                            Text {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Alert volume"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal
                            }

                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10

                                FinnIcon {
                                    name: "volume-min"
                                    size: 18
                                    tint: FinnTheme.withAlpha(FinnTheme.ink, 0.50)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                FinnSlider {
                                    width: 150
                                    height: 18
                                    mode: "compact"
                                    value: SettingsCtrl.alertVolume
                                    onMoved: (v) => SettingsCtrl.alertVolume = v
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                FinnIcon {
                                    name: "volume-max"
                                    size: 18
                                    tint: FinnTheme.withAlpha(FinnTheme.ink, 0.50)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 0.5
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                            }
                        }

                        // Row 3: Play sound on startup (71:353: 526x32, y=64, Switch 30x18 at x=489, y=7)
                        Item {
                            width: parent.width
                            height: 32

                            Text {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Play sound on startup"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal
                            }

                            FinnToggle {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                checked: SettingsCtrl.startupSound
                                onToggled: (v) => SettingsCtrl.startupSound = v
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 0.5
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                            }
                        }

                        // Row 4: Play UI sound effects (71:358: 526x32, y=96)
                        Item {
                            width: parent.width
                            height: 32

                            Text {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Play UI sound effects"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal
                            }

                            FinnToggle {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                checked: SettingsCtrl.uiSounds
                                onToggled: (v) => SettingsCtrl.uiSounds = v
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 0.5
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.08) : "#0D000000"
                            }
                        }

                        // Row 5: Play feedback when volume is changed (71:324: 526x32, y=128)
                        Item {
                            width: parent.width
                            height: 32

                            Text {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Play feedback when volume is changed"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Normal
                            }

                            FinnToggle {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                checked: SettingsCtrl.volumeFeedback
                                onToggled: (v) => SettingsCtrl.volumeFeedback = v
                            }
                        }
                    }
                }
            }

            // ================================================================
            // 3. APPEARANCE & ADAPTIVE FORM FACTOR PAGE
            // ================================================================
            Column {
                id: appearanceCol
                visible: win.activeNavId === "appearance"
                width: parent.width
                spacing: 12

                // Unified Adaptive Form Factor Switcher
                FinnCard {
                    width: parent.width
                    implicitHeight: ffCol.implicitHeight + 24

                    Column {
                        id: ffCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "System UI Form Factor"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: win.isTv ? 20 : 14
                            font.weight: Font.Medium
                        }

                        Text {
                            text: "Choose how FinnOS adapts across screens, or leave on Auto to follow viewport geometry."
                            color: FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: win.isTv ? 15 : 12
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        Flow {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: [
                                    { id: "auto", label: "Auto", icon: "sliders" },
                                    { id: "desktop", label: "Desktop", icon: "desktop" },
                                    { id: "tablet", label: "Tablet", icon: "tablet" },
                                    { id: "mobile", label: "Mobile", icon: "smartphone" },
                                    { id: "tv", label: "TV", icon: "tv" }
                                ]
                                delegate: FinnButton {
                                    text: modelData.label
                                    iconName: modelData.icon
                                    tvMode: win.isTv
                                    variant: FinnTheme.formFactorSetting === modelData.id ? "primary" : "secondary"
                                    onClicked: {
                                        FinnTheme.formFactorSetting = modelData.id
                                        win.showToast("Form factor set to " + modelData.label, "success")
                                    }
                                }
                            }
                        }
                    }
                }

                // Theme & Motion Toggles
                FinnCard {
                    width: parent.width
                    implicitHeight: themeToggleCol.implicitHeight + 12

                    Column {
                        id: themeToggleCol
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 0

                        FinnSettingRow {
                            width: parent.width
                            title: "Dark Appearance"
                            subtitle: "Switch between FinnOS Light and Dark glass surfaces"
                            iconName: FinnTheme.dark ? "moon" : "sun-dim"
                            tvMode: win.isTv
                            FinnToggle {
                                checked: FinnTheme.dark
                                onToggled: (v) => FinnTheme.dark = v
                            }
                        }

                        FinnDivider { width: parent.width }

                        FinnSettingRow {
                            width: parent.width
                            title: "Reduce Motion"
                            subtitle: "Replace spring animations with instant transitions"
                            iconName: "accessibility"
                            tvMode: win.isTv
                            FinnToggle {
                                checked: FinnTheme.reducedMotion
                                onToggled: (v) => FinnTheme.reducedMotion = v
                            }
                        }
                    }
                }

                // Desktop & Lock Screen Wallpaper Picker
                FinnCard {
                    width: parent.width
                    implicitHeight: wpCol.implicitHeight + 24

                    Column {
                        id: wpCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            text: "Desktop & Lock Screen Wallpaper"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: win.isTv ? 20 : 14
                            font.weight: Font.Medium
                        }

                        Flow {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: SettingsCtrl.availableWallpapers
                                delegate: Column {
                                    width: 132
                                    spacing: 6

                                    FinnSquircle {
                                        width: 132
                                        height: 82
                                        radius: 10
                                        color: FinnTheme.tile
                                        borderColor: SettingsCtrl.wallpaperPath === modelData.path ? FinnTheme.accent : FinnTheme.hairSoft
                                        borderWidth: SettingsCtrl.wallpaperPath === modelData.path ? 2 : 1

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
                                            onClicked: SettingsCtrl.applyWallpaper(modelData.path)
                                        }
                                    }

                                    Row {
                                        width: 132
                                        spacing: 4
                                        FinnButton {
                                            width: 64
                                            implicitHeight: 24
                                            text: "Desktop"
                                            onClicked: SettingsCtrl.applyWallpaper(modelData.path)
                                        }
                                        FinnButton {
                                            width: 64
                                            implicitHeight: 24
                                            text: "Lock"
                                            onClicked: SettingsCtrl.applyLockWallpaper(modelData.path)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ================================================================
            // 4. DISPLAYS PAGE
            // ================================================================
            Column {
                id: displaysCol
                visible: win.activeNavId === "displays"
                width: parent.width
                spacing: 12

                FinnCard {
                    width: parent.width
                    implicitHeight: dispInner.implicitHeight + 12

                    Column {
                        id: dispInner
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 0

                        FinnSettingRow {
                            width: parent.width
                            title: "Primary Display"
                            subtitle: SettingsCtrl.displaySummary
                            iconName: "display"
                            tvMode: win.isTv
                            FinnBadge { text: SettingsCtrl.currentResolution; tone: "accent" }
                        }

                        FinnDivider { width: parent.width }

                        // Interactive Resolution Picker (DRM KMS modes)
                        Column {
                            width: parent.width
                            spacing: 8
                            padding: 6

                            Text {
                                text: "Display Resolution"
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Flow {
                                width: parent.width - 12
                                spacing: 8

                                Repeater {
                                    model: SettingsCtrl.availableResolutions
                                    delegate: FinnButton {
                                        text: modelData
                                        implicitHeight: 28
                                        variant: SettingsCtrl.currentResolution === modelData ? "primary" : "secondary"
                                        onClicked: SettingsCtrl.applyResolution(modelData)
                                    }
                                }
                            }
                        }

                        FinnDivider { width: parent.width }

                        FinnSettingRow {
                            width: parent.width
                            title: "Backlight Brightness"
                            subtitle: SettingsCtrl.brightnessAvailable
                                ? Math.round(SettingsCtrl.brightness * 100) + "%"
                                : "No hardware backlight controller detected"
                            iconName: "sun-dim"
                            tvMode: win.isTv

                            FinnSlider {
                                width: 160
                                height: 18
                                mode: "compact"
                                enabled: SettingsCtrl.brightnessAvailable
                                value: SettingsCtrl.brightnessAvailable ? SettingsCtrl.brightness : 0.0
                                onMoved: (v) => SettingsCtrl.setBrightnessLevel(v)
                            }
                        }
                    }
                }
            }

            // ================================================================
            // 5. WI-FI & NETWORK PAGE
            // ================================================================
            Column {
                id: wifiCol
                visible: win.activeNavId === "wifi" || win.activeNavId === "network"
                width: parent.width
                spacing: 12

                FinnCard {
                    width: parent.width
                    implicitHeight: wifiHeaderCol.implicitHeight + 12

                    Column {
                        id: wifiHeaderCol
                        anchors.fill: parent
                        anchors.margins: 6

                        FinnSettingRow {
                            width: parent.width
                            title: "Wi-Fi"
                            subtitle: SettingsCtrl.wifiSsid
                            iconName: "wifi"
                            tvMode: win.isTv

                            Row {
                                spacing: 8
                                FinnButton {
                                    text: "Scan"
                                    enabled: SettingsCtrl.wifiAvailable && SettingsCtrl.wifiEnabled
                                    onClicked: SettingsCtrl.scanWifi()
                                }
                                FinnToggle {
                                    enabled: SettingsCtrl.wifiAvailable
                                    checked: SettingsCtrl.wifiEnabled
                                    onToggled: SettingsCtrl.toggleWifi()
                                }
                            }
                        }
                    }
                }

                FinnEmptyState {
                    visible: !SettingsCtrl.wifiAvailable || SettingsCtrl.wifiNetworks.length === 0
                    width: parent.width
                    height: 180
                    iconName: "wifi"
                    headline: SettingsCtrl.wifiAvailable ? "No networks found" : "Wi-Fi hardware unavailable"
                    description: SettingsCtrl.wifiAvailable
                        ? "Tap Scan to search for nearby wireless networks."
                        : "NetworkManager reported no wireless adapter on this machine."
                }
            }

            // ================================================================
            // 6. BLUETOOTH PAGE
            // ================================================================
            Column {
                id: btCol
                visible: win.activeNavId === "bluetooth"
                width: parent.width
                spacing: 12

                FinnCard {
                    width: parent.width
                    implicitHeight: btRow.implicitHeight + 12

                    FinnSettingRow {
                        id: btRow
                        anchors.fill: parent
                        anchors.margins: 6
                        title: "Bluetooth"
                        subtitle: SettingsCtrl.bluetoothSubtitle
                        iconName: "bluetooth"
                        tvMode: win.isTv
                        FinnToggle {
                            enabled: SettingsCtrl.bluetoothAvailable
                            checked: SettingsCtrl.bluetoothEnabled
                            onToggled: SettingsCtrl.toggleBluetooth()
                        }
                    }
                }

                FinnEmptyState {
                    visible: !SettingsCtrl.bluetoothAvailable
                    width: parent.width
                    height: 180
                    iconName: "bluetooth"
                    headline: "Bluetooth adapter unavailable"
                    description: "BlueZ reported no active Bluetooth controller."
                }
            }

            // ================================================================
            // 7. USERS & ACCOUNTS PAGE
            // ================================================================
            Column {
                id: usersCol
                visible: win.activeNavId === "users"
                width: parent.width
                spacing: 14

                property string passwordTargetUser: ""
                property string newFullName: ""
                property string newUsername: ""
                property string newPassword: ""
                property bool newIsAdmin: true
                property string adminPasswordInput: ""
                property string changePwValue: ""

                // Existing accounts list
                FinnCard {
                    width: parent.width
                    implicitHeight: usersListCol.implicitHeight + 12

                    Column {
                        id: usersListCol
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 0

                        Repeater {
                            model: SettingsCtrl.systemUsers
                            delegate: Column {
                                width: usersListCol.width
                                spacing: 0

                                FinnDivider {
                                    visible: index > 0
                                    width: parent.width
                                }

                                Item {
                                    width: parent.width
                                    height: 58

                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 12

                                        Rectangle {
                                            width: 34
                                            height: 34
                                            radius: 17
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: modelData.isCurrent ? FinnTheme.accent : FinnTheme.surfaceHover

                                            Text {
                                                anchors.centerIn: parent
                                                text: modelData.displayName.length > 0 ? modelData.displayName.charAt(0).toUpperCase() : "U"
                                                color: modelData.isCurrent ? "#FFFFFF" : FinnTheme.ink
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                            }
                                        }

                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2

                                            Row {
                                                spacing: 6
                                                Text {
                                                    text: modelData.displayName
                                                    color: FinnTheme.ink
                                                    font.family: FinnTheme.fontFamily
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                }
                                                FinnBadge {
                                                    visible: modelData.isCurrent
                                                    text: "Current"
                                                    tone: "accent"
                                                }
                                                FinnBadge {
                                                    text: modelData.isAdmin ? "Admin" : "Standard"
                                                    tone: "neutral"
                                                }
                                            }

                                            Text {
                                                text: "@" + modelData.username
                                                color: FinnTheme.inkMute
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 11
                                            }
                                        }
                                    }

                                    Row {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6

                                        FinnButton {
                                            text: "Password"
                                            variant: "secondary"
                                            enabled: !SettingsCtrl.userActionBusy
                                            onClicked: {
                                                usersCol.passwordTargetUser = modelData.username
                                                usersCol.changePwValue = ""
                                            }
                                        }

                                        FinnButton {
                                            visible: !modelData.isCurrent
                                            text: "Remove"
                                            variant: "secondary"
                                            enabled: !SettingsCtrl.userActionBusy
                                            onClicked: SettingsCtrl.deleteUser(modelData.username, usersCol.adminPasswordInput)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Inline password change card
                FinnCard {
                    visible: usersCol.passwordTargetUser.length > 0
                    width: parent.width
                    implicitHeight: pwChangeCol.implicitHeight + 28

                    Column {
                        id: pwChangeCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Text {
                            text: "Change password for @" + usersCol.passwordTargetUser
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            width: parent.width
                            height: 34
                            radius: 8
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#08000000"
                            border.width: 1
                            border.color: pwNewInput.activeFocus ? FinnTheme.accent : FinnTheme.hair

                            TextInput {
                                id: pwNewInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                echoMode: TextInput.Password
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                text: usersCol.changePwValue
                                onTextChanged: usersCol.changePwValue = text
                            }

                            Text {
                                visible: pwNewInput.text.length === 0
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "New password"
                                color: FinnTheme.inkMute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                            }
                        }

                        Row {
                            spacing: 8
                            FinnButton {
                                text: SettingsCtrl.userActionBusy ? "Saving..." : "Update Password"
                                variant: "primary"
                                enabled: !SettingsCtrl.userActionBusy && usersCol.changePwValue.length > 0
                                onClicked: {
                                    SettingsCtrl.changeUserPassword(
                                        usersCol.passwordTargetUser,
                                        usersCol.changePwValue,
                                        usersCol.adminPasswordInput)
                                    usersCol.passwordTargetUser = ""
                                    usersCol.changePwValue = ""
                                }
                            }
                            FinnButton {
                                text: "Cancel"
                                variant: "secondary"
                                onClicked: {
                                    usersCol.passwordTargetUser = ""
                                    usersCol.changePwValue = ""
                                }
                            }
                        }
                    }
                }

                // Add New User card
                FinnCard {
                    width: parent.width
                    implicitHeight: addUserCol.implicitHeight + 28

                    Column {
                        id: addUserCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Text {
                            text: "Add New User"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Grid {
                            width: parent.width
                            columns: win.isMobile ? 1 : 2
                            columnSpacing: 10
                            rowSpacing: 10
                            readonly property real fieldW: columns === 1 ? width : (width - 10) / 2

                            // Full Name
                            Rectangle {
                                width: parent.fieldW
                                height: 34
                                radius: 8
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#08000000"
                                border.width: 1
                                border.color: fullNameField.activeFocus ? FinnTheme.accent : FinnTheme.hair

                                TextInput {
                                    id: fullNameField
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    text: usersCol.newFullName
                                    onTextChanged: {
                                        usersCol.newFullName = text
                                        if (!userNameField.activeFocus && usersCol.newUsername.length === 0) {
                                            // keep username editable independently
                                        }
                                    }
                                }

                                Text {
                                    visible: fullNameField.text.length === 0
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Full name"
                                    color: FinnTheme.inkMute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                }
                            }

                            // Username
                            Rectangle {
                                width: parent.fieldW
                                height: 34
                                radius: 8
                                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#08000000"
                                border.width: 1
                                border.color: userNameField.activeFocus ? FinnTheme.accent : FinnTheme.hair

                                TextInput {
                                    id: userNameField
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                    text: usersCol.newUsername
                                    onTextChanged: usersCol.newUsername = text.toLowerCase().replace(/[^a-z0-9_-]/g, "")
                                }

                                Text {
                                    visible: userNameField.text.length === 0
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Username (e.g. alex)"
                                    color: FinnTheme.inkMute
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                }
                            }
                        }

                        // Password
                        Rectangle {
                            width: parent.width
                            height: 34
                            radius: 8
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#08000000"
                            border.width: 1
                            border.color: newPwField.activeFocus ? FinnTheme.accent : FinnTheme.hair

                            TextInput {
                                id: newPwField
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                echoMode: TextInput.Password
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                text: usersCol.newPassword
                                onTextChanged: usersCol.newPassword = text
                            }

                            Text {
                                visible: newPwField.text.length === 0
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Password"
                                color: FinnTheme.inkMute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                            }
                        }

                        // Admin password prompt (only shown when sudo requires password)
                        Rectangle {
                            visible: SettingsCtrl.sudoPasswordRequired
                            width: parent.width
                            height: 34
                            radius: 8
                            color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.06) : "#08000000"
                            border.width: 1
                            border.color: adminPwField.activeFocus ? FinnTheme.accent : FinnTheme.hair

                            TextInput {
                                id: adminPwField
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                echoMode: TextInput.Password
                                color: FinnTheme.ink
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                                text: usersCol.adminPasswordInput
                                onTextChanged: usersCol.adminPasswordInput = text
                            }

                            Text {
                                visible: adminPwField.text.length === 0
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Current administrator password"
                                color: FinnTheme.inkMute
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 13
                            }
                        }

                        Item {
                            width: parent.width
                            height: 36

                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10

                                FinnToggle {
                                    checked: usersCol.newIsAdmin
                                    onToggled: usersCol.newIsAdmin = !usersCol.newIsAdmin
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Administrator privileges"
                                    color: FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 13
                                }
                            }

                            FinnButton {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: SettingsCtrl.userActionBusy ? "Creating..." : "Create User"
                                variant: "primary"
                                enabled: !SettingsCtrl.userActionBusy && usersCol.newUsername.length > 0
                                onClicked: {
                                    SettingsCtrl.createUser(
                                        usersCol.newFullName,
                                        usersCol.newUsername,
                                        usersCol.newPassword,
                                        usersCol.newIsAdmin,
                                        usersCol.adminPasswordInput)
                                    usersCol.newFullName = ""
                                    usersCol.newUsername = ""
                                    usersCol.newPassword = ""
                                }
                            }
                        }
                    }
                }
            }

            // ================================================================
            // 8. GENERAL / PRIVACY & LOCK / UPDATES / COMPATIBILITY PAGE
            // ================================================================
            Column {
                id: privacyCol
                visible: win.activeNavId === "privacy" || win.activeNavId === "general"
                width: parent.width
                spacing: 12

                FinnCard {
                    width: parent.width
                    implicitHeight: privInner.implicitHeight + 12

                    Column {
                        id: privInner
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 0

                        FinnSettingRow {
                            width: parent.width
                            title: "Automatic Screen Lock"
                            subtitle: SettingsCtrl.autoLockMinutes === 0
                                ? "Never lock automatically"
                                : "Lock after " + SettingsCtrl.autoLockMinutes + " minutes of idle"
                            iconName: "lock"
                            tvMode: win.isTv

                            Row {
                                spacing: 6
                                Repeater {
                                    model: [0, 2, 5, 15]
                                    delegate: FinnButton {
                                        text: modelData === 0 ? "Never" : (modelData + "m")
                                        variant: SettingsCtrl.autoLockMinutes === modelData ? "primary" : "secondary"
                                        onClicked: SettingsCtrl.autoLockMinutes = modelData
                                    }
                                }
                            }
                        }

                        FinnDivider { width: parent.width }

                        FinnSettingRow {
                            width: parent.width
                            title: "Lock Screen Wallpaper"
                            subtitle: SettingsCtrl.lockWallpaperPath.length > 0
                                ? SettingsCtrl.lockWallpaperPath
                                : "Following default desktop wallpaper"
                            iconName: "image"
                            tvMode: win.isTv
                            FinnButton {
                                text: "Change"
                                onClicked: win.selectNav("appearance")
                            }
                        }

                        FinnDivider { width: parent.width }

                        FinnSettingRow {
                            width: parent.width
                            title: "Software Update"
                            subtitle: SettingsCtrl.updateStatus
                            iconName: "update"
                            tvMode: win.isTv
                            FinnButton {
                                text: "Open"
                                onClicked: win.selectNav("updates")
                            }
                        }
                    }
                }
            }

            // ================================================================
            // 8. SYSTEM UPDATES PAGE
            // ================================================================
            Column {
                id: updatesCol
                visible: win.activeNavId === "updates"
                width: parent.width
                spacing: 12

                FinnHeroCard {
                    width: parent.width
                    title: SettingsCtrl.osName + " " + SettingsCtrl.osVersion
                    subtitle: SettingsCtrl.updateStatus
                    tvMode: win.isTv
                    actionText: SettingsCtrl.updateApplying
                        ? "Installing..."
                        : (SettingsCtrl.updateAvailable
                            ? "Install " + SettingsCtrl.availableVersion
                            : (SettingsCtrl.updateChecking ? "Checking..." : "Check Now"))
                    onActionClicked: {
                        if (SettingsCtrl.updateAvailable) SettingsCtrl.applyUpdate()
                        else SettingsCtrl.checkForUpdates()
                    }
                }
            }

            // ================================================================
            // 9. COMPATIBILITY RUNTIMES PAGE
            // ================================================================
            Column {
                id: compatCol
                visible: win.activeNavId === "compat"
                width: parent.width
                spacing: 10

                Repeater {
                    model: SettingsCtrl.compatRuntimes
                    delegate: FinnCard {
                        width: compatCol.width
                        implicitHeight: cRow.implicitHeight + 12

                        FinnSettingRow {
                            id: cRow
                            anchors.fill: parent
                            anchors.margins: 6
                            title: modelData.name
                            subtitle: modelData.description
                            iconName: "layers"
                            tvMode: win.isTv
                            FinnBadge {
                                text: modelData.status
                                tone: modelData.available ? "success" : "neutral"
                            }
                        }
                    }
                }
            }
        }
    }
}
