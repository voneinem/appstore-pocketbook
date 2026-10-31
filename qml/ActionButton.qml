import QtQuick

// A button with a word in a rounded outline (the footer buttons of dialogs and of the plan). At least 120 px high; a press fills light grey at
// once; a disabled button is drawn lighter and ignores taps. No animation.
Item {
    id: root
    property string text
    property bool primary: false
    signal clicked()

    implicitWidth: Math.max(300, word.implicitWidth + 60)
    implicitHeight: Style.touchMin
    width: implicitWidth
    height: implicitHeight
    opacity: enabled ? 1.0 : Style.disabledOpacity

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusField
        border.width: root.primary ? 4 : Style.ruleWidth
        border.color: Style.ink
        color: tap.pressed ? Style.pressed : Style.paper
    }
    Text {
        id: word
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Style.rowTitleSize
        font.bold: root.primary
        color: Style.ink
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
