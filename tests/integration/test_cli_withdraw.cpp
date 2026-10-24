// T072: the yank flow end to end with the command line (User Story 5).
#include "pbstore/catalog.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "keys.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore;
using json::Value;

static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

namespace {

struct Flow {
    pbtest::TmpDir t;
    pbtest::KeyPair key = pbtest::make_keypair();
    Flow() {
        for (const char* d : {"entries", "packages", "keys", "live", "out"}) ::mkdir(t.file(d).c_str(), 0755);
        t.write("keys/current.pub", key.pub_text());
    }
    void entry(const std::string& id, const std::string& version, const std::vector<std::string>& withdrawn, const std::string& menu, const std::string& data) {
        Value e = pbtest::sample_entry_doc(id, version);
        if (!withdrawn.empty()) { Value a = Value::make_array(); for (auto& v : withdrawn) a.push(Value::from_string(v)); e.set("withdrawn_versions", a); }
        t.write("entries/" + id + ".json", json::write(e));
        t.write("packages/" + id + "-inkpad-one-" + version + ".zip", pbtest::build_package(pbtest::make_sample_app(id, version, menu, data, 3)));
    }
    pbtest::RunResult build(const std::string& out, int64_t now, bool live) {
        std::vector<std::string> a = {"build", "--entries", t.file("entries"), "--out", out, "--package-dir", t.file("packages"), "--now", std::to_string(now)};
        if (live) { a.push_back("--live-dir"); a.push_back(t.file("live")); a.push_back("--keys"); a.push_back(t.file("keys")); } else a.push_back("--first-publish");
        return pbtest::run_tool(a);
    }
    void sign_and_promote(const std::string& out) {
        auto s = pbtest::run_tool({"sign", "--catalog", out + "/catalog.json"}, {"PBSTORE_SIGNING_KEY=" + key.seed_text()});
        if (s.code != 0) throw pbtest::Failure{"sign failed"};
        t.write("live/catalog.json", t.read(out.substr(out.rfind('/') + 1) + "/catalog.json"));
        t.write("live/catalog.json.sig", t.read(out.substr(out.rfind('/') + 1) + "/catalog.json.sig"));
    }
    pbtest::RunResult check_entry(const std::string& id, const std::string& version) {
        return pbtest::run_tool({"check-entry", t.file("entries/" + id + ".json"), "--package", t.file("packages/" + id + "-inkpad-one-" + version + ".zip"),
                                 "--live-dir", t.file("live"), "--keys", t.file("keys")});
    }
};

}  // namespace

PB_TEST(a_version_is_withdrawn_end_to_end) {
    Flow f;
    f.entry("alpha-app", "1.2.0", {}, "U_Alpha", "system/alpha-app");
    PB_CHECK_EQ(f.build(f.t.file("out"), 1000, false).code, 0);
    f.sign_and_promote(f.t.file("out"));
    // the yank pull request: the version is added to withdrawn_versions
    f.entry("alpha-app", "1.2.0", {"1.2.0"}, "U_Alpha", "system/alpha-app");
    auto c = f.check_entry("alpha-app", "1.2.0");
    PB_CHECK_EQ(c.code, 0);
    PB_CHECK(!has(c.err, "skipped"));                                   // the live catalog was used
    ::mkdir(f.t.file("out2").c_str(), 0755);
    auto b = f.build(f.t.file("out2"), 2000, true);
    PB_CHECK_EQ(b.code, 0);
    PB_CHECK(has(b.out, "1 carried over"));                             // not downloaded again
    auto s = pbtest::run_tool({"sign", "--catalog", f.t.file("out2/catalog.json")}, {"PBSTORE_SIGNING_KEY=" + f.key.seed_text()});
    PB_CHECK_EQ(s.code, 0);
    PB_CHECK_EQ(pbtest::run_tool({"verify", "--catalog", f.t.file("out2/catalog.json"), "--sig", f.t.file("out2/catalog.json.sig"), "--keys", f.t.file("keys"), "--live-serial", "1000"}).code, 0);
    auto cat = parse_catalog(f.t.read("out2/catalog.json"));
    PB_CHECK(cat.catalog.has_value());
    PB_CHECK_EQ(cat.catalog->apps.size(), size_t(1));
    PB_CHECK_EQ(cat.catalog->apps[0].withdrawn_versions, std::vector<std::string>({"1.2.0"}));
    size_t offered_count = 0;
    for (const auto& e : cat.catalog->apps) offered_count += offered(e) ? 1 : 0;
    PB_CHECK_EQ(offered_count, size_t(0));                              // nothing is offered for the app any more
}

PB_TEST(a_version_that_was_never_listed_or_a_change_to_a_withdrawn_version_fails) {
    Flow f;
    f.entry("alpha-app", "1.2.0", {}, "U_Alpha", "system/alpha-app");
    PB_CHECK_EQ(f.build(f.t.file("out"), 1000, false).code, 0);
    f.sign_and_promote(f.t.file("out"));
    f.entry("alpha-app", "1.2.0", {"0.9.0"}, "U_Alpha", "system/alpha-app");
    auto never = f.check_entry("alpha-app", "1.2.0");
    PB_CHECK_EQ(never.code, 1);
    PB_CHECK(has(never.err, "error[E-ENT-11] alpha-app") && has(never.err, "0.9.0") && has(never.err, "never listed"));
    // a link that changed to a release whose version is on the withdrawn list
    f.entry("alpha-app", "1.3.0", {"1.3.0"}, "U_Alpha", "system/alpha-app");
    auto changed = f.check_entry("alpha-app", "1.3.0");
    PB_CHECK_EQ(changed.code, 1);
    PB_CHECK(has(changed.err, "error[E-BLD-05]"));
    ::mkdir(f.t.file("out3").c_str(), 0755);
    auto b = f.build(f.t.file("out3"), 3000, true);
    PB_CHECK_EQ(b.code, 1);
    PB_CHECK(!f.t.exists("out3/catalog.json"));
}

PB_TEST(check_entry_reports_collisions_with_other_live_apps) {
    Flow f;
    f.entry("alpha-app", "1.2.0", {}, "U_Alpha", "system/alpha-app");
    PB_CHECK_EQ(f.build(f.t.file("out"), 1000, false).code, 0);
    f.sign_and_promote(f.t.file("out"));
    f.entry("beta-app", "1.0.0", {}, "U_Alpha", "system/beta-app");
    auto menu = f.check_entry("beta-app", "1.0.0");
    PB_CHECK_EQ(menu.code, 1);
    PB_CHECK(has(menu.err, "error[E-BLD-01]") && has(menu.err, "alpha-app"));
    f.entry("beta-app", "1.0.0", {}, "U_Beta", "system/alpha-app");
    auto dir = f.check_entry("beta-app", "1.0.0");
    PB_CHECK_EQ(dir.code, 1);
    PB_CHECK(has(dir.err, "error[E-BLD-02]") && has(dir.err, "system/alpha-app"));
    f.entry("beta-app", "1.0.0", {}, "U_Beta", "system/beta-app");
    PB_CHECK_EQ(f.check_entry("beta-app", "1.0.0").code, 0);
}

PB_TEST_MAIN()
