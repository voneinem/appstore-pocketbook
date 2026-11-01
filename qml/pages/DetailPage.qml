import QtQuick
import QtQuick.Controls
import ".."

// The detail of one app: the long description, license, author, source repository, package size, minimum firmware, the address the package
// comes from and the changelog link. The source and the changelog lines are links: a tap asks "Open this link?" with the whole address, and only Open
// hands it to the reader's browser (device.openUrl); the store itself never fetches or opens a link (spec FR-011, FR-043).
Item {
    id: page
    property var nav
    readonly property string pageKind: "list"
    readonly property var d: store.detail
    property string pendingLink: ""    // the address the question is about; empty: no question
    property bool linkFailed: false
    function askLink(url) { linkFailed = false; pendingLink = url }

    TopBar {
        id: bar
        title: page.d.name || ""
        leftItems: [ IconButton { objectName: "back"; label: qsTr("detail.back"); icon: "chevron-left"; onClicked: page.nav.pop() } ]
    }

    Flickable {
        id: flick
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: actions.top; leftMargin: Style.pad; rightMargin: Style.pad }
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
            component LinkLine: Item {
                id: link
                property string caption
                property string value
                visible: value.length > 0
                width: column.width
                height: Math.max(Style.touchMin, label.height + 12)
                Rectangle { anchors.fill: parent; color: Style.pressed; visible: linkTap.pressed }
                Text {
                    id: label
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Style.bodySize
                    font.underline: true
                    color: Style.ink
                    text: link.caption + ": " + link.value
                }
                MouseArea { id: linkTap; anchors.fill: parent; onClicked: page.askLink(link.value) }
            }
            // an app with an update shows the same badge as the list and both versions after it (feature 004); other states show their words
            Row {
                width: parent.width
                spacing: 12
                UpdateBadge { id: detailBadge; objectName: "detailBadge"; visible: page.d.stateWords === "Update available"; text: page.d.stateWords || "" }
                Text { objectName: "detailState"; width: parent.width - (detailBadge.visible ? detailBadge.width + parent.spacing : 0); font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink; wrapMode: Text.Wrap
                       text: detailBadge.visible ? "(" + page.d.installedVersion + " → " + page.d.offeredVersion + ")" : (page.d.stateWords || "") }
            }
            Text { visible: (page.d.notes || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.notes || "" }
            Text { visible: (page.d.longDescription || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.longDescription || "" }
            Line { objectName: "detailInstalled"; caption: qsTr("detail.installed_version"); value: page.d.installedVersion || "" }
            Line { objectName: "detailOffered"; caption: qsTr("detail.version_in_the_catalog"); value: page.d.offeredVersion || "" }
            Line { objectName: "detailLicense"; caption: qsTr("detail.license"); value: page.d.license || "" }
            Line { objectName: "detailAuthor"; caption: qsTr("detail.author"); value: page.d.author || "" }
            LinkLine { objectName: "detailSource"; caption: qsTr("detail.source"); value: page.d.sourceUrl || "" }
            Line { objectName: "detailSize"; caption: qsTr("detail.package_size"); value: page.d.packageSize || "" }
            Line { objectName: "detailMinFirmware"; caption: qsTr("detail.minimum_firmware"); value: page.d.minFirmware || "" }
            Line { objectName: "detailPackage"; caption: qsTr("detail.downloaded_from"); value: page.d.packageUrl || "" }
            LinkLine { objectName: "detailChangelog"; caption: qsTr("detail.changelog"); value: page.d.changelogUrl || "" }
            Text { visible: !(page.d.inCatalog); width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap
                   text: qsTr("detail.not_in_catalog") }
            Text { objectName: "linkFailed"; visible: page.linkFailed; width: parent.width; font.pixelSize: Style.bodySize; font.bold: true; color: Style.ink; wrapMode: Text.Wrap
                   text: qsTr("detail.link_failed") }
            Text { objectName: "storeNote"; visible: (page.d.storeNote || "").length > 0; width: parent.width; font.pixelSize: Style.bodySize; color: Style.ink; wrapMode: Text.Wrap; text: page.d.storeNote || "" }
        }
    }

    // the actions that fit the state (a repair pending, or an app that cannot be changed, has none)
    Row {
        id: actions
        objectName: "actions"
        anchors { left: parent.left; right: parent.right; bottom: footer.top; leftMargin: Style.pad; bottomMargin: 6 }
        spacing: Style.gap
        height: (page.d.actions || []).length > 0 ? Style.touchMin : 0
        Repeater {
            model: page.d.actions || []
            ActionButton {
                objectName: "action" + modelData
                text: modelData
                primary: index === 0
                onClicked: {
                    store.startAction(modelData, page.d.appId)
                    page.nav.push(Qt.resolvedUrl("OperationPage.qml"), {"nav": page.nav})
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
    // "Open this link?": the whole address, then Cancel or Open (the reader's browser is started only by Open)
    Item {
        id: linkOverlay
        anchors.fill: parent
        z: 10
        visible: page.pendingLink.length > 0
        MouseArea { anchors.fill: parent }   // nothing behind the question can be tapped
        Rectangle {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Style.pad }
            height: askColumn.height + 2 * Style.pad
            color: Style.paper
            radius: Style.radiusDialog
            border.width: Style.ruleWidth
            border.color: Style.ink
            Column {
                id: askColumn
                x: Style.pad
                y: Style.pad
                width: parent.width - 2 * Style.pad
                spacing: 14
                Text { objectName: "linkAsk"; width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink; text: qsTr("detail.link_title") }
                Text { objectName: "linkAddress"; width: parent.width; wrapMode: Text.WrapAnywhere; font.pixelSize: Style.bodySize; color: Style.ink; text: page.pendingLink }
                Text { width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink; text: qsTr("detail.link_hint") }
                Row {
                    spacing: Style.gap
                    ActionButton { objectName: "linkCancel"; text: qsTr("detail.cancel"); onClicked: page.pendingLink = "" }
                    ActionButton {
                        objectName: "linkOpen"
                        text: qsTr("detail.open")
                        primary: true
                        onClicked: {
                            const url = page.pendingLink
                            page.pendingLink = ""
                            page.linkFailed = !device.openUrl(url)
                        }
                    }
                }
            }
        }
    }
}
