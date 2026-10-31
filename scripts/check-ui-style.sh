#!/usr/bin/env bash
# Mechanical check of the e-ink style of the store's QML (spec FR-009, FR-042; contracts/ui-pages.md).
# Run by CTest and scripts/pre-merge.sh.
#
#   scripts/check-ui-style.sh [repo-root]    scan (default: this repository)
#   scripts/check-ui-style.sh --self-test    prove that every rule below detects a violation
#
# Rules (a finding makes the script exit non-zero), over every file under qml/:
#   S1 No animation: NumberAnimation, PropertyAnimation, SmoothedAnimation, SpringAnimation, ColorAnimation,
#      ParallelAnimation, SequentialAnimation, PathAnimation, any *Animator, Animation, AnimatedImage,
#      AnimatedSprite, Behavior, Transition, BusyIndicator, ProgressBar, or the word indeterminate.
#   S2 Touch targets: Style.touchMin (the property touchMin) is at least 120, and a tappable item (MouseArea,
#      TapHandler, any type ending in Button, or any item with onClicked/onTapped) has no literal width, height,
#      implicitWidth or implicitHeight below 120.
#   S3 A Flickable, ListView, GridView or ScrollView sets boundsBehavior: Flickable.StopAtBounds (no kinetic overshoot).
#   S4 Text is at least 32 px: font.pixelSize below 32 and Style.bodySize below 32 fail; font.pointSize is not used.
#   S5 An IconButton inside a TopBar has a non-empty label (its name for the tests and assistive software; since 2026-10-29 it is not drawn).
#   S6 Text is black on white: a Text, Label or SelectableText with a literal color other than black or white fails
#      (grey comes only from the named Style colors of a disabled control).
#   S7 A list row is 160 px: rowHeight and the height of an InkRow, when literal, are 160.
#   S8 No remote content: no source: set to an http(s)/ftp address, no QtWebView/QtWebEngine, no Qt.openUrlExternally
#      (a link is opened only through device.openUrl after the user's confirmation, never by Qt and never fetched by the store, FR-043).
set -u

SELF="$(basename "${BASH_SOURCE[0]}")"

