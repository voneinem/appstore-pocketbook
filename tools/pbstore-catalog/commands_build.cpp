// build (docs/contracts/cli.md): rebuild the unsigned catalog from all entry files.
#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <filesystem>
#include <iostream>

#include "common.h"
#include "pbstore/catalog_build.h"
#include "pbstore/catalog_verify.h"
#include "pbstore/live_catalog.h"
#include "pbstore/validate.h"

namespace {

using pbcli::Args;
namespace fs = std::filesystem;

std::string today_utc(std::time_t now) {
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

// For tests and offline rehearsals: the package of an entry is read from DIR/<asset file name of
// package_url> instead of being downloaded. Every package check still applies.
pbstore::Fetcher directory_fetcher(const std::string& dir) {
    return [dir](const std::string& url, uint64_t max_bytes) {
        pbstore::FetchResult r;
        const std::string asset = url.substr(url.rfind('/') + 1);
        std::string bytes, error;
        if (asset.empty() || !pbcli::read_file(dir + "/" + asset, &bytes, &error)) {
            r.kind = pbstore::FetchKind::Unreachable;
            r.error = "the package " + asset + " is not in the package directory " + dir;
            return r;
        }
        if (bytes.size() > max_bytes) {
            r.kind = pbstore::FetchKind::TooLarge;
            r.error = "the package is larger than " + std::to_string(max_bytes) + " bytes";
            return r;
        }
        r.ok = true;
        r.http_status = 200;
        r.body = std::move(bytes);
        return r;
    };
}

int cmd_build(const Args& a) {
    if (!a.has("entries")) return pbcli::usage_error("build needs --entries DIR");
    if (a.has("out") == a.flag("dry-run")) return pbcli::usage_error("give exactly one of --out DIR and --dry-run");
    const bool first = a.flag("first-publish");
    if (a.has("live-url") && a.has("live-dir")) return pbcli::usage_error("use either --live-url or --live-dir, not both");
    if (!first && !a.has("live-url") && !a.has("live-dir"))
        return pbcli::usage_error("build needs the live catalog (--live-url URL or --live-dir DIR with --keys DIR) or --first-publish");
    if (!first && !a.has("keys")) return pbcli::usage_error("--live-url and --live-dir need --keys DIR (the trusted public keys)");
    std::error_code ec;
    if (!fs::is_directory(a.value("entries"), ec)) return pbcli::usage_error("entries directory not found: " + a.value("entries"));
    std::time_t now = std::time(nullptr);
    if (a.has("now")) {
        const std::string n = a.value("now");
        if (n.empty() || n.size() > 12 || n.find_first_not_of("0123456789") != std::string::npos) return pbcli::usage_error("--now must be a number of seconds since 1970");
        now = static_cast<std::time_t>(std::stoll(n));
    }

    pbstore::BuildOptions options;
    options.entries_dir = a.value("entries");
    options.first_publish = first;
    options.dry_run = a.flag("dry-run");
    options.now = static_cast<int64_t>(now);
    options.today = today_utc(std::time(nullptr));   // the date check uses the real clock; --now only sets the serial and generated_at
    options.fetcher = a.has("package-dir") ? directory_fetcher(a.value("package-dir")) : pbstore::make_https_fetcher();

    std::vector<pbstore::Finding> findings;
    if (!first) {
        std::string error;
        const std::vector<std::string> keys = pbstore::load_trusted_keys(a.value("keys"), &error);
        if (keys.empty()) return pbcli::usage_error(error);
        pbstore::LiveCatalogResult live = a.has("live-dir")
            ? pbstore::load_live_catalog_dir(a.value("live-dir"), keys)
            : pbstore::load_live_catalog_url(a.value("live-url"), keys, pbstore::make_https_fetcher());
        if (!live.catalog) {
            pbcli::report(live.findings, a);
            return 1;
        }
        options.live = std::move(live.catalog);
    }

    pbstore::BuildResult r = pbstore::build_catalog(options);
    findings.insert(findings.end(), r.findings.begin(), r.findings.end());
    pbcli::report(findings, a);
    const int code = pbcli::exit_code(findings);
    if (code != 0 || !r.catalog) return code != 0 ? code : 1;

    if (options.dry_run) {
        std::cout << "dry run ok: serial " << r.catalog->serial << ", " << r.catalog->apps.size() << " app(s) ("
                  << r.carried_over << " carried over, " << r.downloaded << " downloaded); nothing was written\n";
        return 0;
    }
    const std::string out_dir = a.value("out");
    ::mkdir(out_dir.c_str(), 0755);
    if (!fs::is_directory(out_dir, ec)) return pbcli::usage_error("cannot create the output directory " + out_dir);
    // the signature of an older catalog must never stay next to a new one
    ::unlink((out_dir + "/catalog.json.sig").c_str());
    const std::string tmp = out_dir + "/catalog.json.tmp", final_path = out_dir + "/catalog.json";
    std::string error;
    if (!pbcli::write_file(tmp, r.catalog_bytes, &error)) return pbcli::usage_error(error);
    if (::rename(tmp.c_str(), final_path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return pbcli::usage_error("cannot write " + final_path);
    }
    std::cout << "catalog built: " << final_path << "\n"
              << "serial: " << r.catalog->serial << ", " << r.catalog->apps.size() << " app(s) ("
              << r.carried_over << " carried over, " << r.downloaded << " downloaded)\n"
              << "next: sign it with `pbstore-catalog sign`, then verify it with `pbstore-catalog verify`\n";
    return 0;
}

pbcli::CommandRegistrar reg("build",
    "rebuild the unsigned catalog: build --entries DIR (--out DIR | --dry-run) (--live-url URL | --live-dir DIR | --first-publish) [--keys DIR] [--github]  (tests only: --package-dir DIR, --now EPOCH)",
    cmd_build);

}  // namespace
