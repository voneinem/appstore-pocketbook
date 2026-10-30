import QtQuick
import ".."

// One operation from start to result (stories 2 to 6): planning, the plan with the consents it names, the work, and the result with the three
// parts of a failure. The plan text is the engine's, unchanged. Nothing is written before Confirm. Cancel works until the commit point.
Item {
    id: page
    property var nav
    readonly property string pageKind: "progress"
    property bool started: false
    readonly property string phase: store.phase
    property var ticked: ({})
    property bool riskTicked: false
    readonly property int needed: store.confirmations.length
    property int tickedCount: 0
    readonly property bool ready: tickedCount >= needed && (!store.batteryUnreadable || riskTicked)

    function names() {
        var out = []
        for (var i = 0; i < store.confirmations.length; ++i) if (ticked[store.confirmations[i].name]) out.push(store.confirmations[i].name)
        return out
    }
    Connections {
        target: store
        function onOperationChanged() {
            if (store.phase !== "idle") page.started = true
            else if (page.started) { while (page.nav.depth > 1) page.nav.pop() }   // done or declined: back to the list
            if (store.phase === "plan") { page.ticked = ({}); page.tickedCount = 0; page.riskTicked = false }
        }
    }

    TopBar { id: bar; title: store.operationTitle }

    // planning and executing: what is going on, as plain text, and Cancel until the commit point
    Column {
        visible: page.phase === "planning" || page.phase === "executing"
        anchors { top: bar.bottom; left: parent.left; right: parent.right; margins: Style.pad }
        topPadding: 30
        spacing: 30
        Text { objectName: "operationProgress"; width: parent.width; text: store.progressText; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; color: Style.ink }
        Text { visible: store.notice.length > 0; width: parent.width; text: store.notice; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        ActionButton { objectName: "cancel"; visible: store.cancellable; text: qsTr("Cancel"); onClicked: store.cancel() }
        Text { visible: !store.cancellable; width: parent.width; text: qsTr("Finishing; this cannot be cancelled"); wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
    }

    // the plan: a few plain sentences, the consents as separate tap controls, then Confirm and Cancel; the engine's full text (every file) is behind Show details
    Flickable {
        id: planFlick
        visible: page.phase === "plan"
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: planFooter.top; leftMargin: Style.pad; rightMargin: Style.pad }
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        contentWidth: width
        contentHeight: planColumn.height + Style.pad
        Column {
            id: planColumn
            width: planFlick.width
            spacing: 14
            topPadding: 14
            Text { objectName: "planSummary"; width: parent.width; text: store.planSummary; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; color: Style.ink }
            Text { visible: store.extraLine.length > 0; width: parent.width; text: store.extraLine; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; font.bold: true; color: Style.ink }
            Repeater {
                model: store.confirmations
                TickRow {
                    objectName: "consent"
                    text: modelData.label
                    onToggled: {
                        var t = page.ticked
                        t[modelData.name] = checked
                        page.ticked = t
                        var n = 0
                        for (var k in t) if (t[k]) ++n
                        page.tickedCount = n
                    }
                }
            }
            TickRow {
                objectName: "batteryRisk"
                visible: store.batteryUnreadable
                text: qsTr("The battery level cannot be read; continue at my own risk")
                onToggled: page.riskTicked = checked
            }
            ActionButton {
                objectName: "toggleDetails"
                text: store.planDetailsShown ? qsTr("Hide details") : qsTr("Show details")
                onClicked: store.toggleDetails()
            }
            Text { objectName: "planText"; visible: store.planDetailsShown; width: parent.width; text: store.planText; wrapMode: Text.WrapAnywhere; font.pixelSize: Style.bodySize; color: Style.ink }
        }
    }
    Item {
        id: planFooter
        visible: page.phase === "plan"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.footerHeight + Style.touchMin
        PageButtons { id: planPages; anchors { top: parent.top; horizontalCenter: parent.horizontalCenter } target: planFlick; onTurned: store.pageTurned() }
        Row {
            anchors { bottom: parent.bottom; bottomMargin: 10; horizontalCenter: parent.horizontalCenter }
            spacing: Style.gap
            ActionButton { objectName: "decline"; text: qsTr("Cancel"); onClicked: store.decline() }
            ActionButton { objectName: "confirm"; primary: true; text: qsTr("Confirm"); enabled: page.ready; onClicked: store.confirm(page.names(), page.riskTicked) }
        }
    }

    // the result: what was done, or the three parts of a failure; after a removal the separate, doubly confirmed deletion of the data
    Column {
        visible: page.phase === "result"
        anchors { top: bar.bottom; left: parent.left; right: parent.right; margins: Style.pad }
        topPadding: 30
        spacing: 24
        Text { objectName: "resultText"; width: parent.width; text: store.resultText; wrapMode: Text.Wrap; font.pixelSize: Style.rowTitleSize; font.bold: !store.resultOk; color: Style.ink }
        Text { objectName: "resultReader"; visible: store.resultReader.length > 0; width: parent.width; text: store.resultReader; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        Text { objectName: "resultTodo"; visible: store.resultTodo.length > 0; width: parent.width; text: store.resultTodo; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        Text { visible: store.notice.length > 0; width: parent.width; text: store.notice; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; color: Style.ink }
        Column {
            visible: store.deleteStep > 0
            width: parent.width
            spacing: 16
            Text { objectName: "deletePrompt"; width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Style.bodySize; font.bold: true; color: Style.ink
                   text: store.deleteStep === 1 ? qsTr("Delete the data of this app? %1").arg(store.dataFolderText)
                                                : qsTr("Really delete it? This cannot be undone. %1").arg(store.dataFolderText) }
            Row {
                spacing: Style.gap
                ActionButton { objectName: "keepData"; text: qsTr("Keep"); onClicked: store.finishResult() }
                ActionButton { objectName: "deleteData"; text: qsTr("Delete"); onClicked: store.confirmDeleteData() }
            }
        }
        Row {
            visible: store.deleteStep === 0
            spacing: Style.gap
            ActionButton { objectName: "askDelete"; visible: store.canDeleteData; text: qsTr("Delete data too?"); onClicked: store.askDeleteData() }
            ActionButton { objectName: "ok"; primary: true; text: qsTr("OK"); onClicked: store.finishResult() }
        }
    }
}
