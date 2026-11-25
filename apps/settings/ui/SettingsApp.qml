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
                if (win.activeNavId === "privacy") return privacyCol.implicitHeight
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
            // 7. GENERAL / PRIVACY & LOCK / UPDATES / COMPATIBILITY PAGE
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
