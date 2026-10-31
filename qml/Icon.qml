import QtQuick

// A line-art icon from qml/icons/<source>.svg, drawn at size x size panel pixels. A missing name draws nothing and `ready` stays false.
Item {
    id: root
    property string source
    property int size: Style.iconTop
    readonly property bool ready: img.status === Image.Ready

    width: size
    height: size

    Image {
        id: img
        anchors.fill: parent
        source: root.source.length > 0 ? Qt.resolvedUrl("icons/" + root.source + ".svg") : ""
        sourceSize: Qt.size(root.size, root.size)
        smooth: true
        cache: true
    }
}
