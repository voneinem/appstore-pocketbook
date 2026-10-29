import QtQuick
import ".."

// Settings (FR-038): facts only. There is no switch that weakens a check, no catalog address to edit and no custom CA file.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"
    readonly property var s: store.settings

    TopBar {
        id: bar
        title: qsTr("Settings")
        leftItems: [ IconButton { objectName: "back"; label: qsTr("Back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
        rightItems: [ IconButton { objectName: "log"; label: qsTr("Log"); icon: "info"; onClicked: { store.loadLog(); page.nav.push(Qt.resolvedUrl("LogPage.qml"), {"nav": page.nav}) } } ]
    }
    Column {
        anchors { top: bar.bottom; left: parent.left; right: parent.right; margins: Style.pad }
        topPadding: 14
        spacing: 14
        component Fact: Text {
            property string caption
            property string value
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Style.bodySize
            color: Style.ink
            text: caption + ": " + value
        }
        Fact { objectName: "setCatalog"; caption: qsTr("Catalog address (cannot be changed)"); value: page.s.catalogUrl || "" }
        Fact { objectName: "setRefreshed"; caption: qsTr("Catalog last updated"); value: page.s.refreshedAt || "" }
        Fact { objectName: "setGenerated"; caption: qsTr("Catalog dated"); value: page.s.generatedAt || "" }
        Fact { objectName: "setTrust"; caption: qsTr("Certificates"); value: page.s.trustSource || "" }
        Fact { objectName: "setCustomCa"; caption: qsTr("Custom CA file"); value: page.s.customCa || "" }
        Fact { objectName: "setReader"; caption: qsTr("Reader"); value: page.s.readerIdentity || "" }
        Fact { objectName: "setLog"; caption: qsTr("Log"); value: page.s.logLocation || "" }
        Fact { objectName: "setStorage"; caption: qsTr("Free storage"); value: page.s.freeSpace || "" }
        Fact { objectName: "setBattery"; caption: qsTr("Battery"); value: page.s.battery || "" }
    }
}
