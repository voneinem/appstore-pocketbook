#pragma once
// The rule ids of conditions that exist only on the reader (E-STO-01 .. E-STO-10, W-STO-01 .. W-STO-03), and the
// message the store shows for any finding: what happened (with the rule id), whether the reader was changed, what to do.
// The engine's texts are used as they are, except where they tell a user of the reader to use a computer, a drive, a cable,
// a keyboard or a command-line option: then the store keeps the first part and replaces the third (contracts/store-rules.md,
// research R-14). docs/store-app-rules.md and contracts/store-rules.md equal this registry (test_store_rules_docs).

#include <string>
#include <vector>

#include "pbstore/install/install_rules.h"
#include "pbstore/install/messages.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace store {

struct StoreRule {
    const char* id;
    const char* what;             // the condition, in plain words (the message of a finding usually adds the figures)
    install::ReaderEffect effect;
    const char* next;             // what to do; empty for a notice that needs nothing
};

const std::vector<StoreRule>& store_rule_registry();
const StoreRule* find_store_rule(const std::string& id);   // nullptr if `id` is not a store rule

// A finding of a store rule. `detail` is the complete sentence with the figures ("The battery is at 17 %; ..."); empty: the
// rule's own condition text.
Finding make_store_finding(const std::string& id, const std::string& subject, const std::string& detail = "");

// The message of any finding, three parts, at most 600 characters, no path longer than 60 characters, never telling the user of
// the reader to use a computer. `effect` is what the reader's state is; the overload without it takes the registry's.
install::Message store_message(const Finding& finding, install::ReaderEffect effect);
install::Message store_message(const Finding& finding);

// True when the text tells the user to use a computer, a drive, a cable, a keyboard, a mount or a command-line option.
// The phrase "PB Installer on a computer" is allowed.
bool tells_the_user_to_use_a_computer(const std::string& text);

// Every token that contains a '/' and is longer than `max_chars` is cut in the middle ("applications/aaa...bbb.bin").
std::string shorten_paths(const std::string& text, size_t max_chars = 60);

constexpr size_t kMaxMessageChars = 600;
constexpr size_t kMaxPathChars = 60;

}  // namespace store
}  // namespace pbstore
