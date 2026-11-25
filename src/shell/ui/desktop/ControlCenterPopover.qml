import QtQuick
import FinnUI 1.0

// Exact Figma Control Center (11:19 / 11:216: 444x300, r=12, C1/C2 fixed!):
// - Top-left: Wi-Fi (211x42) & Bluetooth (211x42) toggle tiles
// - Top-right: Media Control card (211x94, 82x82 art, progress bar, transport buttons)
// - Middle: Full-width Brightness (432x36) & Volume (432x36) pill sliders
// - Bottom: 50px frosted footer bar at y=250 with Dark/Light & Settings (settings-03)
Item {
    id: root
    property bool shown: false

    width: 444
    height: 300
    visible: openSpring.value > 0.01
    opacity: Math.max(0.0, Math.min(1.0, openSpring.value))
    transform: Translate { y: (1.0 - openSpring.value) * 14 }
    scale: 0.97 + 0.03 * openSpring.value

    FinnSpring {
        id: openSpring
        preset: "snappy"
    }
    Component.onCompleted: openSpring.snap(shown ? 1.0 : 0.0)
    onShownChanged: openSpring.target = shown ? 1.0 : 0.0

    FinnGlassPanel {
        anchors.fill: parent
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        MouseArea { anchors.fill: parent }

        // Top Grid: Left column (Wi-Fi + Bluetooth) & Right column (Media Control)
        Item {
            x: 6
            y: 6
            width: 432
            height: 94

            // Wi-Fi Card (Figma 11:250: 211x42, r=6, borderless)
            FinnSquircle {
                x: 0
                y: 0
                width: 211
                height: 42
                radius: FinnTheme.radiusSm
                color: FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.72 : 0.72)
                borderWidth: 0
                specularRim: false

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 8
                    spacing: 10

                    Rectangle {
                        width: 32
                        height: 32
                        radius: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: ShellCorona.wifiAvailable && ShellCorona.wifiEnabled
                            ? FinnTheme.accent
                            : FinnTheme.mute

                        FinnIcon {
                            anchors.centerIn: parent
                            name: "wifi"
                            size: 18
                            tint: "#FFFFFF"
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 152
                        spacing: 1
                        Text {
                            text: "Wi-Fi"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                        }
                        Text {
                            text: ShellCorona.wifiSsid
                            color: FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: ShellCorona.wifiAvailable
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: ShellCorona.toggleWifi()
                }
            }

            // Bluetooth Card (Figma 11:258: 211x42, r=6, borderless)
            FinnSquircle {
                x: 0
                y: 52
                width: 211
                height: 42
                radius: FinnTheme.radiusSm
                color: FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.72 : 0.72)
                borderWidth: 0
                specularRim: false

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 8
                    spacing: 10

                    Rectangle {
                        width: 32
                        height: 32
                        radius: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: ShellCorona.bluetoothAvailable && ShellCorona.bluetoothEnabled
                            ? FinnTheme.accent
                            : FinnTheme.mute

                        FinnIcon {
                            anchors.centerIn: parent
                            name: ShellCorona.bluetoothEnabled ? "bluetooth" : "bluetooth-off"
                            size: 18
                            tint: "#FFFFFF"
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 152
                        spacing: 1
                        Text {
                            text: "Bluetooth"
                            color: FinnTheme.ink
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 13
                        }
                        Text {
                            text: ShellCorona.bluetoothSubtitle
                            color: FinnTheme.dim
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: ShellCorona.bluetoothAvailable
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: ShellCorona.toggleBluetooth()
                }
            }

            // Media Control Card (Figma 11:229: 211x94, r=6, borderless)
            FinnSquircle {
                x: 221
                y: 0
                width: 211
                height: 94
                radius: FinnTheme.radiusSm
                color: FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.72 : 0.72)
                borderWidth: 0
                specularRim: false

                // 82x82 Album Art Well (Figma 11:266)
                Rectangle {
                    x: 6
                    y: 6
                    width: 82
                    height: 82
                    radius: 4
                    color: FinnTheme.surfaceInput

                    FinnIcon {
                        anchors.centerIn: parent
                        name: "volume-02"
                        size: 26
                        tint: FinnTheme.mute
                    }
                }

                Column {
                    x: 96
                    y: 10
                    width: 108
                    spacing: 3

                    Text {
                        text: ShellCorona.mediaTitle
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Text {
                        text: ShellCorona.mediaArtist.length > 0 ? ShellCorona.mediaArtist : "Idle"
                        color: FinnTheme.dim
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    // Progress track (Figma 11:275: 103x4)
                    Rectangle {
                        width: 103
                        height: 4
                        radius: 2
                        color: FinnTheme.withAlpha(FinnTheme.ink, 0.20)
                        Rectangle {
                            width: parent.width * ShellCorona.mediaProgress
                            height: parent.height
                            radius: 2
                            color: FinnTheme.ink
                        }
                    }

                    // Transport buttons (Figma 16:11, 16:2, 16:8)
                    Row {
                        spacing: 12
                        anchors.horizontalCenter: parent.horizontalCenter
                        opacity: ShellCorona.mediaAvailable ? 1.0 : 0.40

                        FinnIconButton {
                            iconName: "backward"
                            buttonSize: 22
                            iconSize: 16
                            enabled: ShellCorona.mediaAvailable
                            onClicked: ShellCorona.mediaPrev()
                        }
                        FinnIconButton {
                            iconName: ShellCorona.mediaPlaying ? "pause" : "play"
                            buttonSize: 22
                            iconSize: 16
                            enabled: ShellCorona.mediaAvailable
                            onClicked: ShellCorona.mediaPlayPause()
                        }
                        FinnIconButton {
                            iconName: "forward"
                            buttonSize: 22
                            iconSize: 16
                            enabled: ShellCorona.mediaAvailable
                            onClicked: ShellCorona.mediaNext()
                        }
                    }
                }
            }
        }

        // Brightness Pill Slider (Figma 17:30: x=6, y=110, 432x36)
        FinnSlider {
            x: 6
            y: 110
            width: 432
            height: 36
            mode: "pill"
            iconName: "sun-dim"
            enabled: ShellCorona.brightnessAvailable
            value: ShellCorona.brightnessAvailable ? ShellCorona.brightnessLevel : 0.0
            onMoved: (v) => ShellCorona.setBrightness(v)
        }

        // Volume Pill Slider (Figma 17:24: x=6, y=156, 432x36)
        FinnSlider {
            x: 6
            y: 156
            width: 432
            height: 36
            mode: "pill"
            iconName: "volume-02"
            enabled: ShellCorona.audioAvailable
            value: ShellCorona.audioAvailable ? ShellCorona.volumeLevel : 0.0
            onMoved: (v) => ShellCorona.setVolume(v)
        }

        // Exact 50px Bottom Bar at y=250 (Figma 17:42: edit-01 + settings-03, no divider line)
        Rectangle {
            x: 0
            y: 250
            width: 444
            height: 50
            color: FinnTheme.glassFooter

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                FinnIconButton {
                    iconName: "edit-01"
                    buttonSize: 32
                    iconSize: 20
                    onClicked: ShellCorona.openSettings("appearance")
                }

                FinnIconButton {
                    iconName: "settings-03"
                    buttonSize: 32
                    iconSize: 20
                    onClicked: ShellCorona.openSettings()
                }
            }
        }
    }
}
