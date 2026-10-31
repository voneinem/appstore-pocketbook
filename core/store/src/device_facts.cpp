#include "pbstore/store/device_facts.h"

namespace pbstore {
namespace store {

RestartState restart_state(const DeviceFacts& f) {
    if (f.restart_needed == Fact::Yes) return RestartState::Needed;
    if (f.restart_needed == Fact::No) return RestartState::NotNeeded;
    if (f.shown_without_restart == Fact::Yes) return RestartState::NotNeeded;
    if (f.shown_without_restart == Fact::No) return RestartState::Needed;
    return RestartState::Maybe;
}

namespace {
const char* kLonger = " The next start of the reader may take noticeably longer; wait for it.";
}

std::string result_text(const DeviceFacts& facts, WordingOp op, const WordingArgs& a) {
    const RestartState restart = restart_state(facts);
    std::string t;
    switch (op) {
        case WordingOp::Install:
        case WordingOp::Update:
            if (restart == RestartState::Needed) {
                t = a.app + " is installed.";
                if (op == WordingOp::Update) t += " Its data was kept.";
                t += " Restart the reader now to see it in " + a.group + "." + kLonger;
            } else {
                t = a.app + " is installed. It will appear in " + a.group + ".";
                if (op == WordingOp::Update) t += " Its data was kept.";
                if (restart == RestartState::Maybe) t += std::string(" If it is not there, restart the reader.") + kLonger;
            }
            break;
        case WordingOp::Remove:
            t = a.app + " was removed.";
            if (restart == RestartState::Maybe) t += " If it is still in the menu, restart the reader.";
            if (restart == RestartState::Needed) t += " Restart the reader now to remove it from the menu.";
            if (!a.folder.empty()) t += a.data_deleted ? " Its data was deleted." : " Its data was kept in " + a.folder + ".";
            break;
        case WordingOp::Adopt:
            t = a.app + " is now managed by PB Store. Nothing else on the reader was changed.";
            break;
        case WordingOp::Repair:
            t = "The interrupted " + a.repair_kind + " of " + a.app + " was " + (a.repair_finished ? "finished" : "undone") + ".";
            if (restart == RestartState::Maybe) t += " If the menu looks wrong, restart the reader.";
            if (restart == RestartState::Needed) t += " Restart the reader now to see the result.";
            break;
        case WordingOp::SelfUpdate:
            t = "PB Store " + a.version + " is installed. It takes effect when you open PB Store again.";
            break;
    }
    return t;
}

}  // namespace store
}  // namespace pbstore
