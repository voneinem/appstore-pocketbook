#pragma once
// What the store says about its own version (research R-12). The installer's record names the version that is packaged
// (applications/pbstore.bin, always true); the store knows the version that runs (compiled in); the launcher leaves a failed
// program and a message when a new version did not start. The view adds a note to the list entry and About; it never changes
// the state the engine derived.

#include <optional>
#include <string>

#include "pbstore/store/run_state.h"

namespace pbstore {
namespace store {

enum class SelfUpdateKind {
    Current,        // the running version is the recorded one (or nothing can be compared)
    UpdateStaged,   // the record is newer: it takes effect when PB Store is opened again
    RolledBack,     // the record is newer, but that version failed to start; the older one runs
    Ahead,          // the running version is newer than the record: someone replaced the package by hand
};

struct SelfUpdateInputs {
    std::string running_version;                    // compiled in from app/VERSION
    std::string record_version;                     // the install record of `pbstore`, "" if there is none
    RunMessage message;                             // what the launcher left, already taken
    bool trying = false;                            // the launcher's marker: this start has not reached its first screen
    bool failed_present = false;                    // the launcher keeps the program that failed
    std::optional<std::string> last_run_version;    // the last version that reached its first screen
};

struct SelfUpdateView {
    SelfUpdateKind kind = SelfUpdateKind::Current;
    std::string failed_version;                     // RolledBack
    std::string list_note;                          // for the list entry; empty for Current
    std::string about_text;                         // for About
    std::optional<std::string> updated_to;          // the first screen says "PB Store was updated to X" once
    std::string updated_text;
};

SelfUpdateView self_update_view(const SelfUpdateInputs& in);

}  // namespace store
}  // namespace pbstore
