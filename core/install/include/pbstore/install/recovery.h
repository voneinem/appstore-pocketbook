#pragma once
// Finding and repairing an interrupted operation (contracts/journal.md, recovery decision). The same code
// serves a failure, a cancel and the next run: it reads hashes first and each step is idempotent, so it can
// itself be interrupted and run again.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/confirmations.h"
#include "pbstore/install/install_rules.h"
#include "pbstore/install/journal.h"
#include "pbstore/install/progress.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct RepairPlan {
    enum class Action {
        Discard,   // the journal never reached the prepared state: the live area is untouched
        Undo,      // prepared but not committed: put the old version back
        Finish,    // committed: verify and clean up
    };
    Action action = Action::Discard;
    std::string kind, app_id, from_version, to_version;   // from the intent; empty if there is none
    std::string reason;                                   // why this action, in plain words
    Intent intent;                                        // valid for Undo and Finish
    std::string intent_sha256;
    std::string text() const;                             // the repair plan as shown
};

struct RepairCheck {
    std::optional<RepairPlan> plan;        // set if an interrupted operation was found
    std::vector<Finding> findings;         // E-INS-35 when the journal is from a newer format
};

RepairCheck plan_repair(ReaderFs& fs);

struct RepairResult {
    bool done = false;                     // the journal is gone and the reader holds one complete version
    std::string outcome;                   // "undone" or "finished" (or "undone because the new version did not verify")
    std::vector<Finding> findings;         // E-INS-24 for paths that match neither old nor new; E-INS-33
    ReaderEffect effect = ReaderEffect::Unchanged;
};

// Needs the Repair confirmation to have been given by the caller (the front ends check it); locks the reader.
RepairResult repair(ReaderFs& fs, const RepairPlan& plan);

// The undo and the finish steps, used by the executor for its own journal while it holds the lock.
RepairResult undo_journal(ReaderFs& fs, const Intent& intent);
RepairResult finish_journal(ReaderFs& fs, const Intent& intent, const std::string& intent_sha256);

}  // namespace install
}  // namespace pbstore
