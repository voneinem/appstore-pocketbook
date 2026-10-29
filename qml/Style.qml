pragma Singleton
import QtQuick

// The look every page shares (contracts/ui-pages.md; the look follows PB Notes and PB Cloud: a top bar with icon controls, rounded
// dialogs, paged lists, no animation). Sizes are in panel pixels (1404 x 1872, 300 ppi). Every tappable item is at least touchMin
// (120 px = 10.2 mm, spec FR-009) and every text is at least 32 px, black on white; scripts/check-ui-style.sh enforces both.
QtObject {
    readonly property color ink: "#000000"
    readonly property color paper: "#ffffff"
    readonly property color hairline: "#808080"      // lines only, never text
    readonly property color pressed: "#d8d8d8"       // the fill of a pressed row or control
    readonly property real disabledOpacity: 0.4      // a disabled control is drawn lighter; its label stays legible

    readonly property int hairlineWidth: 1
    readonly property int ruleWidth: 2
    readonly property int radiusField: 10
    readonly property int radiusDialog: 18

    readonly property int touchMin: 120              // the hit area of every control
    readonly property int gap: 16                    // the space between two controls
    readonly property int barHeight: 120
    readonly property int rowHeight: 160
    readonly property int footerHeight: 140
    readonly property int pad: 30

    readonly property int iconTop: 56      // the icons of the top bar, drawn alone (no word under them), as in PB Notes
    readonly property int iconRow: 64

    readonly property int titleSize: 44
    readonly property int rowTitleSize: 40
    readonly property int bodySize: 32
    readonly property int labelSize: 32
    readonly property int captionSize: 32
}
