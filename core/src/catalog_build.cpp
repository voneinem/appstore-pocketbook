#include "pbstore/catalog_build.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "pbstore/schema.h"
#include "pbstore/semver.h"

namespace pbstore {

bool BuildResult::ok() const {
    if (!catalog.has_value()) return false;
    for (const Finding& f : findings)
        if (f.severity == Severity::Error) return false;
    return true;
}

namespace {

std::string timestamp_utc(int64_t epoch) {
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool slurp(const std::filesystem::path& p, std::string* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// the versions that were ever listed, plus the current one, ascending
std::vector<std::string> union_versions(std::vector<std::string> listed, const std::string& current) {
    if (std::find(listed.begin(), listed.end(), current) == listed.end()) listed.push_back(current);
    std::sort(listed.begin(), listed.end(), [](const std::string& a, const std::string& b) {
        auto x = parse_semver(a), y = parse_semver(b);
        return x && y ? compare(*x, *y) < 0 : a < b;
    });
    return listed;
}

void copy_descriptive_fields(const Entry& e, CatalogEntry* out) {
    out->id = e.id;
    out->short_description = e.short_description;
    out->long_description = e.long_description;
    out->release_date = e.release_date;
    out->package_url = e.package_url;
    out->license = e.license;
    out->source_url = e.source_url;
    out->author = e.author;
    out->icon_url = e.icon_url;
    out->changelog_url = e.changelog_url;
    out->withdrawn_versions = e.withdrawn_versions;
}

}  // namespace

BuildResult build_catalog(const BuildOptions& options) {
    namespace fs = std::filesystem;
    BuildResult result;
    if (!options.first_publish && !options.live) {
        result.findings.push_back(make_error("E-BLD-03", "live catalog",
            "no live catalog is available and this is not a first publish; a build without the live catalog would lose the recorded hashes and could lower the serial"));
        return result;
    }

    // the entry files
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(options.entries_dir, ec)) {
        result.findings.push_back(make_error("E-BLD-07", options.entries_dir, "the entries directory cannot be read"));
        return result;
    }
    for (const auto& e : fs::directory_iterator(options.entries_dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());

    std::vector<CatalogEntry> apps;
    std::vector<std::string> failed;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        std::string bytes;
        if (!slurp(file, &bytes)) {
            result.findings.push_back(make_error("E-ENT-01", file.stem().string(), "the file cannot be read"));
            failed.push_back(file.stem().string());
            continue;
        }
        EntryParseResult parsed = parse_entry(bytes, name, options.today);
        const CatalogEntry* live_entry = nullptr;
        if (parsed.entry && options.live)
            for (const CatalogEntry& l : options.live->apps)
                if (l.id == parsed.entry->id && l.package_url == parsed.entry->package_url) live_entry = &l;

        if (parsed.entry && live_entry) {
            // unchanged package link: keep the recorded values, do not download
            for (Finding& f : parsed.findings) result.findings.push_back(std::move(f));
            for (Finding& f : compare_with_live(*parsed.entry, *options.live, file.stem().string())) result.findings.push_back(std::move(f));
            // the withdrawn versions of an unchanged entry must have been listed
            const std::vector<std::string> listed = union_versions(live_entry->listed_versions, live_entry->version);
            bool withdrawn_ok = true;
            for (const std::string& v : parsed.entry->withdrawn_versions)
                if (std::find(listed.begin(), listed.end(), v) == listed.end()) {
                    result.findings.push_back(make_error("E-ENT-11", file.stem().string(), "withdrawn_versions: the version " + v + " was never listed in the catalog, so it cannot be withdrawn"));
                    withdrawn_ok = false;
                }
            if (!withdrawn_ok) {
                failed.push_back(file.stem().string());
                continue;
            }
            CatalogEntry c;
            copy_descriptive_fields(*parsed.entry, &c);
            c.name = live_entry->name;
            c.version = live_entry->version;
            c.package_size = live_entry->package_size;
            c.package_sha256 = live_entry->package_sha256;
            c.min_firmware = live_entry->min_firmware;
            c.target_device = live_entry->target_device;
            c.menu_entry_id = live_entry->menu_entry_id;
            c.data_dir = live_entry->data_dir;
            c.listed_versions = union_versions(live_entry->listed_versions, c.version);
            apps.push_back(std::move(c));
            ++result.carried_over;
            continue;
        }

        // a new or changed entry: the full check, including the download
        PackageSource source;
        source.fetcher = options.fetcher;
        EntryCheckOptions check_options;
        check_options.today = options.today;
        // a first publish has an empty live catalog: nothing was listed before, so the checks that
        // compare with the live catalog (withdrawn versions, collisions) still run
        check_options.live = options.live ? options.live : std::optional<Catalog>(Catalog{});
        EntryCheckResult checked = check_entry(bytes, name, source, check_options);
        bool entry_failed = false;
        for (const Finding& f : checked.findings) {
            result.findings.push_back(f);
            if (f.severity == Severity::Error) entry_failed = true;
        }
        if (entry_failed || !checked.ok()) {
            failed.push_back(file.stem().string());
            continue;
        }
        ++result.downloaded;
        CatalogEntry c;
        copy_descriptive_fields(*checked.entry, &c);
        c.name = checked.manifest->name;
        c.version = checked.manifest->version;
        c.package_size = checked.package_size;
        c.package_sha256 = checked.package_sha256;
        c.min_firmware = checked.manifest->min_firmware;
        c.target_device = checked.manifest->target_device;
        c.menu_entry_id = checked.manifest->menu_entry.id;
        c.data_dir = checked.manifest->data_dir;
        std::vector<std::string> before;
        if (options.live)
            for (const CatalogEntry& l : options.live->apps)
                if (l.id == c.id) before = l.listed_versions;
        c.listed_versions = union_versions(before, c.version);
        apps.push_back(std::move(c));
    }

    if (!failed.empty()) {
        std::string names;
        for (size_t i = 0; i < failed.size(); ++i) names += (i ? ", " : "") + failed[i];
        result.findings.push_back(make_error("E-BLD-07", "build",
            std::to_string(failed.size()) + " entr" + (failed.size() == 1 ? "y" : "ies") + " failed the checks (" + names + "); nothing was built"));
        return result;
    }

    // uniqueness across apps
    std::map<std::string, std::string> menus, dirs;
    bool clash = false;
    for (const CatalogEntry& a : apps) {
        auto m = menus.emplace(a.menu_entry_id, a.id);
        if (!m.second) {
            result.findings.push_back(make_error("E-BLD-01", "build", "the apps \"" + m.first->second + "\" and \"" + a.id + "\" both claim the menu entry id " + a.menu_entry_id));
            clash = true;
        }
        if (a.data_dir) {
            auto d = dirs.emplace(*a.data_dir, a.id);
            if (!d.second) {
                result.findings.push_back(make_error("E-BLD-02", "build", "the apps \"" + d.first->second + "\" and \"" + a.id + "\" both claim the data folder " + *a.data_dir));
                clash = true;
            }
        }
    }
    if (clash) return result;

    Catalog catalog;
    catalog.schema_version = kSupportedSchema;
    catalog.serial = std::max<int64_t>(options.now, options.live ? options.live->serial + 1 : 1);
    catalog.generated_at = timestamp_utc(options.now);
    catalog.apps = std::move(apps);
    const std::string bytes = write_catalog(catalog);
    if (bytes.size() > kMaxCatalogBytes) {
        result.findings.push_back(make_error("E-BLD-06", "build", "the catalog would be " + std::to_string(bytes.size()) +
                                             " bytes, the limit is " + std::to_string(kMaxCatalogBytes)));
        return result;
    }
    result.catalog = std::move(catalog);
    if (!options.dry_run) result.catalog_bytes = bytes;
    return result;
}

}  // namespace pbstore
