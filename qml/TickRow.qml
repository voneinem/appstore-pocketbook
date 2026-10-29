import QtQuick

// A consent the plan names, as its own tap control: a box and the words. It is never preselected (spec FR-015).
Item {
    id: root
    property string text
    property bool checked: false
    signal toggled()

    width: parent ? parent.width : 0
    implicitHeight: Math.max(Style.touchMin, label.implicitHeight + 40)
    height: implicitHeight

    Rectangle { anchors.fill: parent; color: Style.pressed; visible: tap.pressed }
    Icon {
        id: box
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        size: Style.iconRow
        source: root.checked ? "box-checked" : "box"
    }
    Text {
        id: label
        x: box.width + 24
        width: parent.width - x - 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        wrapMode: Text.Wrap
        font.pixelSize: Style.bodySize
        color: Style.ink
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        onClicked: { root.checked = !root.checked; root.toggled() }
    }
}
