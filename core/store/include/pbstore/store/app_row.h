#pragma once
// What the list and the detail page show for one app: derived from the engine's app states and the cached catalog entry, never
// stored (data model AppRow). Each offered app has exactly one of four states; an app found on the reader that the catalog does not
// list has the separate label "not in the catalog" (a fifth label for those apps only; spec FR-008). Notes accompany a state. Every
// text from a catalog or a package passes store_text with the limits of the catalog contract.

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/install/app_state.h"
#include "pbstore/store/self_update_view.h"

namespace pbstore {
namespace store {

enum class RowState { NotInstalled, Installed, UpdateAvailable, InstalledOutside, NotInCatalog };
enum class RowAction { Install, Update, Remove, Check, Adopt, Replace };

const char* state_words(RowState state);   // "Not installed", "Installed", "Update available", "Installed outside the store", "Not in the catalog"
const char* state_icon(RowState state);    // the name of the icon (a separate role: the state is never colour or position only)
const char* action_words(RowAction action);

constexpr const char* kStoreAppId = "pbstore";

struct AppRow {
    std::string id, name, version, short_description;
    RowState state = RowState::NotInstalled;
    std::vector<std::string> notes;           // in the order of contracts/ui-pages.md
    std::string installed_version, offered_version;
    std::vector<RowAction> actions;
    bool is_store = false;                    // PB Store itself: Update, never Remove
    bool has_record = false;                  // an install record exists
    // the detail page (from the catalog entry; empty for an app that is not in the catalog)
    bool in_catalog = false;
    std::string long_description, license, author, source_url, changelog_url, min_firmware, package_url;
    int64_t package_size = 0;
};

struct RowContext {
    std::optional<Catalog> catalog;                       // the verified cached or fresh catalog, if any
    std::optional<SelfUpdateView> self_update;            // PB Store's own version note
    std::set<std::string> needs_repair;                   // ids of apps marked "needs repair" (E-INS-24)
};

// Builds, sorts (catalog apps by name ignoring case, then apps not in the catalog by name) and returns the rows.
std::vector<AppRow> build_rows(const std::vector<install::AppEntry>& entries, const RowContext& context);

}  // namespace store
}  // namespace pbstore
