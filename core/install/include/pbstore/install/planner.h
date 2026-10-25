#pragma once
// The pure planner: a request, a verified package manifest and a snapshot of the reader become a plan or the
// findings that say why there is none. No I/O, no clock (the times it needs are in the request): the same
// input gives the same plan, so the dry run and the real run print and do the same.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/clock.h"
#include "pbstore/install/plan.h"
#include "pbstore/install/reader_state.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct PlanRequest {
    PlanKind kind = PlanKind::Install;
    std::string app_id;
    const Manifest* manifest = nullptr;     // the new package (not for Remove)
    std::string package_sha256;
    std::string source = "catalog";         // what the record will say: catalog or local
    std::string source_label = "catalog";   // shown in the plan
    std::string catalog_generated_at;
    Utc now;                                // written to the record
    LocalTime local_now;                    // names the backup
    std::string installed_by;               // "pbstore-install <version>"
};

struct PlanResult {
    std::optional<Plan> plan;
    std::vector<Finding> findings;   // errors: why there is no plan
    std::vector<Finding> notices;    // W-INS-01 "nothing to do", W-INS-02
};

PlanResult make_plan(const PlanRequest& request, const ReaderSnapshot& snapshot);

// The one text of a plan, for the dry run and the real run, for every front end.
std::string render_plan(const Plan& plan);

}  // namespace install
}  // namespace pbstore
