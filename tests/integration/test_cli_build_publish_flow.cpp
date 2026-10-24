// T064: pack -> build -> sign -> verify through the command line, offline (--package-dir, --live-dir,
// --now are for tests and rehearsals; the publish workflow never uses them).
#include "pbstore/catalog.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "keys.h"
#include "run_tool.h"
#include "sample_app.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using json::Value;

static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

namespace {

struct Flow {
    pbtest::TmpDir t;
    pbtest::KeyPair key = pbtest::make_keypair();
    Flow() {
        ::mkdir(t.file("entries").c_str(), 0755);
        ::mkdir(t.file("packages").c_str(), 0755);
        ::mkdir(t.file("keys").c_str(), 0755);
        t.write("keys/current.pub", key.pub_text());
    }
    void add(const std::string& id, const std::string& version, const std::string& menu, const std::string& data_dir) {
        Value e = pbtest::sample_entry_doc(id, version);
        t.write("entries/" + id + ".json", json::write(e));
        const std::string asset = id + "-inkpad-one-" + version + ".zip";
        t.write("packages/" + asset, pbtest::build_package(pbtest::make_sample_app(id, version, menu, data_dir, 3)));
    }
    std::vector<std::string> build_args(const std::string& out, const std::vector<std::string>& extra) const {
        std::vector<std::string> a = {"build", "--entries", t.file("entries"), "--out", out, "--package-dir", t.file("packages"), "--now", "1000"};
        a.insert(a.end(), extra.begin(), extra.end());
        return a;
    }
    pbtest::RunResult sign(const std::string& dir) const {
        return pbtest::run_tool({"sign", "--catalog", dir + "/catalog.json"}, {"PBSTORE_SIGNING_KEY=" + key.seed_text()});
    }
    pbtest::RunResult verify(const std::string& dir, const std::vector<std::string>& extra = {}) const {
        std::vector<std::string> a = {"verify", "--catalog", dir + "/catalog.json", "--sig", dir + "/catalog.json.sig", "--keys", t.file("keys")};
        a.insert(a.end(), extra.begin(), extra.end());
        return pbtest::run_tool(a);
    }
};

int64_t serial_of(const pbtest::TmpDir& t, const std::string& rel) {
    auto c = parse_catalog(t.read(rel));
    return c.catalog ? c.catalog->serial : -1;
}

}  // namespace

PB_TEST(first_build_sign_and_verify_work_end_to_end) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    f.add("beta-app", "2.0.1", "U_Beta", "system/beta-app");
    ::mkdir(f.t.file("out").c_str(), 0755);
    f.t.write("out/catalog.json.sig", "stale signature of an older catalog\n");        // must not survive a new build
    auto b = pbtest::run_tool(f.build_args(f.t.file("out"), {"--first-publish"}));
    PB_CHECK_EQ(b.code, 0);
    PB_CHECK(f.t.exists("out/catalog.json"));
    PB_CHECK(!f.t.exists("out/catalog.json.sig"));
    PB_CHECK(has(b.out, "2 app(s)"));
    PB_CHECK_EQ(f.sign(f.t.file("out")).code, 0);
    auto v = f.verify(f.t.file("out"));
    PB_CHECK_EQ(v.code, 0);
    PB_CHECK_EQ(serial_of(f.t, "out/catalog.json"), int64_t(1000));
}

PB_TEST(twenty_consecutive_builds_have_strictly_increasing_serials_even_in_the_same_second) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    ::mkdir(f.t.file("live").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool(f.build_args(f.t.file("live"), {"--first-publish"})).code, 0);
    PB_CHECK_EQ(f.sign(f.t.file("live")).code, 0);
    int64_t last = serial_of(f.t, "live/catalog.json");
    for (int i = 0; i < 20; ++i) {
        const std::string out = f.t.file("out" + std::to_string(i));
        ::mkdir(out.c_str(), 0755);
        auto b = pbtest::run_tool(f.build_args(out, {"--live-dir", f.t.file("live"), "--keys", f.t.file("keys")}));
        PB_CHECK_EQ(b.code, 0);
        PB_CHECK_EQ(f.sign(out).code, 0);
        PB_CHECK_EQ(f.verify(out, {"--live-serial", std::to_string(last)}).code, 0);       // strictly greater than the live serial
        const int64_t serial = serial_of(f.t, "out" + std::to_string(i) + "/catalog.json");
        PB_CHECK(serial > last);
        last = serial;
        f.t.write("live/catalog.json", f.t.read("out" + std::to_string(i) + "/catalog.json"));
        f.t.write("live/catalog.json.sig", f.t.read("out" + std::to_string(i) + "/catalog.json.sig"));
    }
    PB_CHECK_EQ(last, int64_t(1020));
}

