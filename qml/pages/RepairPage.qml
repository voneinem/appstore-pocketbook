import QtQuick
import ".."

// An interrupted operation found at start (story 6): what was found, then one confirmation to repair it, or "Not now" to browse only.
Item {
    id: page
    property var nav
    readonly property string pageKind: "other"
    property bool riskTicked: false
    readonly property bool unknownBattery: store.settings.battery === qsTr("unknown")
    Component.onCompleted: store.loadSettings()

    TopBar { id: bar; title: qsTr("Repair") }
    Column {
        anchors { top: bar.bottom; left: parent.left; right: parent.right; margins: Style.pad }
        topPadding: 30
        spacing: 24
        Text { objectName: "repairText"; width: parent.width; text: store.repairText; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; font.bold: true; color: Style.ink }
        TickRow {
            visible: page.unknownBattery
            text: qsTr("The battery level cannot be read; continue at my own risk")
            onToggled: page.riskTicked = checked
        }
        Row {
            spacing: Style.gap
            ActionButton { objectName: "notNow"; text: qsTr("Not now"); onClicked: page.nav.pop() }
            ActionButton {
                objectName: "repair"
                primary: true
                text: qsTr("Repair")
                enabled: !page.unknownBattery || page.riskTicked
                onClicked: {
                    store.startRepair(page.riskTicked)
                    page.nav.replace(Qt.resolvedUrl("OperationPage.qml"), {"nav": page.nav})
                }
            }
        }
    }
}
