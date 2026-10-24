#include "pbstore/entry_check.h"

#include <algorithm>

#include "pbstore/package_check.h"
#include "pbstore/validate.h"

namespace pbstore {

bool EntryCheckResult::ok() const {
    if (!entry.has_value() || !manifest.has_value()) return false;
    for (const Finding& f : findings)
        if (f.severity == Severity::Error) return false;
    return true;
}

namespace {

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string stem_of(const std::string& file_name) {
    const size_t dot = file_name.rfind('.');
    return dot == std::string::npos || dot == 0 ? file_name : file_name.substr(0, dot);
}

std::string repo_name(const std::string& owner, const std::string& repo) { return owner + "/" + repo; }

}  // namespace

std::vector<Finding> compare_with_live(const Entry& entry, const Catalog& live, const std::string& subject) {
    std::vector<Finding> found;
    std::vector<Finding>* out = &found;
    for (const CatalogEntry& l : live.apps) {
        if (l.id != entry.id) continue;
        auto new_rel = validate::parse_github_release_url(entry.package_url);
        auto old_rel = validate::parse_github_release_url(l.package_url);
        if (new_rel && old_rel &&
            (lower_ascii(new_rel->owner) != lower_ascii(old_rel->owner) || lower_ascii(new_rel->repo) != lower_ascii(old_rel->repo)))
            out->push_back(make_notice("W-ENT-01", subject, "package_url repository changed from " + repo_name(old_rel->owner, old_rel->repo) +
                                       " to " + repo_name(new_rel->owner, new_rel->repo) + "; check that the new owner is the app's author"));
        auto new_src = validate::parse_github_repo_url(entry.source_url);
        auto old_src = validate::parse_github_repo_url(l.source_url);
        if (new_src && old_src) {
            if (lower_ascii(new_src->owner) != lower_ascii(old_src->owner) || lower_ascii(new_src->repo) != lower_ascii(old_src->repo))
                out->push_back(make_notice("W-ENT-01", subject, "source_url repository changed from " + repo_name(old_src->owner, old_src->repo) +
                                           " to " + repo_name(new_src->owner, new_src->repo)));
        } else if (entry.source_url != l.source_url) {
            out->push_back(make_notice("W-ENT-01", subject, "source_url changed from " + l.source_url + " to " + entry.source_url));
        }
        break;
    }
    return found;
}


EntryCheckResult check_entry(const std::string& entry_bytes, const std::string& file_name, const PackageSource& source,
                             const EntryCheckOptions& options) {
    EntryCheckResult r;
    const std::string subject = stem_of(file_name);
    EntryParseResult parsed = parse_entry(entry_bytes, file_name, options.today);
    r.findings = parsed.findings;
    if (!parsed.entry) return r;                      // an invalid entry is not downloaded
    r.entry = parsed.entry;
    const Entry& entry = *parsed.entry;

    // the package
    std::string zip;
    if (source.local_zip) {
        zip = *source.local_zip;
    } else {
        if (!source.fetcher) {
            r.findings.push_back(make_error("E-ENT-12", subject, "package cannot be downloaded: no download function available"));
            return r;
        }
        FetchResult f = source.fetcher(entry.package_url, static_cast<uint64_t>(kMaxPackageBytes));
        if (!f.ok) {
            r.findings.push_back(make_error("E-ENT-12", subject, "package cannot be downloaded from " + entry.package_url + ": " + f.error));
            return r;
        }
        zip = std::move(f.body);
    }
    PackageCheckResult pc = check_package(zip);
    r.package_size = pc.package_size;
    r.package_sha256 = pc.package_sha256;
    for (Finding f : pc.findings) {
        f.message = f.subject + ": " + f.message;     // keep the file or field visible, name the entry as subject
        f.subject = subject;
        r.findings.push_back(std::move(f));
    }
    if (!pc.manifest) return r;
    r.manifest = pc.manifest;

    // cross-checks between entry and manifest
    if (pc.manifest->id != entry.id)
        r.findings.push_back(make_error("E-ENT-13", subject, "the entry id is \"" + entry.id + "\" but the package manifest id is \"" + pc.manifest->id + "\""));
    if (auto rel = validate::parse_github_release_url(entry.package_url)) {
        std::string tag = rel->tag;
        if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
        if (tag != pc.manifest->version)
            r.findings.push_back(make_error("E-ENT-14", subject, "the release tag \"" + rel->tag + "\" does not match the manifest version \"" + pc.manifest->version +
                                            "\" (the tag must equal the version, optionally with one leading v)"));
    }

    // comparison with the live catalog
    if (!options.live) {
        std::string note = "the comparison with the live catalog was skipped: no live catalog available";
        if (!entry.withdrawn_versions.empty()) note += "; the existence of the withdrawn versions was not checked";
        r.findings.push_back(make_notice("W-ENT-01", subject, note));
        return r;
    }
    const Catalog& live = *options.live;
    for (Finding& f : compare_with_live(entry, live, subject)) r.findings.push_back(std::move(f));
    const CatalogEntry* same = nullptr;
    for (const CatalogEntry& l : live.apps)
        if (l.id == entry.id) same = &l;
    // withdrawn versions must have been listed (the version this package carries counts as listed)
    std::vector<std::string> listed = same ? same->listed_versions : std::vector<std::string>();
    listed.push_back(pc.manifest->version);
    for (const std::string& v : entry.withdrawn_versions)
        if (std::find(listed.begin(), listed.end(), v) == listed.end())
            r.findings.push_back(make_error("E-ENT-11", subject, "withdrawn_versions: the version " + v + " was never listed in the catalog, so it cannot be withdrawn"));
    // a changed package link must not lead to a version that is withdrawn
    const bool link_changed = !same || same->package_url != entry.package_url;
    if (link_changed && std::find(entry.withdrawn_versions.begin(), entry.withdrawn_versions.end(), pc.manifest->version) != entry.withdrawn_versions.end())
        r.findings.push_back(make_error("E-BLD-05", subject, "package_url now points to version " + pc.manifest->version + ", which is on the entry's withdrawn_versions; " +
                                        "point the entry at a version that is not withdrawn"));
    // the menu entry id and the data folder belong to one app
    for (const CatalogEntry& l : live.apps) {
        if (l.id == entry.id) continue;
        if (l.menu_entry_id == pc.manifest->menu_entry.id)
            r.findings.push_back(make_error("E-BLD-01", subject, "the menu entry id " + l.menu_entry_id + " is already claimed by the listed app \"" + l.id + "\""));
        if (l.data_dir && pc.manifest->data_dir && *l.data_dir == *pc.manifest->data_dir)
            r.findings.push_back(make_error("E-BLD-02", subject, "the data folder " + *l.data_dir + " is already claimed by the listed app \"" + l.id + "\""));
    }
    return r;
}

}  // namespace pbstore
