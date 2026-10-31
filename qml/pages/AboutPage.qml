import QtQuick
import ".."

// About (FR-037): the version, the notice that PB Store is unofficial, which hosts it contacts, and the libraries it is built with.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"

    TopBar {
        id: bar
        title: qsTr("about.title")
        leftItems: [ IconButton { objectName: "back"; label: qsTr("about.back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
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
            Para { objectName: "aboutVersion"; font.bold: true; font.pixelSize: Style.rowTitleSize; text: qsTr("about.version").arg(store.version) }
            Para { objectName: "aboutNotice"; text: store.aboutNotice }
            Para { text: qsTr("about.description") }
            Para {
                objectName: "aboutAuthor"
                visible: store.licensesAvailable
                text: qsTr("about.copyright").arg(store.appLicense.copyright).arg(store.appLicense.license)
            }
            Para { visible: store.licensesAvailable; font.bold: true; text: qsTr("about.built_with") }
            Para { visible: !store.licensesAvailable; objectName: "licensesMissing"; text: qsTr("about.no_components") }
            Repeater {
                model: store.licenses
                delegate: Item {
                    objectName: "license-" + modelData.id
                    width: column.width
                    height: Math.max(Style.touchMin, entry.height + 24)
                    Rectangle { anchors.fill: parent; color: Style.pressed; visible: tap.pressed }
                    Column {
                        id: entry
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                        Text { width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink; text: modelData.name }
                        Text { width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink; text: modelData.license + " - " + modelData.purpose }
                    }
                    RowDivider { anchors.bottom: parent.bottom; width: parent.width }
                    MouseArea {
                        id: tap
                        anchors.fill: parent
                        onClicked: { store.loadLicense(modelData.id); page.nav.push(Qt.resolvedUrl("LicensesPage.qml"), {"nav": page.nav}) }
                    }
                }
            }
        }
    }
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { anchors.centerIn: parent; target: flick; onTurned: store.pageTurned() }
    }
}