PB_TEST(a_signature_by_a_key_that_is_not_trusted_never_verifies) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    ::mkdir(f.t.file("out").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool(f.build_args(f.t.file("out"), {"--first-publish"})).code, 0);
    const pbtest::KeyPair other = pbtest::make_keypair();        // a well-formed key nobody trusts
    PB_CHECK_EQ(pbtest::run_tool({"sign", "--catalog", f.t.file("out/catalog.json")}, {"PBSTORE_SIGNING_KEY=" + other.seed_text()}).code, 0);
    auto v = f.verify(f.t.file("out"));
    PB_CHECK_EQ(v.code, 1);
    PB_CHECK(has(v.err, "error[E-CAT-01]"));                    // the verify step before deployment stops this
}

PB_TEST(a_tampered_live_catalog_stops_the_build) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    ::mkdir(f.t.file("live").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool(f.build_args(f.t.file("live"), {"--first-publish"})).code, 0);
    PB_CHECK_EQ(f.sign(f.t.file("live")).code, 0);
    std::string bytes = f.t.read("live/catalog.json");
    bytes[bytes.find("alpha-app")] = 'z';
    f.t.write("live/catalog.json", bytes);
    ::mkdir(f.t.file("out").c_str(), 0755);
    auto b = pbtest::run_tool(f.build_args(f.t.file("out"), {"--live-dir", f.t.file("live"), "--keys", f.t.file("keys")}));
    PB_CHECK_EQ(b.code, 1);
    PB_CHECK(has(b.err, "error[E-BLD-03]"));
    PB_CHECK(!f.t.exists("out/catalog.json"));
    // no live option and no --first-publish: refused
    auto none = pbtest::run_tool(f.build_args(f.t.file("out"), {}));
    PB_CHECK_EQ(none.code, 2);
}

PB_TEST(verify_rejects_a_serial_that_is_not_greater_than_the_live_serial) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    ::mkdir(f.t.file("out").c_str(), 0755);
    PB_CHECK_EQ(pbtest::run_tool(f.build_args(f.t.file("out"), {"--first-publish"})).code, 0);
    PB_CHECK_EQ(f.sign(f.t.file("out")).code, 0);
    auto same = f.verify(f.t.file("out"), {"--live-serial", "1000"});
    PB_CHECK_EQ(same.code, 1);
    PB_CHECK(has(same.err, "error[E-BLD-04]"));
    PB_CHECK_EQ(f.verify(f.t.file("out"), {"--live-serial", "999"}).code, 0);
}

PB_TEST(a_dry_run_finds_conflicts_between_entries_that_each_pass_alone_and_writes_nothing) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Shared", "system/alpha-app");
    f.add("beta-app", "2.0.1", "U_Shared", "system/beta-app");
    // each entry on its own is fine
    for (const char* id : {"alpha-app", "beta-app"}) {
        const std::string ver = std::string(id) == "alpha-app" ? "1.2.0" : "2.0.1";
        auto r = pbtest::run_tool({"check-entry", f.t.file(std::string("entries/") + id + ".json"), "--package", f.t.file(std::string("packages/") + id + "-inkpad-one-" + ver + ".zip")});
        PB_CHECK_EQ(r.code, 0);
    }
    auto d = pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run", "--first-publish", "--package-dir", f.t.file("packages"), "--now", "1000"});
    PB_CHECK_EQ(d.code, 1);
    PB_CHECK(has(d.err, "error[E-BLD-01]") && has(d.err, "U_Shared"));
    PB_CHECK(!f.t.exists("catalog.json"));
}

PB_TEST(a_failing_entry_among_several_leaves_no_catalog) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    f.add("beta-app", "2.0.1", "U_Beta", "system/beta-app");
    std::string zip = f.t.read("packages/beta-app-inkpad-one-2.0.1.zip");
    zip[zip.size() / 2] = static_cast<char>(zip[zip.size() / 2] ^ 0xFF);       // corrupt the package
    f.t.write("packages/beta-app-inkpad-one-2.0.1.zip", zip);
    ::mkdir(f.t.file("out").c_str(), 0755);
    auto b = pbtest::run_tool(f.build_args(f.t.file("out"), {"--first-publish"}));
    PB_CHECK_EQ(b.code, 1);
    PB_CHECK(has(b.err, "beta-app"));
    PB_CHECK(has(b.err, "E-BLD-07"));
    PB_CHECK(!f.t.exists("out/catalog.json"));
    PB_CHECK(!f.t.exists("out/catalog.json.tmp"));
}

PB_TEST(dry_run_and_usage_rules) {
    Flow f;
    f.add("alpha-app", "1.2.0", "U_Alpha", "system/alpha-app");
    auto ok = pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run", "--first-publish", "--package-dir", f.t.file("packages")});
    PB_CHECK_EQ(ok.code, 0);
    PB_CHECK(!f.t.exists("catalog.json"));
    // --out and --dry-run together, neither, no live source, a missing entries directory, a bad clock
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run", "--out", f.t.file("o"), "--first-publish"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--first-publish"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("none"), "--dry-run", "--first-publish"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run", "--first-publish", "--now", "abc"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"build"}).code, 2);
    // a live source needs the trusted keys
    PB_CHECK_EQ(pbtest::run_tool({"build", "--entries", f.t.file("entries"), "--dry-run", "--live-dir", f.t.file("live")}).code, 2);
}

PB_TEST_MAIN()
