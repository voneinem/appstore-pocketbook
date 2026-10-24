// T071: withdrawing (yanking) a version (User Story 5, SC-007, FR-036, FR-037, FR-045).
#include "pbstore/catalog_build.h"
#include "pbstore/entry_check.h"
#include "pbstore/json.h"
#include "entry_builder.h"
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
    std::map<std::string, std::string> packages;
    void add(const std::string& id, const std::string& version, const std::vector<std::string>& withdrawn = {}, const std::string& menu = "",
             const std::string& data_dir = "") {
        Value e = pbtest::sample_entry_doc(id, version);
        if (!withdrawn.empty()) { Value a = Value::make_array(); for (auto& v : withdrawn) a.push(Value::from_string(v)); e.set("withdrawn_versions", a); }
        dir.write_p("entries/" + id + ".json", json::write(e));
        std::string m = menu.empty() ? "U_" + std::string(1, char(std::toupper(id[0]))) + id.substr(1, id.find('-') - 1) : menu;
        packages[*e.get_string("package_url")] = pbtest::build_package(pbtest::make_sample_app(id, version, m, data_dir.empty() ? "system/" + id : data_dir, 3));
    }
    Fetcher fetcher() { return [this](const std::string& url, uint64_t) { FetchResult r; auto it = packages.find(url); if (it == packages.end()) { r.kind = FetchKind::HttpStatus; r.error = "status 404"; return r; } r.ok = true; r.body = it->second; return r; }; }
    BuildOptions options(int64_t now, const std::optional<Catalog>& live) {
        BuildOptions o; o.entries_dir = dir.file("entries"); o.now = now; o.today = "2026-10-24"; o.fetcher = fetcher();
        o.first_publish = !live; o.live = live;
        return o;
    }
};
bool has(const std::vector<Finding>& f, const char* rule, const std::string& needle = "") {
    for (const Finding& x : f) if (x.rule_id == rule && (x.message.find(needle) != std::string::npos || x.subject.find(needle) != std::string::npos)) return true;
    return false;
}

}  // namespace

PB_TEST(withdrawing_the_listed_version_stops_the_app_from_being_offered_scenario_1) {
    World w;
    w.add("alpha-app", "1.2.0");
    BuildResult first = build_catalog(w.options(1000, std::nullopt));
    PB_CHECK(first.ok() && offered(first.catalog->apps[0]));
    w.add("alpha-app", "1.2.0", {"1.2.0"});                       // the yank: same release link, version added to the list
    BuildResult second = build_catalog(w.options(2000, first.catalog));
    PB_CHECK(second.ok());
    PB_CHECK_EQ(second.downloaded, size_t(0));
    PB_CHECK(!offered(second.catalog->apps[0]));                   // nothing is offered for the app
    PB_CHECK_EQ(second.catalog->apps[0].withdrawn_versions, std::vector<std::string>({"1.2.0"}));
    PB_CHECK_EQ(second.catalog->apps.size(), size_t(1));           // the app is still in the catalog: installed copies can be recognized
}

PB_TEST(a_later_entry_pointing_at_a_fixed_or_earlier_version_is_offered_again_scenario_2) {
    World w;
    w.add("alpha-app", "1.0.0");
    BuildResult a = build_catalog(w.options(1000, std::nullopt));
    w.add("alpha-app", "1.2.0");
    BuildResult b = build_catalog(w.options(2000, a.catalog));
    PB_CHECK(b.ok());
    PB_CHECK_EQ(b.catalog->apps[0].listed_versions, std::vector<std::string>({"1.0.0", "1.2.0"}));
    w.add("alpha-app", "1.2.0", {"1.2.0"});
    BuildResult c = build_catalog(w.options(3000, b.catalog));    // 1.2.0 withdrawn
    PB_CHECK(c.ok() && !offered(c.catalog->apps[0]));
    // back to the earlier release: offered again, and 1.2.0 stays withdrawn
    w.add("alpha-app", "1.0.0", {"1.2.0"});
    BuildResult d = build_catalog(w.options(4000, c.catalog));
    PB_CHECK(d.ok());
    PB_CHECK_EQ(d.catalog->apps[0].version, std::string("1.0.0"));
    PB_CHECK(offered(d.catalog->apps[0]));
    // or forward to a fixed release
    w.add("alpha-app", "1.2.1", {"1.2.0"});
    BuildResult e = build_catalog(w.options(5000, c.catalog));
    PB_CHECK(e.ok() && offered(e.catalog->apps[0]));
    PB_CHECK_EQ(e.catalog->apps[0].listed_versions, std::vector<std::string>({"1.0.0", "1.2.0", "1.2.1"}));
}

PB_TEST(an_installed_version_can_be_recognized_as_withdrawn_scenario_3) {
    CatalogEntry e = pbtest::sample_entry("alpha-app", "1.2.1");
    e.withdrawn_versions = {"1.1.0", "1.2.0"};
    e.listed_versions = {"1.1.0", "1.2.0", "1.2.1"};
    PB_CHECK(is_withdrawn(e, "1.1.0"));
    PB_CHECK(is_withdrawn(e, "1.2.0"));
    PB_CHECK(!is_withdrawn(e, "1.2.1"));
    PB_CHECK(!is_withdrawn(e, "0.9.0"));
    PB_CHECK(offered(e));        // the current version is fine; the installed one is flagged by is_withdrawn
}

