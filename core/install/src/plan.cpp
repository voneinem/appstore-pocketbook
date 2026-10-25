#include "pbstore/install/plan.h"

#include <algorithm>
#include <set>

#include "pbstore/install/install_record.h"

namespace pbstore {
namespace install {

const char* plan_kind_name(PlanKind kind) {
    switch (kind) {
        case PlanKind::Install: return "install";
        case PlanKind::Update: return "update";
        case PlanKind::Remove: return "remove";
        case PlanKind::Adopt: return "adopt";
        case PlanKind::Replace: return "replace";
        case PlanKind::Repair: return "repair";
        case PlanKind::DeleteData: return "delete-data";
    }
    return "?";
}

std::string plan_record_path(const Plan& plan) { return record_path(plan.app_id); }

std::vector<std::string> plan_target_paths(const Plan& plan) {
    std::set<std::string> paths;
    for (const PlanItem& i : plan.items) paths.insert(i.path);
    for (const std::string& d : plan.directories_to_create) paths.insert(d);
    for (const std::string& d : plan.directories_to_remove) paths.insert(d);
    if (!plan.app_id.empty()) paths.insert(plan_record_path(plan));
    if (plan.data) paths.insert(plan.data->path);
    return std::vector<std::string>(paths.begin(), paths.end());
}

}  // namespace install
}  // namespace pbstore
