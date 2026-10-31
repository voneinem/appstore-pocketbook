import QtQuick
import ".."

// The log (FR-039): the last 50 lines of the store's log. It holds what was done, app ids, versions, results and rule ids, no personal data.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"

    TopBar {
        id: bar
        title: qsTr("log.log")
        leftItems: [ IconButton { objectName: "back"; label: qsTr("log.back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
    }
    Flickable {
        id: flick
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: footer.top; leftMargin: Style.pad; rightMargin: Style.pad }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        contentWidth: width
        contentHeight: logText.height + Style.pad
        Text { id: logText; objectName: "logText"; width: flick.width; topPadding: 14; wrapMode: Text.WrapAnywhere; font.pixelSize: Style.bodySize; color: Style.ink; text: store.logText }
    }
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { anchors.centerIn: parent; target: flick; onTurned: store.pageTurned() }
    }
}
