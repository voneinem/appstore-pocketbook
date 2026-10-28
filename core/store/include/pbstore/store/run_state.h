#pragma once
// The launcher's small files and the store's last-run version (data model RunState; contracts/launcher.md,
// contracts/store-state-folder.md). Two state folders: the app folder (system/pbstore/app, for last-run-version) and its
// run/ folder (the launcher's files). The store removes `trying` and `message`; the launcher writes everything else in run/.

#include <optional>
#include <string>

#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace store {

// The one line the launcher leaves for the store to show once.
struct RunMessage {
    enum class Kind { None, UpdateFailed, Restored, SwapSkipped, Unknown };
    Kind kind = Kind::None;
    std::string detail;   // the version, or the reason
};

class RunState {
public:
    RunState(install::StateDirs& app, install::StateDirs& run) : app_(app), run_(run) {}

    bool trying();                          // the running program has not yet reached its first screen
    void mark_started();                    // removes `trying` and nothing else (spec FR-034)
    std::optional<int> last_exit();         // an integer of at most 8 bytes; absent: the launcher itself was killed
    bool failed_present();                  // the launcher keeps the program that failed to start
    RunMessage take_message();              // returns the message once and deletes the file
    std::optional<std::string> last_run_version();   // one semantic version line of at most 32 bytes
    bool remember_run_version(const std::string& version);

private:
    install::StateDirs& app_;
    install::StateDirs& run_;
};

}  // namespace store
}  // namespace pbstore
