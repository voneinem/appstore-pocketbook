#pragma once
// A plan: everything an operation will do, produced by the pure planner, printed by one renderer for the
// dry run and the real run alike, and executed only after the confirmations it names (data-model.md, Plan).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/confirmations.h"
#include "pbstore/install/space.h"

namespace pbstore {
namespace install {

enum class PlanKind { Install, Update, Remove, Adopt, Replace, Repair, DeleteData };
const char* plan_kind_name(PlanKind kind);   // "install", "update", ...

enum class ItemOp { Add, Replace, Remove, Keep };

struct PlanItem {
    ItemOp op = ItemOp::Add;
    std::string path;           // applications/...
    uint64_t size = 0;          // the new size (Add, Replace); the current size (Remove, Keep)
    std::string sha256;         // the new hash (Add, Replace); the recorded hash (Remove, Keep)
    std::string old_sha256;     // Replace, Remove: the hash of the file on the reader now; "" for Add
    std::string reason;         // shown to people, for example "changed since the install, stays"
};

struct MenuChange {
    enum class Action { None, Add, Replace, Remove };
    Action action = Action::None;
    std::string id, title, group, path;
};

enum class RecordChange { None, Write, Replace, Delete };

struct DataDirPlan {
    std::string path;           // system/<name>
    bool exists = false;
    std::string owner;          // the app id whose record names it, or ""
    uint64_t files = 0;         // counted, never read
    uint64_t bytes = 0;
};

struct BackupPlan {
    std::string name;                               // view-<stamp>.json, empty if view.json does not change
    std::vector<std::string> delete_after_commit;   // older backups of the same local day
};

struct SpacePlan {
    uint64_t needed = 0;
    uint64_t free_bytes = 0;
    uint64_t cluster = 4096;
    SpaceParts parts;
};

// What the fingerprint was made from, kept so that a change can be named in plain words.
struct FingerprintBasis {
    std::string view_sha256;               // of the desktop configuration, "" if it is missing
    std::vector<std::string> facts;        // "path<TAB>kind<TAB>size<TAB>sha256", sorted by path
    uint64_t free_bytes = 0;
};

struct Plan {
    PlanKind kind = PlanKind::Install;
    std::string app_id, app_name;
    std::string from_version, to_version;     // "" if none
    std::string source;                       // what the record will say: catalog, local or adopted
    std::string source_label;                 // shown; for a local file: "not from the catalog, not signed by the maintainer"
    std::string catalog_generated_at;         // "" when no catalog was used
    std::string package_sha256;

    std::vector<PlanItem> items;              // in the order they are applied
    std::vector<std::string> directories_to_create;   // in creation order
    std::vector<std::string> directories_to_remove;   // removal: created directories that are removed when empty, deepest first
    std::string launcher;                     // the file placed last (the app's menu path)

    MenuChange menu;
    RecordChange record_change = RecordChange::None;
    std::string record_text;                  // the new install record
    std::string record_old_sha256;            // the record on the reader now, "" if none
    std::string record_new_sha256;            // "" when the record is deleted

    std::string view_old_sha256, view_new_sha256;
    std::string view_new_text;                // "" when view.json does not change

    std::optional<DataDirPlan> data;
    bool delete_data = false;                 // remove: the data folder is to be deleted afterwards, after two more confirmations
    BackupPlan backup;
    SpacePlan space;
    std::vector<std::string> warnings;
    Confirmations confirmations;              // required; Confirmation::Plan always
    FingerprintBasis fingerprint_basis;
    std::string fingerprint;
};

// The paths whose state the plan depends on: item paths, directories, the record, the data folder.
std::vector<std::string> plan_target_paths(const Plan& plan);

// The path of the install record of an app: system/pbstore/installed/<id>.json
std::string plan_record_path(const Plan& plan);

}  // namespace install
}  // namespace pbstore
