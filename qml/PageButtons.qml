import QtQuick

// "Page up" and "page down" for a ListView or Flickable with the position between them ("2 / 3"). The view moves by whole pages (rows
// when `snap` is set), never by kinetic scrolling or animation. At the first and last page the unusable button is drawn disabled, not
// hidden; a view that fits on one page shows nothing (spec FR-009).
Row {
    id: bar
    property Item target: null
    property real snap: 0
    signal turned()

    readonly property real pageStep: !target ? 1 : (snap > 0 ? Math.max(1, Math.floor(target.height / snap)) * snap : Math.max(100, target.height - 80))
    readonly property int pageCount: !target ? 1 : Math.max(1, Math.ceil(Math.max(0, target.contentHeight - target.height - 1) / pageStep) + 1)
    readonly property int pageNumber: !target ? 1 : Math.min(pageCount, Math.max(1, Math.floor((target.contentY - (target.originY || 0)) / pageStep + 0.5) + 1))
    readonly property bool canUp: target && target.contentY > (target.originY || 0) + 1
    readonly property bool canDown: target && target.contentY < (target.originY || 0) + target.contentHeight - target.height - 1
    visible: pageCount > 1
    spacing: 24

    function pageBy(direction) {
        if (!target) return
        var top = target.originY !== undefined ? target.originY : 0
        var maxY = Math.max(top, top + target.contentHeight - target.height)
        if (snap > 0) maxY = top + Math.ceil((maxY - top) / snap) * snap
        target.contentY = Math.min(maxY, Math.max(top, target.contentY + direction * pageStep))
        turned()
    }

    IconButton { objectName: "pageUp"; label: qsTr("pagebuttons.up"); icon: "chevron-up"; enabled: bar.canUp; onClicked: bar.pageBy(-1) }
    Text {
        objectName: "pageIndicator"
        width: 170
        height: Style.touchMin
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Style.bodySize
        color: Style.ink
        text: bar.pageNumber + " / " + bar.pageCount
    }
    IconButton { objectName: "pageDown"; label: qsTr("pagebuttons.down"); icon: "chevron-down"; enabled: bar.canDown; onClicked: bar.pageBy(1) }
}
