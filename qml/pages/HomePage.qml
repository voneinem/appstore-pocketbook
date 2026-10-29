import QtQuick
import QtQuick.Controls
import ".."

// The list of apps (story 1): the catalog line, the search, the apps paged by whole pages, and a message when something failed.
Item {
    id: page
    property var nav
    readonly property string pageKind: "list"
    property bool searching: false
    // an interrupted operation is shown before anything else (story 6)
    Component.onCompleted: if (store.repairPending) page.nav.push(Qt.resolvedUrl("RepairPage.qml"), {"nav": page.nav})

    TopBar {
        id: bar
        title: qsTr("PB Store")
        // Home leaves the store: the house icon at the top left of the main screen, as in the reader's own apps
        leftItems: [
            IconButton {
                objectName: "home"
                label: qsTr("Home")
                icon: "home"
                onClicked: device.quit()
            }
        ]
        rightItems: [
            IconButton {
                objectName: "refresh"
                label: qsTr("Refresh")
                icon: "refresh"
                enabled: !store.busy
                onClicked: store.refresh()
            },
            IconButton {
                objectName: "updateAll"
                visible: store.canUpdateAll
                label: qsTr("Update all")
                icon: "update-all"
                enabled: !store.busy
                onClicked: {
                    store.startUpdateAll()
                    page.nav.push(Qt.resolvedUrl("OperationPage.qml"), {"nav": page.nav})
                }
            },
            IconButton {
                objectName: "search"
                label: qsTr("Search")
                icon: "search"
                onClicked: page.searching = !page.searching
            },
            IconButton {
                objectName: "settings"
                label: qsTr("Settings")
                icon: "gear"
                onClicked: {
                    store.loadSettings()
                    page.nav.push(Qt.resolvedUrl("SettingsPage.qml"), {"nav": page.nav})
                }
            },
            IconButton {
                objectName: "about"
                label: qsTr("About")
                icon: "info"
                onClicked: page.nav.push(Qt.resolvedUrl("AboutPage.qml"), {"nav": page.nav})
            }
        ]
    }

    // a failure: what happened, whether the reader was changed, what to do (three parts, one OK button)
    Rectangle {
        id: message
        objectName: "message"
        visible: store.messageVisible
        anchors { top: bar.bottom; left: parent.left; right: parent.right; margins: Style.pad }
        height: visible ? messageColumn.height + 2 * Style.pad : 0
        color: Style.paper
        radius: Style.radiusDialog
        border.width: Style.ruleWidth
        border.color: Style.ink
        Column {
            id: messageColumn
            x: Style.pad
            y: Style.pad
            width: parent.width - 2 * Style.pad
            spacing: 14
            Text { objectName: "messageTitle"; width: parent.width; text: store.messageTitle; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; font.bold: true; color: Style.ink }
            Text { objectName: "messageReader"; width: parent.width; text: store.messageReader; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
            Text { objectName: "messageTodo"; width: parent.width; text: store.messageTodo; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
            Item {
                width: parent.width
                height: Style.touchMin
                Rectangle {
                    anchors.right: parent.right
                    width: 260
                    height: parent.height
                    radius: Style.radiusField
                    border.width: Style.ruleWidth
                    border.color: Style.ink
                    color: okTap.pressed ? Style.pressed : Style.paper
                    Text { anchors.centerIn: parent; text: qsTr("OK"); font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink }
                    MouseArea { id: okTap; objectName: "messageOk"; anchors.fill: parent; onClicked: store.clearMessage() }
                }
            }
        }
    }

    Column {
        id: info
        anchors { top: message.bottom; topMargin: message.visible ? Style.gap : 0; left: parent.left; right: parent.right; leftMargin: Style.pad; rightMargin: Style.pad }
        spacing: 8
        Text { objectName: "catalogLine"; width: parent.width; text: store.catalogLine; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        Text { objectName: "notice"; visible: store.notice.length > 0; width: parent.width; text: store.notice; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; font.bold: true; color: Style.ink }
        Text { objectName: "progress"; visible: store.busy; width: parent.width; text: store.progressText; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        // the search field: applied when the user confirms it on the reader's keyboard, not at each letter
        Row {
            visible: page.searching
            width: parent.width
            spacing: Style.gap
            TextField {
                id: searchField
                objectName: "searchField"
                width: parent.width - clearButton.width - Style.gap
                height: Style.touchMin
                font.pixelSize: Style.bodySize
                color: Style.ink
                placeholderText: qsTr("Name of an app")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                verticalAlignment: TextInput.AlignVCenter
                background: Rectangle { color: Style.paper; radius: Style.radiusField; border.width: Style.ruleWidth; border.color: Style.ink }
                onAccepted: store.search(text)
            }
            IconButton {
                id: clearButton
                objectName: "searchClear"
                label: qsTr("Clear")
                icon: "close"
                onClicked: { searchField.text = ""; store.search("") }
            }
        }
        Text { objectName: "searchSummary"; visible: text.length > 0; width: parent.width; text: store.searchSummary; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
    }

    ListView {
        id: list
        objectName: "appList"
        anchors { top: info.bottom; topMargin: Style.gap; left: parent.left; right: parent.right; bottom: footer.top }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false          // by whole pages with the buttons: no kinetic scrolling, nothing smeared on e-ink
        model: store.model
        delegate: Item {
            id: row
            objectName: "appRow"
            width: list.width
            height: Style.rowHeight
            Rectangle { anchors.fill: parent; color: Style.pressed; visible: rowTap.pressed }
            Icon { id: stateIcon; x: Style.pad; anchors.verticalCenter: parent.verticalCenter; size: Style.iconRow; source: model.stateIcon }
            Column {
                x: Style.pad + Style.iconRow + 24
                width: parent.width - x - Style.pad
                anchors.verticalCenter: parent.verticalCenter
                Item {
                    width: parent.width
                    height: nameText.implicitHeight
                    Text { id: nameText; objectName: "rowName"; width: parent.width - versionText.width - 16; text: model.name; elide: Text.ElideRight; font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink }
                    Text { id: versionText; anchors.right: parent.right; text: model.version; font.pixelSize: Style.bodySize; color: Style.ink }
                }
                Text { width: parent.width; text: model.shortDescription; elide: Text.ElideRight; font.pixelSize: Style.bodySize; color: Style.ink; visible: text.length > 0 }
                Text {
                    objectName: "rowState"
                    width: parent.width
                    text: model.stateWords + (model.offeredVersion.length > 0 && model.stateKey === "update-available" ? " (" + model.installedVersion + " → " + model.offeredVersion + ")" : "") + (model.notes.length > 0 ? " · " + model.notes : "")
                    elide: Text.ElideRight
                    font.pixelSize: Style.bodySize
                    color: Style.ink
                }
            }
            RowDivider { anchors.bottom: parent.bottom; x: Style.pad; width: parent.width - 2 * Style.pad }
            MouseArea {
                id: rowTap
                anchors.fill: parent
                onClicked: { store.select(model.appId); page.nav.push(Qt.resolvedUrl("DetailPage.qml"), {"nav": page.nav}) }
            }
        }
    }

    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight
        PageButtons { id: pages; objectName: "pageButtons"; anchors.centerIn: parent; target: list; snap: Style.rowHeight; onTurned: store.pageTurned() }
    }
}
