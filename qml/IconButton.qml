import QtQuick

// An icon control, drawn alone as in the reader's own apps (PB Notes). `label` is its name for the tests and for assistive software; it is
// not drawn. A press fills light grey at once; a disabled control is drawn lighter and ignores taps. No animation.
Item {
    id: root
    property string label
    property string icon
    signal clicked()

    implicitWidth: Style.touchMin
    implicitHeight: Style.touchMin
    width: implicitWidth
    height: implicitHeight
    opacity: enabled ? 1.0 : Style.disabledOpacity
    Accessible.role: Accessible.Button
    Accessible.name: root.label

    Rectangle { anchors.fill: parent; radius: Style.radiusField; color: Style.pressed; visible: tap.pressed && root.enabled }
    Icon {
        anchors.centerIn: parent
        size: Style.iconTop
        source: root.icon
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
