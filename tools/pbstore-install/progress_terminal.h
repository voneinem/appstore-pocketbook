#pragma once
// Progress on a terminal: at least once per second while something is going on, one line that is rewritten.
// Nothing is printed when the error output is not a terminal, so output that is read by a program stays clean.

#include "pbstore/install/progress.h"

namespace pbstore {
namespace install {
namespace cli {

ProgressFn terminal_progress();
// Ends the rewritten line.
void finish_terminal_progress();

}  // namespace cli
}  // namespace install
}  // namespace pbstore
