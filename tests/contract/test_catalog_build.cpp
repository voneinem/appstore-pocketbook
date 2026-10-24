// T063: build_catalog: carry-over, serial, uniqueness, all-or-nothing (docs/contracts/catalog-format.md).
#include "pbstore/catalog_build.h"
#include "pbstore/catalog_verify.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "keys.h"
#include "sample_app.h"
#include "sample_catalog.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <map>

using namespace pbstore;
using json::Value;

namespace {

struct World {
    pbtest::TmpDir dir;
    std::map<std::string, std::string> packages;   // package_url -> zip
    std::vector<std::string> fetched;               // urls the fetcher was asked for

    // Adds entries/<id>.json and the package it points to.
    void add(const std::string& id, const std::string& version, const std::string& menu_id, const std::string& data_dir,
             std::function<void(Value&)> mutate_entry = nullptr) {
        Value e = pbtest::sample_entry_doc(id, version);
        if (mutate_entry) mutate_entry(e);
        dir.write_p("entries/" + id + ".json", json::write(e));
        packages[*e.get_string("package_url")] = pbtest::build_package(pbtest::make_sample_app(id, version, menu_id, data_dir, 3));
    }
    void add_default(const std::string& id, const std::string& version = "1.2.0") {
        std::string menu = "U_";
        bool up = true;
        for (char c : id) { if (c == '-') { up = true; continue; } menu += up ? char(std::toupper(c)) : c; up = false; }
        add(id, version, menu, "system/" + id);
    }
    Fetcher fetcher() {
        return [this](const std::string& url, uint64_t) {
            fetched.push_back(url);
            FetchResult r;
            auto it = packages.find(url);
            if (it == packages.end()) { r.kind = FetchKind::HttpStatus; r.http_status = 404; r.error = "the server answered with status 404"; return r; }
            r.ok = true; r.http_status = 200; r.body = it->second;
            return r;
        };
    }
    BuildOptions options(int64_t now = 1792843200) {
        BuildOptions o;
        o.entries_dir = dir.file("entries");
        o.first_publish = true;
        o.now = now;
        o.today = "2026-10-24";
        o.fetcher = fetcher();
        return o;
    }
};

bool has(const BuildResult& r, const char* rule, const std::string& needle = "") {
    for (const Finding& f : r.findings)
        if (f.rule_id == rule && (f.message.find(needle) != std::string::npos || f.subject.find(needle) != std::string::npos)) return true;
    return false;
}
std::string dump(const BuildResult& r) {
    std::string s;
    for (const Finding& f : r.findings) s += format_finding(f) + " | ";
    return s;
}
size_t count_fetched(const World& w, const std::string& id) {
    size_t n = 0;
    for (const std::string& u : w.fetched) if (u.find("/" + id + "/") != std::string::npos) ++n;
    return n;
}

}  // namespace

PB_TEST(entries_become_catalog_entries_sorted_by_id_with_fields_from_the_right_sources) {
    World w;
    w.add_default("beta-app", "2.0.1");
    w.add_default("alpha-app");
    BuildResult r = build_catalog(w.options());
    PB_CHECK(r.ok());
    PB_CHECK(r.catalog.has_value());
    const Catalog& c = *r.catalog;
    PB_CHECK_EQ(c.schema_version, std::string("1.0"));
    PB_CHECK_EQ(c.serial, int64_t(1792843200));
    PB_CHECK_EQ(c.generated_at, std::string("2026-10-24T12:00:00Z"));
    PB_CHECK_EQ(c.apps.size(), size_t(2));
    PB_CHECK_EQ(c.apps[0].id, std::string("alpha-app"));
    PB_CHECK_EQ(c.apps[1].id, std::string("beta-app"));
    const CatalogEntry& a = c.apps[1];
    const std::string& zip = w.packages[a.package_url];
    PB_CHECK_EQ(a.version, std::string("2.0.1"));                 // from the manifest
    PB_CHECK_EQ(a.name, std::string("App beta-app"));             // from the manifest
    PB_CHECK_EQ(a.min_firmware, std::string("6.8.0"));
    PB_CHECK_EQ(a.target_device, std::string("InkPad One"));
    PB_CHECK_EQ(a.menu_entry_id, std::string("U_BetaApp"));
    PB_CHECK(a.data_dir && *a.data_dir == "system/beta-app");
    PB_CHECK_EQ(a.package_size, static_cast<int64_t>(zip.size())); // computed from the download
    PB_CHECK_EQ(a.package_sha256, sha256_hex(zip));
    PB_CHECK_EQ(a.author, std::string("Example Author"));          // from the entry file
    PB_CHECK_EQ(a.release_date, std::string("2026-10-20"));
    PB_CHECK_EQ(a.listed_versions, std::vector<std::string>({"2.0.1"}));
    PB_CHECK(a.withdrawn_versions.empty());
    PB_CHECK_EQ(w.fetched.size(), size_t(2));
    // what was built is a valid catalog that a consumer accepts
    PB_CHECK(parse_catalog(r.catalog_bytes).catalog.has_value());
    PB_CHECK_EQ(r.catalog_bytes, write_catalog(c));
}

