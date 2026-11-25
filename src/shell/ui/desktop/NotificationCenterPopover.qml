import QtQuick
import FinnUI 1.0

// Notification Center Popover (opened by clicking the Date & Time pill on the dock):
// - Top: Live date, time, and real current-month calendar grid
// - Middle: Live notifications feed from ShellNotifications with per-item dismiss & Clear all
// - Bottom: 50px frosted footer bar matching Control Center & Home Menu
Item {
    id: root
    property bool shown: false

    width: 380
    height: 440
    visible: openSpring.value > 0.01
    opacity: Math.max(0.0, Math.min(1.0, openSpring.value))
    transform: Translate { y: (1.0 - openSpring.value) * 14 }
    scale: 0.97 + 0.03 * openSpring.value

    FinnSpring {
        id: openSpring
        preset: "snappy"
    }
    Component.onCompleted: {
        openSpring.snap(shown ? 1.0 : 0.0)
        refreshCalendar()
    }
    onShownChanged: {
        if (shown) refreshCalendar()
        openSpring.target = shown ? 1.0 : 0.0
    }

    property string monthTitle: ""
    property int todayDay: 1
    property var calendarCells: []

    function refreshCalendar() {
        const now = new Date()
        const year = now.getFullYear()
        const month = now.getMonth()
        const months = [
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        ]
        monthTitle = months[month] + " " + year
        todayDay = now.getDate()

        // Monday-first grid (0 = Mon ... 6 = Sun)
        const firstDay = new Date(year, month, 1)
        const startOffset = (firstDay.getDay() + 6) % 7
        const daysInMonth = new Date(year, month + 1, 0).getDate()
        const cells = []
        for (let i = 0; i < startOffset; ++i) {
            cells.push({ day: 0, isToday: false })
        }
        for (let d = 1; d <= daysInMonth; ++d) {
            cells.push({ day: d, isToday: d === todayDay })
        }
        while (cells.length % 7 !== 0) {
            cells.push({ day: 0, isToday: false })
        }
        calendarCells = cells
    }

    FinnGlassPanel {
        anchors.fill: parent
        radius: FinnTheme.radiusLg
        shadow: false
        backdropBlur: !ShellCorona.waylandSession

        MouseArea { anchors.fill: parent }

        // Main content area above 50px footer
        Column {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footerBar.top
            anchors.margins: 14
            spacing: 12

            // ---- Date & Live Clock Header ----
            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: parent.width - headerClockText.implicitWidth - 8
                    spacing: 2

                    Text {
                        text: ShellCorona.lockDateText
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 15
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Text {
                        text: root.monthTitle
                        color: FinnTheme.dim
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 12
                    }
                }

                Text {
                    id: headerClockText
                    text: ShellCorona.dockTimeText
                    color: FinnTheme.ink
                    font.family: FinnTheme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.Normal
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ---- Mini Calendar Card ----
            FinnSquircle {
                width: parent.width
                height: calCol.implicitHeight + 16
                radius: FinnTheme.radiusMd
                color: FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.64 : 0.72)
                borderWidth: 0
                specularRim: false

                Column {
                    id: calCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 4

                    // Weekday labels (Mon .. Sun)
                    Row {
                        width: parent.width
                        Repeater {
                            model: ["M", "T", "W", "T", "F", "S", "S"]
                            delegate: Text {
                                width: Math.floor(calCol.width / 7)
                                height: 18
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: modelData
                                color: FinnTheme.dim
                                font.family: FinnTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }
                        }
                    }

                    // Calendar days grid
                    Grid {
                        width: parent.width
                        columns: 7
                        rowSpacing: 2

                        Repeater {
                            model: root.calendarCells
                            delegate: Item {
                                width: Math.floor(calCol.width / 7)
                                height: 22

                                Rectangle {
                                    visible: modelData.isToday === true
                                    width: 22
                                    height: 22
                                    radius: 11
                                    anchors.centerIn: parent
                                    color: FinnTheme.accent
                                }

                                Text {
                                    visible: modelData.day > 0
                                    anchors.centerIn: parent
                                    text: modelData.day > 0 ? String(modelData.day) : ""
                                    color: modelData.isToday ? "#FFFFFF" : FinnTheme.ink
                                    font.family: FinnTheme.fontFamily
                                    font.pixelSize: 11
                                    font.weight: modelData.isToday ? Font.Medium : Font.Normal
                                }
                            }
                        }
                    }
                }
            }

            // ---- Notifications Header Row ----
            Item {
                width: parent.width
                height: 22

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Text {
                        text: "Notifications"
                        color: FinnTheme.ink
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Rectangle {
                        visible: ShellNotifications.count > 0
                        width: Math.max(18, countLbl.implicitWidth + 10)
                        height: 18
                        radius: 9
                        color: FinnTheme.accent
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: countLbl
                            anchors.centerIn: parent
                            text: String(ShellNotifications.count)
                            color: "#FFFFFF"
                            font.family: FinnTheme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }
                    }
                }

                Item {
                    visible: ShellNotifications.count > 0
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: clearTxt.implicitWidth + 14
                    height: 22

                    FinnSquircle {
                        anchors.fill: parent
                        radius: 6
                        color: clearMa.containsMouse ? FinnTheme.surfaceHover : "transparent"
                        borderWidth: 0
                    }

                    Text {
                        id: clearTxt
                        anchors.centerIn: parent
                        text: "Clear all"
                        color: FinnTheme.accent
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ShellNotifications.clearAll()
                    }
                }
            }

            // ---- Notifications List / Empty State ----
            Item {
                width: parent.width
                height: Math.max(60, parent.height - y)
                clip: true

                Column {
                    visible: ShellNotifications.count === 0
                    anchors.centerIn: parent
                    spacing: 6

                    FinnIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "info"
                        size: 24
                        tint: FinnTheme.mute
                        opacity: 0.6
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No notifications"
                        color: FinnTheme.dim
                        font.family: FinnTheme.fontFamily
                        font.pixelSize: 13
                    }
                }

                Flickable {
                    visible: ShellNotifications.count > 0
                    anchors.fill: parent
                    contentHeight: notifCol.implicitHeight
                    boundsBehavior: Flickable.DragOverBounds
                    clip: true

                    Column {
                        id: notifCol
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: ShellNotifications
                            delegate: FinnSquircle {
                                width: notifCol.width
                                height: 64
                                radius: FinnTheme.radiusMd
                                color: FinnTheme.withAlpha(FinnTheme.tile, FinnTheme.dark ? 0.72 : 0.78)
                                borderWidth: 0
                                specularRim: false

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    spacing: 10

                                    Rectangle {
                                        width: 34
                                        height: 34
                                        radius: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: FinnTheme.dark ? FinnTheme.withAlpha("#FFFFFF", 0.12) : FinnTheme.withAlpha("#000000", 0.06)

                                        FinnIcon {
                                            anchors.centerIn: parent
                                            name: icon
                                            size: 22
                                            monochrome: false
                                        }
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.max(80, parent.width - 96)
                                        spacing: 2

                                        Row {
                                            width: parent.width
                                            spacing: 6

                                            Text {
                                                text: summary
                                                color: FinnTheme.ink
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                                width: parent.width - tsText.implicitWidth - 6
                                            }

                                            Text {
                                                id: tsText
                                                text: timeLabel
                                                color: FinnTheme.mute
                                                font.family: FinnTheme.fontFamily
                                                font.pixelSize: 11
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        Text {
                                            text: body
                                            color: FinnTheme.dim
                                            font.family: FinnTheme.fontFamily
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            maximumLineCount: 2
                                            wrapMode: Text.WordWrap
                                            width: parent.width
                                        }
                                    }

                                    FinnIconButton {
                                        iconName: "x"
                                        buttonSize: 24
                                        iconSize: 12
                                        radius: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        onClicked: ShellNotifications.dismiss(notifId)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- 50px Frosted Footer Bar ----
        Rectangle {
            id: footerBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 50
            color: FinnTheme.glassFooter

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 15
                anchors.verticalCenter: parent.verticalCenter
                text: ShellNotifications.count === 0
                    ? "All caught up"
                    : (ShellNotifications.count === 1 ? "1 notification" : (ShellNotifications.count + " notifications"))
                color: FinnTheme.dim
                font.family: FinnTheme.fontFamily
                font.pixelSize: 13
            }

            FinnIconButton {
                iconName: "settings-03"
                buttonSize: 32
                iconSize: 20
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                onClicked: ShellCorona.openSettings("notifications")
            }
        }
    }
}