static EntryCheckResult check(World& w, const std::string& id, const std::optional<Catalog>& live) {
    std::string bytes;
    PackageSource src;
    src.fetcher = w.fetcher();
    EntryCheckOptions opt;
    opt.today = "2026-10-24";
    opt.live = live;
    std::string entry_text;
    {
        std::string path = w.dir.file("entries/" + id + ".json");
        entry_text = w.dir.read("entries/" + id + ".json");
    }
    return check_entry(entry_text, id + ".json", src, opt);
}

PB_TEST(a_withdrawn_version_must_have_been_listed_E_ENT_11_scenario_4) {
    World w;
    w.add("alpha-app", "1.2.0");
    BuildResult first = build_catalog(w.options(1000, std::nullopt));
    // 1.1.0 and 0.9.0 were never listed
    w.add("alpha-app", "1.2.0", {"0.9.0"});
    EntryCheckResult r = check(w, "alpha-app", first.catalog);
    PB_CHECK(!r.ok());
    PB_CHECK(has(r.findings, "E-ENT-11", "0.9.0") && has(r.findings, "E-ENT-11", "never listed"));
    // the build refuses it too, naming the entry
    BuildResult b = build_catalog(w.options(2000, first.catalog));
    PB_CHECK(!b.ok() && has(b.findings, "E-ENT-11", "0.9.0") && has(b.findings, "E-BLD-07"));
    // the current version and an earlier listed one are fine
    w.add("alpha-app", "1.2.0", {"1.2.0"});
    PB_CHECK(check(w, "alpha-app", first.catalog).ok());
    // without a live catalog the existence cannot be checked: said in the skipped notice, not an error
    w.add("alpha-app", "1.2.0", {"0.9.0"});
    EntryCheckResult nolive = check(w, "alpha-app", std::nullopt);
    PB_CHECK(nolive.ok());
    PB_CHECK(has(nolive.findings, "W-ENT-01", "withdrawn"));
}

PB_TEST(an_entry_changed_to_a_withdrawn_version_fails_E_BLD_05) {
    World w;
    w.add("alpha-app", "1.0.0");
    BuildResult a = build_catalog(w.options(1000, std::nullopt));
    w.add("alpha-app", "1.2.0");
    BuildResult b = build_catalog(w.options(2000, a.catalog));
    PB_CHECK(b.ok());
    w.add("alpha-app", "1.2.0", {"1.2.0"});
    BuildResult c = build_catalog(w.options(3000, b.catalog));       // 1.2.0 yanked, same link: fine
    PB_CHECK(c.ok());
    // somebody changes the link back to the withdrawn release
    w.add("alpha-app", "1.0.0", {"1.0.0", "1.2.0"});                  // points at 1.0.0 but 1.0.0 is withdrawn too
    BuildResult d = build_catalog(w.options(4000, c.catalog));
    PB_CHECK(!d.ok() && has(d.findings, "E-BLD-05", "1.0.0"));
    EntryCheckResult r = check(w, "alpha-app", c.catalog);
    PB_CHECK(!r.ok() && has(r.findings, "E-BLD-05", "1.0.0"));
    // withdrawing the version a changed link points to is refused; withdrawing another one is fine
    w.add("alpha-app", "1.0.0", {"1.2.0"});
    PB_CHECK(build_catalog(w.options(5000, c.catalog)).ok());
}

PB_TEST(a_menu_id_or_data_folder_that_collides_with_a_live_app_is_reported_E_BLD_01_and_02) {
    World w;
    w.add("alpha-app", "1.0.0", {}, "U_Alpha", "system/alpha-app");
    BuildResult live = build_catalog(w.options(1000, std::nullopt));
    w.add("beta-app", "1.0.0", {}, "U_Alpha", "system/beta-app");
    EntryCheckResult a = check(w, "beta-app", live.catalog);
    PB_CHECK(!a.ok() && has(a.findings, "E-BLD-01", "alpha-app") && has(a.findings, "E-BLD-01", "U_Alpha"));
    w.add("beta-app", "1.0.0", {}, "U_Beta", "system/alpha-app");
    EntryCheckResult b = check(w, "beta-app", live.catalog);
    PB_CHECK(!b.ok() && has(b.findings, "E-BLD-02", "system/alpha-app"));
    w.add("beta-app", "1.0.0", {}, "U_Beta", "system/beta-app");
    PB_CHECK(check(w, "beta-app", live.catalog).ok());
    // the app's own entry in the live catalog is not a collision with itself
    PB_CHECK(check(w, "alpha-app", live.catalog).ok());
}

PB_TEST(the_first_publish_checks_withdrawn_versions_against_an_empty_catalog) {
    // nothing was listed before the first publish: only the version of the package itself can be withdrawn
    World w;
    w.add("alpha-app", "1.2.0", {"0.9.0"});
    BuildResult never = build_catalog(w.options(1000, std::nullopt));
    PB_CHECK(!never.ok() && has(never.findings, "E-ENT-11", "0.9.0") && has(never.findings, "E-BLD-07"));
    // a brand-new entry that points at a version it also withdraws is refused (E-BLD-05)
    World v;
    v.add("alpha-app", "1.2.0", {"1.2.0"});
    BuildResult own = build_catalog(v.options(1000, std::nullopt));
    PB_CHECK(!own.ok() && has(own.findings, "E-BLD-05", "1.2.0"));
    // withdrawing nothing is fine, and the serial is still the clock on a first publish
    World x;
    x.add("alpha-app", "1.2.0");
    BuildResult fine = build_catalog(x.options(4321, std::nullopt));
    PB_CHECK(fine.ok());
    PB_CHECK_EQ(fine.catalog->serial, int64_t(4321));
}

PB_TEST_MAIN()
