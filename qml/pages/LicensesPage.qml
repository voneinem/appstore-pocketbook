import QtQuick
import ".."

// The license text of one component (FR-037), opened from the About page: the name in the bar, then the text, paged with the page buttons.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"

    TopBar {
        id: bar
        title: store.licenseTitle
        leftItems: [ IconButton { objectName: "back"; label: qsTr("licenses.back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
    }
    Flickable {
        id: flick
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: footer.top; leftMargin: Style.pad; rightMargin: Style.pad }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        contentWidth: width
        contentHeight: licenseBody.height + Style.pad
        Text { id: licenseBody; objectName: "licenseText"; width: flick.width; topPadding: 14; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink; text: store.licenseText }
    }
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { anchors.centerIn: parent; target: flick; onTurned: store.pageTurned() }
    }
}