static Catalog previous_build(World& w, int64_t now) {
    BuildResult first = build_catalog(w.options(now));
    if (!first.ok()) throw pbtest::Failure{"first build failed: " + dump(first)};
    return *first.catalog;
}

PB_TEST(an_unchanged_entry_keeps_its_recorded_values_and_is_not_downloaded_again) {
    World w;
    w.add_default("alpha-app");
    w.add_default("beta-app", "2.0.1");
    Catalog live = previous_build(w, 1000);
    // make the recorded values distinguishable from anything a download would produce
    for (CatalogEntry& e : live.apps) if (e.id == "alpha-app") { e.package_size = 111; e.package_sha256 = std::string(64, 'a'); e.listed_versions = {"1.0.0", "1.1.0", "1.2.0"}; }
    w.fetched.clear();
    BuildOptions o = w.options(2000);
    o.first_publish = false;
    o.live = live;
    BuildResult r = build_catalog(o);
    PB_CHECK(r.ok());
    PB_CHECK_EQ(w.fetched.size(), size_t(0));                       // nothing downloaded at all
    for (const CatalogEntry& e : r.catalog->apps)
        if (e.id == "alpha-app") {
            PB_CHECK_EQ(e.package_size, int64_t(111));
            PB_CHECK_EQ(e.package_sha256, std::string(64, 'a'));
            PB_CHECK_EQ(e.listed_versions, std::vector<std::string>({"1.0.0", "1.1.0", "1.2.0"}));
        }
}

PB_TEST(a_new_or_changed_entry_is_downloaded_once_and_the_versions_ever_listed_grow) {
    World w;
    w.add_default("alpha-app", "1.0.0");
    Catalog live = previous_build(w, 1000);
    w.add_default("alpha-app", "1.1.0");                            // the entry now points at the new release
    w.add_default("beta-app", "2.0.1");                             // and a new app appears
    w.fetched.clear();
    BuildOptions o = w.options(2000);
    o.first_publish = false;
    o.live = live;
    BuildResult r = build_catalog(o);
    PB_CHECK(r.ok());
    PB_CHECK_EQ(count_fetched(w, "alpha-app"), size_t(1));
    PB_CHECK_EQ(count_fetched(w, "beta-app"), size_t(1));
    PB_CHECK_EQ(r.catalog->apps[0].version, std::string("1.1.0"));
    PB_CHECK_EQ(r.catalog->apps[0].listed_versions, std::vector<std::string>({"1.0.0", "1.1.0"}));
    PB_CHECK_EQ(r.catalog->apps[1].listed_versions, std::vector<std::string>({"2.0.1"}));
    // an app whose entry file was removed disappears from the catalog
    BuildOptions removed = w.options(3000);
    removed.first_publish = false;
    removed.live = *r.catalog;
    ::unlink(w.dir.file("entries/beta-app.json").c_str());
    BuildResult r2 = build_catalog(removed);
    PB_CHECK(r2.ok());
    PB_CHECK_EQ(r2.catalog->apps.size(), size_t(1));
}

