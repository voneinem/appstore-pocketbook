#pragma once
// What the user sees for each app: derived from the catalog, the install records and the desktop
// configuration; never stored (data-model.md, App State).

#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/install/reader_state.h"

namespace pbstore {
namespace install {

enum class AppStateKind {
    NotInstalled,       // in the catalog, no record and no entry with its menu id
    Installed,          // a record exists
    OutsideInstaller,   // no record, but an entry with the menu id of a catalog app exists
    UnknownApp,         // a U_ entry that is neither recorded nor in the catalog
};

struct AppEntry {
    std::string id;                  // the catalog id, or the menu entry id for an unknown app
    std::string name;
    AppStateKind state = AppStateKind::NotInstalled;
    std::string installed_version;   // "" if not installed
    std::string offered_version;     // "" if not in the catalog
    std::string description;         // the catalog's short description (English), "" if none
    bool offered = false;            // in the catalog and not withdrawn
    bool update_available = false;   // offered and newer than the installed copy
    bool changed = false;            // a recorded file differs from the record (or is missing)
    bool from_local = false;         // installed from a local file
    bool not_in_catalog = false;     // installed, but the catalog does not list it
    bool installed_version_withdrawn = false;   // information only: nothing is ever changed because of it
    bool menu_entry_missing = false; // a record exists but its entry is not in the desktop configuration
    std::string source;              // catalog, local, adopted
    std::vector<std::string> changed_files;
};

// Sorted by name. `catalog` may be null (offline): then only what is on the reader is listed.
std::vector<AppEntry> derive_app_states(const Catalog* catalog, const ReaderSnapshot& snapshot);

}  // namespace install
}  // namespace pbstore
