import QtQuick
import QtQuick.Controls
import ".."

// The detail of one app: the long description, license, author, source repository, package size, minimum firmware, the address the package
// comes from and the changelog link, all as text (a link is never opened or fetched, spec FR-011, FR-043).
Item {
    id: page
    property var nav
    readonly property string pageKind: "list"
    readonly property var d: store.detail

    TopBar {
        id: bar
        title: page.d.name || ""
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
            spacing: 10
            topPadding: 14
            component Line: Text {
                property string caption
                property string value
                visible: value.length > 0
                width: column.width
                wrapMode: Text.Wrap
                font.pixelSize: Style.bodySize
                color: Style.ink
                text: caption + ": " + value
            }
            Text { objectName: "detailState"; width: parent.width; font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink; wrapMode: Text.Wrap
                   text: (page.d.stateWords || "") + (page.d.offeredVersion && page.d.stateWords === "Update available" ? " (" + page.d.installedVersion + " → " + page.d.offeredVersion + ")" : "") }
            Text { visible: (page.d.notes || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.notes || "" }
            Text { visible: (page.d.longDescription || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.longDescription || "" }
            Line { objectName: "detailInstalled"; caption: qsTr("Installed version"); value: page.d.installedVersion || "" }
            Line { objectName: "detailOffered"; caption: qsTr("Version in the catalog"); value: page.d.offeredVersion || "" }
            Line { objectName: "detailLicense"; caption: qsTr("License"); value: page.d.license || "" }
            Line { objectName: "detailAuthor"; caption: qsTr("Author"); value: page.d.author || "" }
            Line { objectName: "detailSource"; caption: qsTr("Source"); value: page.d.sourceUrl || "" }
            Line { objectName: "detailSize"; caption: qsTr("Package size"); value: page.d.packageSize || "" }
            Line { objectName: "detailMinFirmware"; caption: qsTr("Minimum firmware"); value: page.d.minFirmware || "" }
            Line { objectName: "detailPackage"; caption: qsTr("Downloaded from"); value: page.d.packageUrl || "" }
            Line { objectName: "detailChangelog"; caption: qsTr("Changelog"); value: page.d.changelogUrl || "" }
            Text { visible: !(page.d.inCatalog); width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap
                   text: qsTr("This app is not in the catalog. Nothing is changed because of that.") }
            Text { objectName: "storeNote"; visible: (page.d.storeNote || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.storeNote || "" }
            Text { objectName: "actionsNote"; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap
                   text: qsTr("Install, update, remove and adopt are not part of this version of PB Store: it only browses the catalog.") }
        }
    }

    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { anchors.centerIn: parent; target: flick; onTurned: store.pageTurned() }
    }
}
