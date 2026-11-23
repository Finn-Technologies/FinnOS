import QtQuick

Rectangle {
    id: root
    property string text: ""
    property string tone: "neutral" // neutral | accent | success | warning | danger

    implicitWidth: lbl.implicitWidth + 16
    implicitHeight: 22
    radius: height / 2

    color: {
        if (tone === "accent") return FinnTheme.withAlpha(FinnTheme.accent, 0.16)
        if (tone === "success") return FinnTheme.withAlpha(FinnTheme.success, 0.16)
        if (tone === "warning") return FinnTheme.withAlpha(FinnTheme.warning, 0.16)
        if (tone === "danger") return FinnTheme.withAlpha(FinnTheme.danger, 0.16)
        return FinnTheme.surfaceInput
    }
    border.color: {
        if (tone === "accent") return FinnTheme.withAlpha(FinnTheme.accent, 0.35)
        if (tone === "success") return FinnTheme.withAlpha(FinnTheme.success, 0.35)
        if (tone === "warning") return FinnTheme.withAlpha(FinnTheme.warning, 0.35)
        if (tone === "danger") return FinnTheme.withAlpha(FinnTheme.danger, 0.35)
        return FinnTheme.hair
    }
    border.width: 0.8

    Text {
        id: lbl
        anchors.centerIn: parent
        text: root.text
        font.family: FinnTheme.fontFamily
        font.pixelSize: 11
        font.weight: Font.Medium
        color: {
            if (root.tone === "accent") return FinnTheme.on
            if (root.tone === "success") return FinnTheme.success
            if (root.tone === "warning") return FinnTheme.warning
            if (root.tone === "danger") return FinnTheme.danger
            return FinnTheme.dim
        }
    }
}
