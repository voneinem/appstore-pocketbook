import QtQuick
import QtQuick.Controls
import ".."

// The list of apps (story 1): the catalog line, the search, the apps paged by whole pages, and a message when something failed.
Item {
    id: page
    property var nav
    readonly property string pageKind: "list"
    property bool searching: false
    // an interrupted operation is shown before anything else (story 6): Main.qml pushes its page on top of this one

    property bool menuOpen: false

    TopBar {
        id: bar
        title: qsTr("home.pb_store")
        // Home leaves the store: the house icon at the top left of the main screen, as in the reader's own apps. The menu beside it holds
        // the rarer controls; Refresh and Search stay in reach at the right (the look of PB Notes).
        leftItems: [
            IconButton {
                objectName: "home"
                label: qsTr("home.home")
                icon: "home"
                onClicked: { store.leaveAndWait(); device.quit() }
            },
            IconButton {
                objectName: "menu"
                label: qsTr("home.menu")
                icon: "menu"
                onClicked: page.menuOpen = !page.menuOpen
            }
        ]
        rightItems: [
            IconButton {
                objectName: "refresh"
                label: qsTr("home.refresh")
                icon: "refresh"
                enabled: !store.busy
                onClicked: { page.menuOpen = false; store.refresh() }
            },
            IconButton {
                objectName: "search"
                label: qsTr("home.search")
                icon: "search"
                onClicked: { page.menuOpen = false; page.searching = !page.searching }
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
                    Text { anchors.centerIn: parent; text: qsTr("home.ok"); font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink }
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
                placeholderText: qsTr("home.name_of_an_app")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                verticalAlignment: TextInput.AlignVCenter
                background: Rectangle { color: Style.paper; radius: Style.radiusField; border.width: Style.ruleWidth; border.color: Style.ink }
                onAccepted: store.search(text)
            }
            IconButton {
                id: clearButton
                objectName: "searchClear"
                label: qsTr("home.clear")
                icon: "close"
                onClicked: { searchField.text = ""; store.search("") }
            }
        }
        Text { objectName: "searchSummary"; visible: text.length > 0; width: parent.width; text: store.searchSummary; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
    }

    ListView {
        id: list
        objectName: "appList"
        anchors { top: info.bottom; topMargin: Style.gap; left: parent.left; right: parent.right }
        // whole rows only: a page never ends in half a row (it looked like a fault on the panel), the rest of the height stays empty
        height: Math.max(Style.rowHeight, Math.floor((footer.y - info.y - info.height - Style.gap) / Style.rowHeight) * Style.rowHeight)
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

    // the menu: a closed panel under the bar; a tap anywhere else closes it
    MouseArea { anchors.fill: parent; visible: page.menuOpen; z: 9; onClicked: page.menuOpen = false }
    Rectangle {
        id: menuPanel
        objectName: "menuPanel"
        visible: page.menuOpen
        z: 10
        x: Style.pad
        y: bar.height
        width: 560
        height: menuColumn.height + 2 * Style.ruleWidth
        color: Style.paper
        radius: Style.radiusDialog
        border.width: Style.ruleWidth
        border.color: Style.ink
        Column {
            id: menuColumn
            x: Style.ruleWidth
            y: Style.ruleWidth
            width: parent.width - 2 * Style.ruleWidth
            component MenuRow: Item {
                property alias text: rowText.text
                signal chosen()
                width: parent.width
                height: Style.touchMin
                Rectangle { anchors.fill: parent; color: Style.pressed; visible: rowTapArea.pressed; radius: Style.radiusField }
                Text { id: rowText; x: Style.pad; width: parent.width - 2 * Style.pad; height: parent.height; verticalAlignment: Text.AlignVCenter; font.pixelSize: Style.rowTitleSize; color: Style.ink }
                MouseArea { id: rowTapArea; anchors.fill: parent; onClicked: { page.menuOpen = false; parent.chosen() } }
            }
            MenuRow {
                objectName: "menuUpdateAll"
                visible: store.canUpdateAll
                height: visible ? Style.touchMin : 0
                text: qsTr("home.update_all")
                enabled: !store.busy
                onChosen: {
                    store.startUpdateAll()
                    page.nav.push(Qt.resolvedUrl("OperationPage.qml"), {"nav": page.nav})
                }
            }
            MenuRow {
                objectName: "menuSettings"
                text: qsTr("home.settings")
                onChosen: {
                    store.loadSettings()
                    page.nav.push(Qt.resolvedUrl("SettingsPage.qml"), {"nav": page.nav})
                }
            }
            MenuRow {
                objectName: "menuAbout"
                text: qsTr("home.about")
                onChosen: page.nav.push(Qt.resolvedUrl("AboutPage.qml"), {"nav": page.nav})
            }
        }
    }
}
