#include "pbstore/install/adoption.h"

#include <set>

#include "pbstore/install/desktop_config.h"

namespace pbstore {
namespace install {

AdoptionCheck compare_to_package(const ReaderSnapshot& snap, const Manifest& m) {
    AdoptionCheck c;
    std::set<std::string> in_package;
    for (const FileEntry& f : m.files) {
        in_package.insert(f.path);
        auto it = snap.facts.find(f.path);
        if (it == snap.facts.end() || it->second.kind != Kind::File) c.missing.push_back(f.path);
        else if (it->second.size != static_cast<uint64_t>(f.size) || it->second.sha256 != f.sha256) c.differing.push_back(f.path);
    }
    // files in the app's own folder (applications/<id>/) that the package does not list: left alone, not claimed
    const std::string own = "applications/" + m.id;
    for (const auto& kv : snap.listings) {
        if (kv.first != own && kv.first.compare(0, own.size() + 1, own + "/") != 0) continue;
        for (const DirEntry& e : kv.second) {
            const std::string path = join_path(kv.first, e.name);
            if (e.kind != Kind::Dir && !in_package.count(path)) c.extra.push_back(path);
        }
    }
    if (!snap.desktop) {
        c.entry_problem = "the desktop configuration cannot be used";
    } else {
        const json::Value* apps = snap.desktop->tree.find("applications");
        const json::Value* entry = apps ? apps->find(m.menu_entry.id) : nullptr;
        if (!entry) c.entry_problem = "the menu entry " + m.menu_entry.id + " is not in the desktop configuration";
        else if (entry->get_string("path").value_or("") != std::string(kDeviceRootPrefix) + m.menu_entry.path)
            c.entry_problem = "the menu entry " + m.menu_entry.id + " does not point at " + std::string(kDeviceRootPrefix) + m.menu_entry.path;
    }
    c.adoptable = c.missing.empty() && c.differing.empty() && c.entry_problem.empty();
    return c;
}

std::string AdoptionCheck::text() const {
    std::string t;
    for (const std::string& p : differing) t += (t.empty() ? "" : "; ") + p + " differs from the catalog package";
    for (const std::string& p : missing) t += (t.empty() ? "" : "; ") + p + " is missing";
    if (!entry_problem.empty()) t += (t.empty() ? "" : "; ") + entry_problem;
    if (t.empty()) t = "all files are equal to the catalog package";
    return t;
}

}  // namespace install
}  // namespace pbstore
