import QtQuick

Item {
    id: root
    property url customSource: FinnTheme.wallpaperUrl

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: FinnTheme.dark ? "#0B0E14" : "#EDF4FD" }
            GradientStop { position: 0.5; color: FinnTheme.dark ? "#111827" : "#C9E0FA" }
            GradientStop { position: 1.0; color: FinnTheme.dark ? "#090D16" : "#76AEEB" }
        }
    }

    Image {
        anchors.fill: parent
        source: root.customSource
        fillMode: Image.PreserveAspectCrop
        smooth: true
        asynchronous: false
    }

    // Subtle dark-mode dim overlay so dark glass pops smoothly
    Rectangle {
        anchors.fill: parent
        color: FinnTheme.dark ? FinnTheme.withAlpha("#0B0E14", 0.36) : "transparent"
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: FinnTheme.motionTheme }
        }
    }
}
