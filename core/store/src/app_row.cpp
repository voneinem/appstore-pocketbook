#include "pbstore/store/app_row.h"

#include <algorithm>

#include "pbstore/store/app_filter.h"
#include "pbstore/store/store_text.h"

namespace pbstore {
namespace store {

const char* state_words(RowState s) {
    switch (s) {
        case RowState::NotInstalled: return "Not installed";
        case RowState::Installed: return "Installed";
        case RowState::UpdateAvailable: return "Update available";
        case RowState::InstalledOutside: return "Installed outside the store";
        case RowState::NotInCatalog: return "Not in the catalog";
    }
    return "";
}

const char* state_icon(RowState s) {
    switch (s) {
        case RowState::NotInstalled: return "state-not-installed";
        case RowState::Installed: return "state-installed";
        case RowState::UpdateAvailable: return "state-update";
        case RowState::InstalledOutside: return "state-outside";
        case RowState::NotInCatalog: return "state-unknown";
    }
    return "";
}

const char* action_words(RowAction a) {
    switch (a) {
        case RowAction::Install: return "Install";
        case RowAction::Update: return "Update";
        case RowAction::Remove: return "Remove";
        case RowAction::Check: return "Check";
        case RowAction::Adopt: return "Adopt";
    }
    return "";
}

namespace {

RowState state_of(const install::AppEntry& e) {
    switch (e.state) {
        case install::AppStateKind::NotInstalled: return RowState::NotInstalled;
        case install::AppStateKind::OutsideInstaller: return RowState::InstalledOutside;
        case install::AppStateKind::UnknownApp: return RowState::NotInCatalog;
        case install::AppStateKind::Installed: break;
    }
    if (e.not_in_catalog) return RowState::NotInCatalog;          // installed with a record, but the catalog does not list it
    return e.update_available ? RowState::UpdateAvailable : RowState::Installed;
}

const CatalogEntry* entry_of(const std::optional<Catalog>& catalog, const std::string& id) {
    if (!catalog) return nullptr;
    for (const CatalogEntry& c : catalog->apps)
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace

std::vector<AppRow> build_rows(const std::vector<install::AppEntry>& entries, const RowContext& ctx) {
    std::vector<AppRow> rows;
    for (const install::AppEntry& e : entries) {
        AppRow r;
        r.id = e.id;
        r.is_store = e.id == kStoreAppId;
        r.name = store_text(e.name.empty() ? e.id : e.name, kMaxNameChars);
        r.short_description = store_text(e.description, kMaxShortDescriptionChars);
        r.state = state_of(e);
        r.has_record = e.state == install::AppStateKind::Installed;
        r.installed_version = store_text(e.installed_version, 32);
        r.offered_version = e.offered ? store_text(e.offered_version, 32) : "";
        r.version = !r.installed_version.empty() ? r.installed_version : store_text(e.offered_version, 32);

        // the notes, in the order of contracts/ui-pages.md
        if (r.is_store && ctx.self_update && !ctx.self_update->list_note.empty()) r.notes.push_back(ctx.self_update->list_note);
        if (ctx.needs_repair.count(e.id)) r.notes.push_back("needs repair");
        if (e.installed_version_withdrawn) r.notes.push_back("this version was withdrawn");
        if (e.changed) r.notes.push_back("changed since install");
        if (e.from_local) r.notes.push_back("from a local file");

        // the actions that fit the state (a repair pending is handled by the service)
        switch (r.state) {
            case RowState::NotInstalled:
                if (e.offered) r.actions.push_back(RowAction::Install);
                break;
            case RowState::Installed:
                r.actions.push_back(RowAction::Remove);
                break;
            case RowState::UpdateAvailable:
                r.actions.push_back(RowAction::Update);
                r.actions.push_back(RowAction::Remove);
                break;
            case RowState::InstalledOutside:
                r.actions.push_back(RowAction::Check);
                break;
            case RowState::NotInCatalog:
                if (r.has_record) r.actions.push_back(RowAction::Remove);
                break;
        }
        if (r.is_store) r.actions.erase(std::remove(r.actions.begin(), r.actions.end(), RowAction::Remove), r.actions.end());   // never removes itself

        if (const CatalogEntry* c = entry_of(ctx.catalog, e.id)) {
            r.in_catalog = true;
            r.long_description = store_text(c->long_description.en, kMaxLongDescriptionChars);
            r.license = store_text(c->license, 40);
            r.author = store_text(c->author, kMaxAuthorChars);
            r.source_url = store_text(c->source_url, 200);
            r.changelog_url = store_text(c->changelog_url, 200);
            r.min_firmware = store_text(c->min_firmware, 32);
            r.package_url = store_text(c->package_url, 300);
            r.package_size = c->package_size;
        }
        rows.push_back(std::move(r));
    }
    sort_rows(&rows);
    return rows;
}

}  // namespace store
}  // namespace pbstore
