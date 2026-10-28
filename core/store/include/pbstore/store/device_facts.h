#pragma once
// What the result texts say depends on two device facts that no one has verified yet (research R-11): whether the reader has
// to be restarted for a changed menu to show, and whether it shows a new entry without a restart. One table maps the facts to
// the words (contracts/ui-pages.md "Result wording by operation"); when a device task (DS-05) answers a fact, the constant
// below changes with its evidence cited next to it, and a test per combination checks the texts.
//
// Evidence log: none yet. Both facts are Unverified (first run of the installer, 2026-10-25: the reader was restarted every
// time, so it is not known whether that was needed).

#include <string>

namespace pbstore {
namespace store {

enum class Fact { Unverified, Yes, No };

struct DeviceFacts {
    Fact restart_needed = Fact::Unverified;
    Fact shown_without_restart = Fact::Unverified;
};

// What the wording says about a restart. A restart that is stated to be needed wins over "shown without a restart", and an
// explicit "not needed" wins over "needed" shown by the other fact: contradictory combinations take the safer wording for
// Yes and the explicit one for No (see test_device_facts).
enum class RestartState { Maybe, Needed, NotNeeded };
RestartState restart_state(const DeviceFacts& facts);

enum class WordingOp { Install, Update, Remove, Adopt, Repair, SelfUpdate };

struct WordingArgs {
    std::string app;            // the app's name
    std::string group;          // the menu group where it appears
    std::string folder;         // its data folder, "" for an app with none
    std::string version;        // PB Store's own new version (SelfUpdate)
    std::string repair_kind;    // "install", "update", "remove", "adopt" (Repair)
    bool data_deleted = false;  // Remove: the data folder was deleted as well
    bool repair_finished = true;  // Repair: finished (true) or undone (false)
    bool wifi_notice = false;   // the store switched Wi-Fi on for this and off again
};

std::string result_text(const DeviceFacts& facts, WordingOp op, const WordingArgs& args);

}  // namespace store
}  // namespace pbstore
