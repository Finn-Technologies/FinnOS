import QtQuick

Item {
    id: root
    property string name: ""
    property string fallbackName: ""
    property color tint: FinnTheme.ink
    property int size: 20
    property bool monochrome: true

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Image {
        id: img
        anchors.fill: parent
        readonly property string effectiveName: (status === Image.Error && root.fallbackName.length > 0)
            ? root.fallbackName
            : root.name
        source: root.name.length > 0
            ? FinnTheme.iconUrl(root.name, root.monochrome ? root.tint : "#00000000", Math.max(16, Math.round(root.size * 2)))
            : ""
        sourceSize.width: Math.max(16, Math.round(root.size * 2))
        sourceSize.height: Math.max(16, Math.round(root.size * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: root.name.indexOf("http://") === 0 || root.name.indexOf("https://") === 0
        visible: status !== Image.Error
    }

    Image {
        anchors.fill: parent
        visible: img.status === Image.Error && root.fallbackName.length > 0
        source: (img.status === Image.Error && root.fallbackName.length > 0)
            ? FinnTheme.iconUrl(root.fallbackName, root.monochrome ? root.tint : "#00000000", Math.max(16, Math.round(root.size * 2)))
            : ""
        sourceSize.width: Math.max(16, Math.round(root.size * 2))
        sourceSize.height: Math.max(16, Math.round(root.size * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
