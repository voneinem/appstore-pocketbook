#pragma once
// Every error says three things: what happened (with the rule id), whether the reader was changed, and
// what to do next. One function builds them for every front end.

#include <string>
#include <vector>

#include "pbstore/install/install_rules.h"
#include "pbstore/install/plan.h"

namespace pbstore {
namespace install {

struct Message {
    std::string what;            // "error[E-INS-03] /folder: applications/ is missing"
    std::string reader_effect;   // "The reader was not changed."
    std::string next;            // what to do next
    std::vector<std::string> rule_ids;
    // The three parts on separate lines, for the command line.
    std::string text() const;
};

Message describe(const Finding& finding, ReaderEffect effect);
// The effect that the rule's registry entry names (unchanged for other rules).
Message describe(const Finding& finding);

std::string reader_effect_text(ReaderEffect effect);

// The last message of a completed action, the same in every front end: what was done, where the app appears
// or that its data folder was kept, how to eject safely and that a restart may be needed.
std::string completion_text(const Plan& plan);

}  // namespace install
}  // namespace pbstore