PB_TEST(descriptive_fields_always_come_from_the_entry_file) {
    World w;
    w.add_default("alpha-app");
    Catalog live = previous_build(w, 1000);
    w.add("alpha-app", "1.2.0", "U_AlphaApp", "system/alpha-app", [](Value& e) {
        e.set("author", Value::from_string("New Author"));
        Value wv = Value::make_array(); wv.push(Value::from_string("1.2.0")); e.set("withdrawn_versions", wv);
    });
    w.fetched.clear();
    BuildOptions o = w.options(2000);
    o.first_publish = false;
    o.live = live;
    BuildResult r = build_catalog(o);
    PB_CHECK(r.ok());
    PB_CHECK_EQ(w.fetched.size(), size_t(0));                       // same package link: carried over
    PB_CHECK_EQ(r.catalog->apps[0].author, std::string("New Author"));
    PB_CHECK_EQ(r.catalog->apps[0].withdrawn_versions, std::vector<std::string>({"1.2.0"}));
    PB_CHECK(!offered(r.catalog->apps[0]));
}

PB_TEST(the_serial_never_decreases_and_never_repeats) {
    World w;
    w.add_default("alpha-app");
    Catalog live = pbtest::sample_catalog(5000);
    live.apps.clear();
    BuildOptions low = w.options(1000);
    low.first_publish = false;
    low.live = live;
    PB_CHECK_EQ(build_catalog(low).catalog->serial, int64_t(5001));          // live + 1 beats a clock that is behind
    BuildOptions high = w.options(9000);
    high.first_publish = false;
    high.live = live;
    PB_CHECK_EQ(build_catalog(high).catalog->serial, int64_t(9000));         // otherwise the clock
    BuildOptions same = w.options(5000);
    same.first_publish = false;
    same.live = live;
    PB_CHECK_EQ(build_catalog(same).catalog->serial, int64_t(5001));         // the same second as the live catalog
    // twenty builds in the same second, each on the previous result: strictly increasing
    Catalog cur = previous_build(w, 7000);
    for (int i = 0; i < 20; ++i) {
        BuildOptions o = w.options(7000);
        o.first_publish = false;
        o.live = cur;
        BuildResult r = build_catalog(o);
        PB_CHECK(r.ok());
        PB_CHECK(r.catalog->serial > cur.serial);
        cur = *r.catalog;
    }
    PB_CHECK_EQ(cur.serial, int64_t(7020));
}

PB_TEST(two_apps_claiming_one_menu_id_or_data_folder_fail_E_BLD_01_and_02) {
    World w;
    w.add("alpha-app", "1.0.0", "U_Shared", "system/alpha-app");
    w.add("beta-app", "1.0.0", "U_Shared", "system/beta-app");
    BuildResult a = build_catalog(w.options());
    PB_CHECK(!a.ok() && !a.catalog.has_value());
    PB_CHECK(has(a, "E-BLD-01", "alpha-app") && has(a, "E-BLD-01", "beta-app") && has(a, "E-BLD-01", "U_Shared"));
    World v;
    v.add("alpha-app", "1.0.0", "U_Alpha", "system/shared");
    v.add("beta-app", "1.0.0", "U_Beta", "system/shared");
    BuildResult b = build_catalog(v.options());
    PB_CHECK(!b.ok() && has(b, "E-BLD-02", "system/shared") && !has(b, "E-BLD-01"));
}

PB_TEST(without_a_live_catalog_only_an_explicit_first_publish_may_build) {
    World w;
    w.add_default("alpha-app");
    BuildOptions o = w.options();
    o.first_publish = false;
    BuildResult r = build_catalog(o);
    PB_CHECK(!r.ok() && !r.catalog.has_value());
    PB_CHECK(has(r, "E-BLD-03", "first publish"));
    PB_CHECK_EQ(w.fetched.size(), size_t(0));                         // it stopped before doing anything
    PB_CHECK(build_catalog(w.options()).ok());
}

PB_TEST(a_catalog_over_one_mebibyte_is_E_BLD_06) {
    World w;
    for (int i = 0; i < 270; ++i) {
        char id[32];
        std::snprintf(id, sizeof id, "app-%03d", i);
        w.add(id, "1.0.0", std::string("U_App") + std::to_string(i), std::string("system/") + id, [](Value& e) {
            Value o = Value::make_object();
            o.set("en", Value::from_string(std::string(2000, 'x')));
            o.set("de", Value::from_string(std::string(2000, 'y')));
            e.set("long_description", o);
        });
    }
    BuildResult r = build_catalog(w.options());
    PB_CHECK(!r.ok());
    PB_CHECK(has(r, "E-BLD-06", "1048576"));
    PB_CHECK(r.catalog_bytes.empty());
}

