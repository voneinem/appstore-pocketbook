#pragma once
// The installer's own rule ids, E-INS-01 .. E-INS-38 and W-INS-01 .. W-INS-03 (contracts/rules.md). The
// catalog and package rules (E-CAT, E-PKG, E-ENT) stay in pbstore_core and keep their own ids.

#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbstore {
namespace install {

// Whether the reader was changed when the rule is reported.
enum class ReaderEffect { Unchanged, Restored, RepairNeeded };

struct InstallRule {
    const char* id;
    const char* description;   // one line, plain language
    ReaderEffect effect;
    const char* next;          // what to do next; empty only for the unused id and for notices
    int exit_status;           // 0..4, see contracts/rules.md
};

const std::vector<InstallRule>& install_rule_registry();
// nullptr if `id` is not an installer rule.
const InstallRule* find_install_rule(const std::string& id);

// The exit status of a command that ended with these findings: the highest status among them
// (notices count 0; findings of the shared checks count 1).
int exit_status_for(const std::vector<Finding>& findings);

}  // namespace install
}  // namespace pbstore
