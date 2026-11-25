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

    // Deep dark-mode scrim so dark liquid glass stays rich obsidian instead of washed-out gray
    Rectangle {
        anchors.fill: parent
        color: FinnTheme.dark ? FinnTheme.withAlpha("#090B10", 0.62) : "transparent"
        Behavior on color {
            enabled: !FinnTheme.reducedMotion
            ColorAnimation { duration: FinnTheme.motionTheme }
        }
    }
}
