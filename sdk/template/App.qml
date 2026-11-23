import QtQuick
import FinnUI 1.0

// Adaptive native FinnOS app built with FinnApplicationWindow.
FinnApplicationWindow {
    id: win
    width: FinnLaunchWidth > 0 ? FinnLaunchWidth : 760
    height: FinnLaunchHeight > 0 ? FinnLaunchHeight : 480
    appTitle: "Finn Counter"
    pageTitle: activeNavId === "home" ? "Counter" : "About"
    activeNavId: "home"
    mobileDetailOpen: true

    navItems: [
        { id: "home", label: "Counter", icon: "compass", badgeColor: "#007BFF", section: "app" },
        { id: "about", label: "About", icon: "info", badgeColor: "#6366F1", section: "app" }
    ]

    Column {
        visible: win.activeNavId === "home"
        anchors.fill: parent
        spacing: 14

        FinnHeroCard {
            width: parent.width
            title: "Count: " + Counter.count
            subtitle: "Active form factor: " + win.formFactor
            tvMode: win.isTv
            actionText: "Increment"
            onActionClicked: Counter.increment()
        }

        Row {
            spacing: 10
            FinnButton {
                text: "Add +1"
                variant: "primary"
                tvMode: win.isTv
                onClicked: Counter.increment()
            }
            FinnButton {
                text: "Reset"
                variant: "secondary"
                tvMode: win.isTv
                onClicked: {
                    Counter.reset()
                    win.showToast("Counter reset", "info")
                }
            }
        }
    }

    FinnCard {
        visible: win.activeNavId === "about"
        width: parent.width
        implicitHeight: 96
        FinnSettingRow {
            anchors.fill: parent
            anchors.margins: 8
            title: "FinnSDK Native Template"
            subtitle: "Built with C++20, Qt 6, and FinnUI adaptive components."
            iconName: "info"
            tvMode: win.isTv
        }
    }
}
