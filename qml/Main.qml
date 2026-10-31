import QtQuick
import QtQuick.Controls
import QtQuick.Window

// The single window. The system rotates (the reader's QPA), Qt resizes the window; this stage only compensates the panel's
// devicePixelRatio so that sizes authored in panel pixels are 1:1. No animation anywhere (no transitions).
ApplicationWindow {
    id: win
    visible: true
    visibility: Window.FullScreen
    color: "#ffffff"

    // Used by the scripted screenshots of the host test build: opens one of the pages on top of the list.
    function openPage(name) {
        if (name === "SettingsPage" || name === "LogPage") store.loadSettings()
        if (name === "LogPage") store.loadLog()
        nav.push(Qt.resolvedUrl("pages/" + name + ".qml"), {"nav": nav})
    }
    function closePage() { if (nav.depth > 1) nav.pop() }

    // True once a page is shown (checked by the UI self-test: a blank screen must never go unnoticed).
    readonly property bool pageLoaded: nav.depth > 0

    Item {
        id: stage
        objectName: "stage"
        readonly property real dpr: Screen.devicePixelRatio
        width: win.width * dpr
        height: win.height * dpr
        anchors.centerIn: parent
        scale: 1 / dpr

        StackView {
            id: nav
            objectName: "nav"
            anchors.fill: parent
            // pushing the first page explicitly: an initialItem binding showed nothing on the reader (as in the sibling apps)
            // An interrupted operation is shown before anything else (story 6): its page goes on top of the list. It is pushed from a timer: a push
            // from inside the first one (while it completes) is refused ("cannot push while already in the process of completing a push").
            Component.onCompleted: {
                push(Qt.resolvedUrl("pages/HomePage.qml"), {"nav": nav})
                if (store.repairPending) repairTimer.start()
            }
            onCurrentItemChanged: if (currentItem) store.pageKind(currentItem.pageKind)
            // E-ink: no transitions (null switches the default ones off)
            pushEnter: null
            pushExit: null
            popEnter: null
            popExit: null
            replaceEnter: null
            replaceExit: null
        }

        Timer {
            id: repairTimer
            interval: 20
            onTriggered: nav.push(Qt.resolvedUrl("pages/RepairPage.qml"), {"nav": nav})
        }

        Connections {
            target: device
            function onBackRequested() {
                if (nav.depth > 1) nav.pop()
            }
        }
    }
}