# scan_file <file>: prints findings, returns 0 when there is at least one.
scan_file() {
    local f="$1" out
    out="$(awk -v F="$f" '
        function finding(rule, line, msg) { printf "%s %s:%d: %s\n", rule, F, line, msg; found = 1 }
        function istappable(t) { return t == "MouseArea" || t == "TapHandler" || t ~ /Button$/ }
        {
            line = $0
            sub(/(^|[[:space:]])\/\/.*$/, "", line)       # comments (not the // of an address)
            # ---- S1, S4 (pointSize), S8: single-line patterns
            if (line ~ /(^|[^A-Za-z_.])(NumberAnimation|PropertyAnimation|SmoothedAnimation|SpringAnimation|ColorAnimation|ParallelAnimation|SequentialAnimation|PathAnimation|Animation|AnimatedImage|AnimatedSprite|BusyIndicator|ProgressBar|Transition|[A-Za-z]+Animator)[[:space:]]*(\{|$)/ ||
                line ~ /(^|[^A-Za-z_.])Behavior[[:space:]]+on[[:space:]]/ || line ~ /indeterminate/)
                finding("S1", NR, "animation or indeterminate item")
            if (line ~ /font\.pointSize/) finding("S4", NR, "font.pointSize (use font.pixelSize of at least 32)")
            if (match(line, /font\.pixelSize[[:space:]]*:[[:space:]]*[0-9]+/)) {
                v = substr(line, RSTART, RLENGTH); sub(/.*:[[:space:]]*/, "", v)
                if (v + 0 < 32) finding("S4", NR, "text smaller than 32 px")
            }
            if (match(line, /(^|[^A-Za-z_.])bodySize[[:space:]]*:[[:space:]]*[0-9]+/)) {
                v = substr(line, RSTART, RLENGTH); sub(/.*:[[:space:]]*/, "", v)
                if (v + 0 < 32) finding("S4", NR, "Style.bodySize below 32")
            }
            if (match(line, /(^|[^A-Za-z_.])touchMin[[:space:]]*:[[:space:]]*[0-9]+/)) {
                v = substr(line, RSTART, RLENGTH); sub(/.*:[[:space:]]*/, "", v)
                if (v + 0 < 120) finding("S2", NR, "Style.touchMin below 120")
            }
            if (match(line, /(^|[^A-Za-z_.])rowHeight[[:space:]]*:[[:space:]]*[0-9]+/)) {
                v = substr(line, RSTART, RLENGTH); sub(/.*:[[:space:]]*/, "", v)
                if (v + 0 != 160) finding("S7", NR, "rowHeight is not 160")
            }
            if (line ~ /(^|[^A-Za-z_.])source[[:space:]]*:[[:space:]]*["\047](https?|ftp):\/\//) finding("S8", NR, "remote source")
            if (line ~ /^[[:space:]]*import[[:space:]]+(QtWebView|QtWebEngine)/) finding("S8", NR, "web view import")
            if (line ~ /Qt\.openUrlExternally/) finding("S8", NR, "Qt.openUrlExternally")

            # ---- block tracking: a line that opens a typed block ("Type {")
            if (match(line, /^[[:space:]]*[A-Z][A-Za-z0-9_.]*[[:space:]]*\{/)) {
                t = substr(line, RSTART, RLENGTH); gsub(/[[:space:]{]/, "", t)
                sp++; type[sp] = t; start[sp] = NR; small[sp] = ""; hasclick[sp] = 0; bounds[sp] = 0; label[sp] = 0; emptylabel[sp] = 0
                bdepth[sp] = depth + 1
            }
            # direct properties of the innermost typed block (only at its own depth)
            if (sp > 0) {
                d = (match(line, /^[[:space:]]*[A-Z][A-Za-z0-9_.]*[[:space:]]*\{/)) ? depth + 1 : depth
                if (d == bdepth[sp]) {
                    if (match(line, /^[[:space:]]*(width|height|implicitWidth|implicitHeight)[[:space:]]*:[[:space:]]*[0-9]+[[:space:]]*;?[[:space:]]*$/)) {
                        v = line; sub(/.*:[[:space:]]*/, "", v); sub(/[^0-9].*$/, "", v)
                        prop = line; sub(/^[[:space:]]*/, "", prop); sub(/[[:space:]]*:.*$/, "", prop)
                        if (v + 0 < 120) small[sp] = small[sp] NR ":" prop "=" v " "
                        if (type[sp] == "InkRow" && prop == "height" && v + 0 != 160) finding("S7", NR, "InkRow height is not 160")
                    }
                    if (line ~ /^[[:space:]]*(onClicked|onTapped)[[:space:]]*:/) hasclick[sp] = 1
                    if (line ~ /^[[:space:]]*boundsBehavior[[:space:]]*:[[:space:]]*Flickable\.StopAtBounds/) bounds[sp] = 1
                    if (line ~ /^[[:space:]]*label[[:space:]]*:[[:space:]]*(""|\047\047)[[:space:]]*;?[[:space:]]*$/) emptylabel[sp] = 1
                    else if (line ~ /^[[:space:]]*label[[:space:]]*:[[:space:]]*[^[:space:]]/) label[sp] = 1
                    if (type[sp] ~ /^(Text|Label|SelectableText)$/ && match(line, /^[[:space:]]*color[[:space:]]*:[[:space:]]*("[^"]*"|\047[^\047]*\047)/)) {
                        c = substr(line, RSTART, RLENGTH); sub(/^[^"\047]*["\047]/, "", c); sub(/["\047]$/, "", c); c = tolower(c)
                        if (!(c == "black" || c == "#000" || c == "#000000" || c == "white" || c == "#fff" || c == "#ffffff"))
                            finding("S6", NR, "text color " c " is neither black nor white")
                    }
                }
            }
            n = gsub(/\{/, "{", line); m = gsub(/\}/, "}", line)
            depth += n - m
            # close typed blocks whose depth was left
            while (sp > 0 && depth < bdepth[sp]) {
                t = type[sp]
                if ((istappable(t) || hasclick[sp]) && small[sp] != "") finding("S2", start[sp], "tappable " t " smaller than 120 px: " small[sp])
                if ((t == "Flickable" || t == "ListView" || t == "GridView" || t == "ScrollView") && !bounds[sp]) finding("S3", start[sp], t " without boundsBehavior: Flickable.StopAtBounds")
                if (t == "IconButton") {
                    intop = 0
                    for (i = 1; i < sp; i++) if (type[i] == "TopBar") intop = 1
                    if (intop && (!label[sp] || emptylabel[sp])) finding("S5", start[sp], "IconButton in a TopBar without a non-empty label")
                }
                sp--
            }
        }
        END { exit found ? 0 : 1 }' "$f")"
    if [ -n "$out" ]; then printf '%s\n' "$out"; return 0; fi
    return 1
}

scan() {
    local root="$1" bad=0 f
    [ -d "$root/qml" ] || { echo "$SELF: no qml/ directory under $root" >&2; return 2; }
    while IFS= read -r -d '' f; do
        scan_file "$f" && bad=1
    done < <(find "$root/qml" -type f \( -name '*.qml' -o -name qmldir -o -name qml.qrc \) -print0 | sort -z)
    return $bad
}

self_test() {
    local tmp rc=0 n=0
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    # good <description> <qml>: must pass;  bad <rule> <description> <qml>: must be reported with that rule
    good() {
        rm -rf "$tmp/t"; mkdir -p "$tmp/t/qml"; printf '%s\n' "$2" > "$tmp/t/qml/A.qml"; n=$((n + 1))
        if scan "$tmp/t" >/dev/null; then :; else echo "self-test FAILED: $1 was reported:" >&2; scan "$tmp/t" | sed 's/^/    /' >&2; rc=1; fi
    }
    bad() {
        rm -rf "$tmp/t"; mkdir -p "$tmp/t/qml"; printf '%s\n' "$3" > "$tmp/t/qml/A.qml"; n=$((n + 1))
        if scan "$tmp/t" 2>/dev/null | grep -q "^$1 "; then :; else echo "self-test FAILED: $1 was not detected: $2" >&2; rc=1; fi
    }
    good "a clean page" 'import QtQuick
Item {
    Text {
        text: "Hello"
        font.pixelSize: 32
        color: "black"
    }
    MouseArea {
        anchors.fill: parent
        width: 120
        height: 160
        onClicked: x()
    }
    ListView {
        boundsBehavior: Flickable.StopAtBounds
        height: 800
    }
    InkRow {
        height: 160
    }
}'
    good "icon buttons with labels in a top bar" 'import QtQuick
Item {
    TopBar {
        IconButton {
            label: qsTr("Refresh")
        }
        IconButton {
            label: strings.search
        }
    }
}'
    good "a small icon inside a button that is not itself small" 'import QtQuick
Item {
    IconButton {
        width: 120
        height: 120
        Icon {
            width: 48
            height: 48
        }
    }
}'
    good "a progress rectangle whose width is set" 'import QtQuick
Item {
    Rectangle {
        width: 300
        height: 12
    }
}'
    for w in NumberAnimation PropertyAnimation SmoothedAnimation SpringAnimation ColorAnimation ParallelAnimation SequentialAnimation PathAnimation OpacityAnimator BusyIndicator ProgressBar AnimatedImage Transition; do
        bad S1 "$w" "import QtQuick
Item {
    $w {
        duration: 200
    }
}"
    done
    bad S1 "Behavior on" 'import QtQuick
Item {
    Behavior on x {
        NumberAnimation { duration: 100 }
    }
}'
    bad S1 "indeterminate" 'import QtQuick
Item {
    property bool indeterminate: true
}'
    bad S2 "touchMin 96" 'pragma Singleton
QtObject {
    readonly property int touchMin: 96
}'
    bad S2 "a small MouseArea" 'import QtQuick
Item {
    MouseArea {
        width: 100
        height: 120
    }
}'
    bad S2 "a small button" 'import QtQuick
Item {
    ToolButton {
        width: 120
        height: 80
    }
}'
    bad S2 "a small item with onClicked" 'import QtQuick
Item {
    Rectangle {
        implicitHeight: 90
        onClicked: x()
    }
}'
    bad S3 "ListView without bounds" 'import QtQuick
Item {
    ListView {
        height: 800
    }
}'
    bad S3 "Flickable with another bounds behavior" 'import QtQuick
Item {
    Flickable {
        boundsBehavior: Flickable.DragOverBounds
    }
}'
    bad S4 "pixelSize 28" 'import QtQuick
Item {
    Text {
        font.pixelSize: 28
    }
}'
    bad S4 "bodySize 24" 'pragma Singleton
QtObject {
    readonly property int bodySize: 24
}'
    bad S4 "pointSize" 'import QtQuick
Item {
    Text {
        font.pointSize: 12
    }
}'
    bad S5 "an icon button without a label in a top bar" 'import QtQuick
Item {
    TopBar {
        IconButton {
            icon: "refresh"
        }
    }
}'
    bad S5 "an icon button with an empty label in a top bar" 'import QtQuick
Item {
    TopBar {
        IconButton {
            label: ""
        }
    }
}'
    bad S6 "grey text" 'import QtQuick
Item {
    Text {
        color: "#808080"
    }
}'
    bad S6 "named grey text" 'import QtQuick
Item {
    SelectableText {
        color: "gray"
    }
}'
    bad S7 "rowHeight 120" 'pragma Singleton
QtObject {
    readonly property int rowHeight: 120
}'
    bad S7 "InkRow height 140" 'import QtQuick
Item {
    InkRow {
        height: 140
    }
}'
    bad S8 "remote image" 'import QtQuick
Item {
    Image {
        source: "https://example.org/a.png"
    }
}'
    bad S8 "web view" 'import QtWebView
Item {
}'
    bad S8 "openUrlExternally" 'import QtQuick
Item {
    MouseArea {
        width: 120
        height: 120
        onClicked: Qt.openUrlExternally(link)
    }
}'
    echo "check-ui-style: $n cases"
    return $rc
}

case "${1:-}" in
    --self-test) self_test; rc=$?; [ $rc -eq 0 ] && echo "check-ui-style self-test passed"; exit $rc ;;
    *) ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
       scan "$ROOT"; rc=$?
       if [ $rc -ne 0 ]; then echo "check-ui-style: the findings above must be fixed" >&2; fi
       exit $rc ;;
esac
