// check-entry (docs/contracts/cli.md): the entry rules, the package rules and the cross-checks for
// one entry file, with the notices for the reviewer.
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "common.h"
#include "pbstore/catalog_verify.h"
#include "pbstore/entry_check.h"
#include "pbstore/live_catalog.h"

namespace {

using pbcli::Args;

std::string today_utc() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

// Values that come from entries and manifests are shown in a Markdown table: no pipes, backticks or
// line breaks may break out of a cell.
std::string md_cell(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\|";
        else if (c == '`') out += '\'';
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}

// Appends a Markdown section to $GITHUB_STEP_SUMMARY (the job summary of the pull-request check).
void write_summary(const std::string& entry_path, const pbstore::EntryCheckResult& r) {
    const char* path = std::getenv("GITHUB_STEP_SUMMARY");
    if (!path || !*path) return;
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    const std::string id = r.entry ? r.entry->id : std::filesystem::path(entry_path).stem().string();
    out << "### " << md_cell(id) << " (`" << md_cell(entry_path) << "`)\n\n";
    if (r.manifest) {
        out << "| | |\n|---|---|\n"
            << "| App | " << md_cell(r.manifest->name) << " " << md_cell(r.manifest->version) << " |\n"
            << "| Package size | " << r.package_size << " bytes |\n"
            << "| Package SHA-256 | `" << r.package_sha256 << "` |\n";
        if (r.entry) out << "| Package | " << md_cell(r.entry->package_url) << " |\n";
        out << "\n";
    }
    std::string failed, notices;
    for (const auto& f : r.findings) {
        std::string text = f.subject + ": " + f.message;
        for (char& c : text) if (c == '\n' || c == '\r') c = ' ';
        const std::string line = "- `" + f.rule_id + "` " + text + "\n";
        (f.severity == pbstore::Severity::Error ? failed : notices) += line;
    }
    if (!failed.empty()) out << "**Failed**\n\n" << failed << "\n";
    if (!notices.empty()) out << "**Needs reviewer attention**\n\n" << notices << "\n";
    if (failed.empty()) out << "All checks passed.\n\n";
}

int cmd_check_entry(const Args& a) {
    if (a.positional.size() != 1) return pbcli::usage_error("check-entry needs one entry file: check-entry ENTRY.json [--package FILE.zip] [--live-url URL | --live-dir DIR --keys DIR]");
    if (a.has("live-url") && a.has("live-dir")) return pbcli::usage_error("use either --live-url or --live-dir, not both");
    const bool live_requested = a.has("live-url") || a.has("live-dir");
    if (live_requested && !a.has("keys")) return pbcli::usage_error("--live-url and --live-dir need --keys DIR (the trusted public keys)");
    std::string entry_bytes, error;
    if (!pbcli::read_file(a.positional[0], &entry_bytes, &error)) return pbcli::usage_error(error);

    pbstore::PackageSource source;
    if (a.has("package")) {
        std::string zip;
        if (!pbcli::read_file(a.value("package"), &zip, &error)) return pbcli::usage_error(error);
        source.local_zip = std::move(zip);
    } else {
        source.fetcher = pbstore::make_https_fetcher();
    }

    pbstore::EntryCheckOptions options;
    options.today = today_utc();
    std::vector<pbstore::Finding> extra;
    if (live_requested && !a.flag("first-publish")) {
        const std::vector<std::string> keys = pbstore::load_trusted_keys(a.value("keys"), &error);
        if (keys.empty()) return pbcli::usage_error(error);
        pbstore::LiveCatalogResult live = a.has("live-dir")
            ? pbstore::load_live_catalog_dir(a.value("live-dir"), keys)
            : pbstore::load_live_catalog_url(a.value("live-url"), keys, pbstore::make_https_fetcher());
        for (const auto& f : live.findings) extra.push_back(f);   // a live catalog that was asked for but cannot be used fails the check
        options.live = std::move(live.catalog);
    }

    const std::string file_name = std::filesystem::path(a.positional[0]).filename().string();
    pbstore::EntryCheckResult r = pbstore::check_entry(entry_bytes, file_name, source, options);
    r.findings.insert(r.findings.begin(), extra.begin(), extra.end());
    pbcli::report(r.findings, a, a.positional[0]);
    write_summary(a.positional[0], r);
    const int code = pbcli::exit_code(r.findings);
    if (code == 0 && r.entry && r.manifest)
        std::cout << "entry ok: " << a.positional[0] << "\n"
                  << "app: " << r.manifest->id << " " << r.manifest->version << "\n"
                  << "size: " << r.package_size << " bytes\n"
                  << "sha256: " << r.package_sha256 << "\n";
    return code;
}

pbcli::CommandRegistrar reg("check-entry",
    "check one entry file: check-entry ENTRY.json [--package FILE.zip] [--live-url URL | --live-dir DIR] [--keys DIR] [--first-publish] [--github]",
    cmd_check_entry);

}  // namespace
