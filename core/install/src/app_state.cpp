#include "pbstore/install/app_state.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "pbstore/semver.h"

namespace pbstore {
namespace install {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool newer(const std::string& offered, const std::string& installed) {
    auto a = parse_semver(offered);
    auto b = parse_semver(installed);
    return a && b && compare(*a, *b) > 0;
}

}  // namespace

std::vector<AppEntry> derive_app_states(const Catalog* catalog, const ReaderSnapshot& snap) {
    std::vector<AppEntry> out;
    std::set<std::string> menu_ids_of_listed;   // entries accounted for (recorded or belonging to a catalog app)

    // 1. every app of the catalog
    if (catalog) {
        for (const CatalogEntry& e : catalog->apps) {
            AppEntry a;
            a.id = e.id;
            a.name = e.name;
            a.offered_version = e.version;
            a.description = e.short_description.en;
            a.offered = offered(e);
            auto rec = snap.installed.find(e.id);
            if (rec != snap.installed.end()) {
                const InstalledApp& inst = rec->second;
                a.state = AppStateKind::Installed;
                a.installed_version = inst.record.version;
                a.source = inst.record.source;
                a.from_local = inst.record.source == "local";
                a.changed = !inst.changed_files.empty();
                a.changed_files = inst.changed_files;
                a.update_available = a.offered && newer(e.version, inst.record.version);
                a.installed_version_withdrawn = is_withdrawn(e, inst.record.version);
                a.menu_entry_missing = snap.desktop && !snap.desktop->has_entry(inst.record.menu_entry.id);
                menu_ids_of_listed.insert(inst.record.menu_entry.id);
            } else if (snap.desktop && snap.desktop->has_entry(e.menu_entry_id)) {
                a.state = AppStateKind::OutsideInstaller;
                menu_ids_of_listed.insert(e.menu_entry_id);
            } else {
                a.state = AppStateKind::NotInstalled;
            }
            out.push_back(a);
        }
    }

    // 2. installed apps the catalog does not list
    for (const auto& kv : snap.installed) {
        bool listed = false;
        for (const AppEntry& a : out) listed = listed || a.id == kv.first;
        if (listed) continue;
        const InstallRecord& r = kv.second.record;
        AppEntry a;
        a.id = kv.first;
        a.name = r.menu_entry.title.empty() ? kv.first : r.menu_entry.title;
        a.state = AppStateKind::Installed;
        a.installed_version = r.version;
        a.source = r.source;
        a.from_local = r.source == "local";
        a.not_in_catalog = true;
        a.changed = !kv.second.changed_files.empty();
        a.changed_files = kv.second.changed_files;
        a.menu_entry_missing = snap.desktop && !snap.desktop->has_entry(r.menu_entry.id);
        menu_ids_of_listed.insert(r.menu_entry.id);
        out.push_back(a);
    }

    // 3. U_ entries that are neither recorded nor belong to a catalog app (the vendor's own entries are not listed)
    if (snap.desktop) {
        const json::Value* apps = snap.desktop->tree.find("applications");
        if (apps) {
            for (const auto& kv : apps->object) {
                if (kv.first.compare(0, 2, "U_") != 0 || menu_ids_of_listed.count(kv.first)) continue;
                AppEntry a;
                a.id = kv.first;
                a.name = kv.second.get_string("title").value_or(kv.first);
                a.state = AppStateKind::UnknownApp;
                a.not_in_catalog = true;
                out.push_back(a);
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const AppEntry& x, const AppEntry& y) {
        const std::string a = lower(x.name), b = lower(y.name);
        return a != b ? a < b : x.id < y.id;
    });
    return out;
}

}  // namespace install
}  // namespace pbstore
