import QtQuick

// The bar at the top of every page, laid out as in PB Notes: the icon controls at the left (Home or Back, a menu), the title in capitals
// in the middle, the icon controls at the right, a hairline underneath. The controls carry no words.
Item {
    id: root
    property string title
    property alias leftItems: leftRow.data
    property alias rightItems: rightRow.data

    width: parent ? parent.width : 0
    height: Style.barHeight

    Row {
        id: leftRow
        x: 8
        y: (Style.barHeight - Style.touchMin) / 2
    }
    Row {
        id: rightRow
        x: root.width - width - 8
        y: (Style.barHeight - Style.touchMin) / 2
    }
    Text {
        objectName: "topBarTitle"
        readonly property real side: Math.max(leftRow.width, rightRow.width) + 24
        x: side
        height: Style.barHeight
        width: Math.max(0, root.width - 2 * side)
        text: root.title
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Style.titleSize
        font.capitalization: Font.AllUppercase
        color: Style.ink
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Style.hairlineWidth
        color: Style.hairline
    }
}
