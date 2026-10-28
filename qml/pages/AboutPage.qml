import QtQuick
import ".."

// About (FR-037): the version, the notice that PB Store is unofficial, which hosts it contacts, and the libraries it is built with.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"

    TopBar {
        id: bar
        title: qsTr("About")
        leftItems: [ IconButton { objectName: "back"; label: qsTr("Back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
    }
    Flickable {
        id: flick
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: footer.top; leftMargin: Style.pad; rightMargin: Style.pad }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        contentWidth: width
        contentHeight: column.height + Style.pad
        Column {
            id: column
            width: flick.width
            spacing: 16
            topPadding: 14
            component Para: Text {
                width: column.width
                wrapMode: Text.Wrap
                font.pixelSize: Style.bodySize
                color: Style.ink
            }
            Para { objectName: "aboutVersion"; font.bold: true; font.pixelSize: Style.rowTitleSize; text: qsTr("PB Store %1").arg(store.version) }
            Para { objectName: "aboutUnofficial"; text: qsTr("PB Store is unofficial and is not made, endorsed or supported by PocketBook.") }
            Para { text: qsTr("PB Store browses the signed catalog of apps for the PocketBook InkPad One. It contacts the catalog host and the host of the package address of an app, and nothing else; it does nothing until you tap a button, and it sends no identifier.") }
            Para { text: qsTr("Author: Volker von Einem. MIT license.") }
            Para { font.bold: true; text: qsTr("Built with") }
            Para { text: qsTr("Qt 6 (LGPL 3), libcurl 8.18.0 (curl license), OpenSSL 3 (Apache 2.0), zlib (zlib license), miniz (MIT), the Mozilla CA bundle (MPL 2.0).") }
        }
    }
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { anchors.centerIn: parent; target: flick; onTurned: store.pageTurned() }
    }
}
