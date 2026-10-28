import QtQuick

// The bar at the top of every page: the title at the left (capitals), the icon controls at the right, a hairline underneath.
Item {
    id: root
    property string title
    property alias leftItems: leftRow.data
    property alias rightItems: rightRow.data

    width: parent ? parent.width : 0
    height: Style.barHeight

    Row {
        id: leftRow
        x: 14
        y: (Style.barHeight - Style.touchMin) / 2
        spacing: Style.gap
    }
    Row {
        id: rightRow
        x: root.width - width - 14
        y: (Style.barHeight - Style.touchMin) / 2
        spacing: Style.gap
    }
    Text {
        objectName: "topBarTitle"
        x: leftRow.width > 0 ? leftRow.width + 28 : Style.pad
        height: Style.barHeight
        width: Math.max(0, rightRow.x - x - 10)
        text: root.title
        elide: Text.ElideRight
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
