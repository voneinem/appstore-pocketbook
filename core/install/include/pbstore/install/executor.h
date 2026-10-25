#pragma once
// Carries out a plan on a reader by the journal protocol (contracts/journal.md): prepare in the journal area,
// apply as a burst of renames, commit, clean up. Nothing is ever replaced in place and no file data is copied
// between the first and the last change to the live area. Any failure before the commit point is undone.

#include <string>
#include <vector>

#include "pbstore/install/confirmations.h"
#include "pbstore/install/content.h"
#include "pbstore/install/install_rules.h"
#include "pbstore/install/plan.h"
#include "pbstore/install/progress.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct ExecResult {
    enum class Outcome {
        Done,            // the new state is complete and the journal is gone
        NotStarted,      // refused before anything was written
        Undone,          // started, then undone: the previous state is complete
        RepairNeeded,    // started, and the undo could not finish
    };
    Outcome outcome = Outcome::NotStarted;
    bool ok() const { return outcome == Outcome::Done; }
    ReaderEffect effect = ReaderEffect::Unchanged;
    std::vector<Finding> findings;                 // why it did not complete
    std::vector<Confirmation> missing_confirmations;
    bool cancelled = false;                        // E-INS-33: cancelled before the commit point and undone
    bool cancel_too_late = false;                  // the cancel arrived after the commit point; the operation finished
};

// `given` must hold every confirmation the plan requires, or nothing is written.
ExecResult execute(ReaderFs& fs, const Plan& plan, PackageContent& content, const Confirmations& given,
                   const ProgressFn& progress, CancelToken& cancel);

}  // namespace install
}  // namespace pbstore