PB_TEST(one_failing_entry_among_several_stops_everything_and_all_failures_are_reported) {
    World w;
    w.add_default("alpha-app");
    w.add("beta-app", "1.0.0", "U_Beta", "system/beta-app", [](Value& e) { e.set("license", Value::from_string("Proprietary")); });
    w.add("gamma-app", "1.0.0", "U_Gamma", "system/gamma-app");
    w.packages.erase("https://github.com/example-org/gamma-app/releases/download/v1.0.0/gamma-app-inkpad-one-1.0.0.zip");   // the download will fail with 404
    w.add("delta-app", "1.0.0", "U_Delta", "system/delta-app", [](Value& e) { e.set("release_date", Value::from_string("2999-01-01")); });
    BuildResult r = build_catalog(w.options());
    PB_CHECK(!r.ok());
    PB_CHECK(!r.catalog.has_value());                                // no partial catalog
    PB_CHECK(r.catalog_bytes.empty());
    PB_CHECK(has(r, "E-ENT-06", "beta-app"));                        // every failing entry is named ...
    PB_CHECK(has(r, "E-ENT-12", "gamma-app"));
    PB_CHECK(has(r, "E-ENT-09", "delta-app"));
    PB_CHECK(has(r, "E-BLD-07"));                                    // ... and the build says it stopped
    PB_CHECK(!has(r, "E-BLD-07", "alpha-app"));                      // the good entry is not blamed
}

PB_TEST(an_empty_entries_directory_builds_a_valid_empty_catalog) {
    World w;
    w.dir.write_p("entries/README.md", "not an entry");
    BuildResult r = build_catalog(w.options());
    PB_CHECK(r.ok());
    PB_CHECK(r.catalog->apps.empty());
    PB_CHECK(parse_catalog(r.catalog_bytes).catalog.has_value());
}

PB_TEST(the_output_is_byte_identical_for_identical_inputs_and_clock) {
    World w;
    w.add_default("alpha-app");
    w.add_default("beta-app", "2.0.1");
    BuildResult a = build_catalog(w.options(4242)), b = build_catalog(w.options(4242));
    PB_CHECK_EQ(a.catalog_bytes, b.catalog_bytes);
    PB_CHECK(!a.catalog_bytes.empty());
}

PB_TEST(a_dry_run_runs_every_check_and_returns_no_catalog_bytes) {
    World w;
    w.add_default("alpha-app");
    BuildOptions o = w.options();
    o.dry_run = true;
    BuildResult ok = build_catalog(o);
    PB_CHECK(ok.ok());
    PB_CHECK(ok.catalog_bytes.empty());
    PB_CHECK(ok.catalog.has_value());
    World bad;
    bad.add("alpha-app", "1.0.0", "U_Shared", "system/aa");
    bad.add("beta-app", "1.0.0", "U_Shared", "system/bb");
    BuildOptions d = bad.options();
    d.dry_run = true;
    PB_CHECK(has(build_catalog(d), "E-BLD-01"));
}

PB_TEST(entries_with_a_live_catalog_get_the_owner_comparison_notice_in_the_build_too) {
    World w;
    w.add_default("alpha-app");
    Catalog live = previous_build(w, 1000);
    // the pull request moves the app to another repository
    w.add("alpha-app", "1.3.0", "U_AlphaApp", "system/alpha-app", [](Value& e) {
        const std::string url = "https://github.com/someone-else/alpha-app/releases/download/v1.3.0/a.zip";
        e.set("package_url", Value::from_string(url));
        e.set("source_url", Value::from_string("https://github.com/someone-else/alpha-app"));
    });
    w.packages["https://github.com/someone-else/alpha-app/releases/download/v1.3.0/a.zip"] = pbtest::build_package(pbtest::make_sample_app("alpha-app", "1.3.0", "U_AlphaApp", "system/alpha-app", 3));
    BuildOptions o = w.options(2000);
    o.first_publish = false;
    o.live = live;
    BuildResult r = build_catalog(o);
    PB_CHECK(r.ok());
    PB_CHECK(has(r, "W-ENT-01", "someone-else/alpha-app"));
}

PB_TEST_MAIN()
