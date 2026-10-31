#pragma once
// A scripted walk through the real pages on the host (offscreen), for the acceptance walk of a feature and for pictures of the pages. NOT
// compiled for the reader; only the host test build (pbstore-app-test) has it, started by the environment variable PBSTORE_UI_SCRIPT.
// Adapted from the walk runner of the sibling app PB Cloud (same author). The script is a text file, one command per line:
//
//   size <w> <h>                      resize the window (the reader is 1404 1872)
//   wait <ms>                         let the event loop run
//   tap <objectName> [n]              press and release the mouse on the n-th visible item of that name (0-based; default 0)
//   type <text>                       key presses into the focused item
//   key <name>                        Return | Backspace | Escape
//   shot <file.png>                   save the window
//   text <objectName> [n]             print the text (or label, or value) of the item, for the log of the walk
//   waitfor <objectName> [substring]  wait (up to 20 s) until such an item is visible, with the substring in its text
//   expect <objectName> <substring>   fail the walk if no visible item of that name has the substring in its text
//   expect_absent <objectName>        fail the walk if an item of that name is visible
//   expect_count <objectName> <n>     fail the walk unless exactly n items of that name are visible
//   expect_disabled <objectName>      fail the walk unless the item is visible and disabled
//   expect_enabled <objectName>       fail the walk unless the item is visible and enabled
//   taps_reset                        start counting the taps
//   expect_taps_max <n>               fail the walk if more than n taps were made since taps_reset
//   quit
//
// "#" starts a comment. The result is the exit code of the app: 0, or 3 when a command failed.

#include <QString>

class QQuickWindow;

namespace ui_script {

// Runs the script from the event loop; calls QCoreApplication::exit when it is done.
void start(QQuickWindow* window, const QString& path);

}  // namespace ui_script
