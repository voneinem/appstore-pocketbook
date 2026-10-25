#pragma once
// The assistant: a loopback server with the page, the address printed and opened in the browser.

#include <iostream>
#include <string>
#include <vector>

#include "pbstore/install/engine.h"
#include "pbstore/install/open_url.h"
#include "pbstore/install/progress.h"

namespace pbstore {
namespace install {
namespace ui {

// Runs until the page closes it, nobody uses it any more, or `interrupt` is cancelled (Ctrl-C). Returns the
// exit status: 0, or 2 if the server could not start.
int run_assistant(Engine& engine, OpenUrl& opener, std::ostream& out, std::ostream& err, const std::string& home, const std::vector<std::string>& roots,
                  CancelToken& interrupt);

}  // namespace ui
}  // namespace install
}  // namespace pbstore
