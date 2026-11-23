import QtQuick
import FinnUI 1.0

// Exact Figma Lock Screen (84:389):
// - Cover-fitted Lock Screen wallpaper (falls back to desktop wallpaper)
// - Top-center: Date "Friday, 20 November" (24px) + Clock "16:32" (64px)
// - Bottom-center: 60x60 gradient Avatar (pfp), Username (20px),
//   174x34 frosted pill password input with arrow-right submit,
//   and 75x36 frosted pill holding Restart + Power icons.
// Adapts cleanly for Mobile (larger touch controls) & TV (10-foot scale).
Item {
    id: root
    property bool locked: false
    property string formFactor: "desktop"

    readonly property bool isMobile: formFactor === "mobile"
    readonly property bool isTv: formFactor === "tv"

    visible: opacity > 0.01
    opacity: locked ? 1.0 : 0.0

    Behavior on opacity {
        enabled: !FinnTheme.reducedMotion
        NumberAnimation { duration: FinnTheme.motionSurfaceOpen; easing.type: Easing.OutCubic }
    }

    FinnSpring {
        id: shakeSpring
        preset: "snappy"
    }
    Component.onCompleted: shakeSpring.snap(0.0)

    Connections {
        target: ShellCorona
        function onUnlockFailedShake() {
            if (!FinnTheme.reducedMotion) {
                shakeSpring.impulse(900.0, 0.0)
            }
            passInput.text = ""
            passInput.forceActiveFocus()
        }
    }

    // Opaque cover-fitted lock wallpaper (nothing behind can be seen)
    WallpaperLayer {
        anchors.fill: parent
        customSource: FinnTheme.lockWallpaperUrl
    }

    // Soft readability scrim
    Rectangle {
        anchors.fill: parent
        color: FinnTheme.dark ? FinnTheme.withAlpha("#0B0E14", 0.30) : FinnTheme.withAlpha("#FFFFFF", 0.06)
    }

    // Top-center Day & Time (Figma 84:585: y=132, Date 24px Regular, Time 64px Regular)
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.isMobile ? 96 : (root.isTv ? 110 : 132)
        spacing: 6

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ShellCorona.lockDateText
            color: FinnTheme.dark ? "#F2F4F8" : "#000000"
            font.family: FinnTheme.fontFamily
            font.pixelSize: root.isTv ? 32 : (root.isMobile ? 20 : 24)
            font.weight: Font.Normal
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ShellCorona.lockTimeText
            color: FinnTheme.dark ? "#FFFFFF" : "#000000"
            font.family: FinnTheme.fontFamily
            font.pixelSize: root.isTv ? 88 : (root.isMobile ? 68 : 64)
            font.weight: Font.Normal
        }
    }

    // Bottom-center User + Password Pill + Power Pill (Exact Figma 84:587, 84:593, 84:618)
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.isMobile ? 48 : (root.isTv ? 72 : 39)
        spacing: 20

        // User Avatar (60x60) + Name (20px Regular)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12

            FinnAvatar {
                width: root.isTv ? 84 : 60
                height: root.isTv ? 84 : 60
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: ShellCorona.currentDisplayName
                color: FinnTheme.dark ? "#F2F4F8" : "#000000"
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.isTv ? 28 : 20
                font.weight: Font.Normal
            }
        }

        // Exact Figma Frosted Capsule Password Pill (84:593: 174x30, true semicircle r=height/2, #FFFFFFA6)
        Item {
            width: root.isTv ? 300 : (root.isMobile ? 240 : 174)
            height: root.isTv ? 46 : (root.isMobile ? 42 : 30)
            anchors.horizontalCenter: parent.horizontalCenter
            transform: Translate { x: shakeSpring.value }

            FinnSquircle {
                anchors.fill: parent
                radius: height / 2
                backdropBlur: true
                color: FinnTheme.dark ? FinnTheme.withAlpha("#262B36", 0.68) : "#A6FFFFFF"
                borderColor: passInput.activeFocus
                    ? FinnTheme.accent
                    : (FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000")
                borderWidth: passInput.activeFocus ? 1.5 : 0.5
            }

            Text {
                visible: passInput.text.length === 0
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: ShellCorona.verifyingUnlock ? "Checking..." : "Enter password"
                color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.85) : "#000000"
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.isTv ? 18 : 14
                font.weight: Font.Normal
            }

            TextInput {
                id: passInput
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: submitBtn.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                echoMode: TextInput.Password
                color: FinnTheme.dark ? "#FFFFFF" : "#000000"
                font.family: FinnTheme.fontFamily
                font.pixelSize: root.isTv ? 18 : 14
                font.weight: Font.Normal
                enabled: !ShellCorona.verifyingUnlock
                clip: true
                onAccepted: ShellCorona.attemptUnlock(text)
            }

            FinnIconButton {
                id: submitBtn
                iconName: "arrow-right"
                buttonSize: parent.height - 4
                iconSize: root.isTv ? 20 : 16
                radius: buttonSize / 2
                iconColor: FinnTheme.dark ? "#FFFFFF" : "#000000"
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                enabled: !ShellCorona.verifyingUnlock
                onClicked: ShellCorona.attemptUnlock(passInput.text)
            }
        }

        // Error feedback label (only visible on wrong password)
        Text {
            visible: ShellCorona.unlockError.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: ShellCorona.unlockError
            color: FinnTheme.danger
            font.family: FinnTheme.fontFamily
            font.pixelSize: root.isTv ? 16 : 12
        }

        Item { width: 1; height: ShellCorona.unlockError.length > 0 ? 0 : 4 }

        // Exact Figma Power Options Capsule Pill (84:618: 75x36, true semicircle r=height/2, #FFFFFFA6)
        Item {
            width: root.isTv ? 108 : 75
            height: root.isTv ? 48 : 36
            anchors.horizontalCenter: parent.horizontalCenter

            FinnSquircle {
                anchors.fill: parent
                radius: height / 2
                backdropBlur: true
                color: FinnTheme.dark ? FinnTheme.withAlpha("#262B36", 0.68) : "#A6FFFFFF"
                borderColor: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.10) : "#0D000000"
                borderWidth: 0.5
            }

            Row {
                anchors.centerIn: parent
                spacing: root.isTv ? 16 : 12

                FinnIconButton {
                    iconName: "restart"
                    buttonSize: root.isTv ? 34 : 24
                    iconSize: root.isTv ? 22 : 20
                    radius: buttonSize / 2
                    iconColor: FinnTheme.dark ? "#FFFFFF" : "#000000"
                    onClicked: ShellCorona.reboot()
                }

                FinnIconButton {
                    iconName: "power"
                    buttonSize: root.isTv ? 34 : 24
                    iconSize: root.isTv ? 22 : 20
                    radius: buttonSize / 2
                    iconColor: FinnTheme.dark ? "#FFFFFF" : "#000000"
                    onClicked: ShellCorona.powerOff()
                }
            }
        }
    }
}
