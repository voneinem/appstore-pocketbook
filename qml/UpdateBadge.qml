import QtQuick

// The mark of an app with an update (feature 004): white bold words on a black rounded rectangle. It works on the black-and-white panel
// without colour, and the state stays in words (spec FR-002). Not tappable; no animation.
Item {
    id: root
    property string text

    implicitWidth: word.implicitWidth + 28
    implicitHeight: word.implicitHeight + 8
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusField
        color: Style.ink
    }
    Text {
        id: word
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Style.bodySize
        font.bold: true
        color: Style.paper
    }
}
