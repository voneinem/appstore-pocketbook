import QtQuick

// An icon control with its word underneath (spec FR-009: every icon control in the top bar also shows a short text label). A press fills
// light grey at once; a disabled control is drawn lighter and ignores taps. No animation.
Item {
    id: root
    property string label
    property string icon
    signal clicked()

    implicitWidth: Math.max(Style.touchMin, wordText.implicitWidth + 24)
    implicitHeight: Style.touchMin
    width: implicitWidth
    height: implicitHeight
    opacity: enabled ? 1.0 : Style.disabledOpacity

    Rectangle { anchors.fill: parent; radius: Style.radiusField; color: Style.pressed; visible: tap.pressed && root.enabled }
    Icon {
        id: glyph
        anchors.horizontalCenter: parent.horizontalCenter
        y: 6
        size: Style.iconTop - 8
        source: root.icon
    }
    Text {
        id: wordText
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 4
        text: root.label
        font.pixelSize: Style.captionSize
        color: Style.ink
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
