#pragma once
// What the plan page shows first: a few plain sentences from the plan, for an end user. No file names, folders, hashes or rule ids; the
// installer's full text (every file, the folders, the install record, the backup, the warnings with their ids) stays under "Show details"
// and is the engine's, unchanged (spec FR-014). Names from a catalog or a package go through store_text.

#include <string>

#include "pbstore/install/plan.h"

namespace pbstore {
namespace store {

std::string plain_plan_summary(const install::Plan& plan);

}  // namespace store
}  // namespace pbstore
