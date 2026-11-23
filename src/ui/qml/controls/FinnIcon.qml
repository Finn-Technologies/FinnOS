import QtQuick

Item {
    id: root
    property string name: ""
    property color tint: FinnTheme.ink
    property int size: 20
    property bool monochrome: true

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Image {
        anchors.fill: parent
        source: root.name.length > 0
            ? FinnTheme.iconUrl(root.name, root.monochrome ? root.tint : "transparent", Math.max(16, Math.round(root.size * 2)))
            : ""
        sourceSize.width: Math.max(16, Math.round(root.size * 2))
        sourceSize.height: Math.max(16, Math.round(root.size * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
    }
}
