// T053: check-entry and check-pr-paths through the command line (docs/contracts/cli.md).
#include "pbstore/catalog.h"
#include "pbstore/json.h"
#include "pbstore/sha256.h"
#include "entry_builder.h"
#include "keys.h"
#include "run_tool.h"
#include "sample_app.h"
#include "sample_catalog.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore;
using json::Value;

static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

struct Setup {
    pbtest::TmpDir t;
    std::string zip;
    Setup(Value entry = pbtest::sample_entry_doc()) {
        ::mkdir(t.file("entries").c_str(), 0755);
        t.write("entries/sample-app.json", json::write(entry));
        zip = pbtest::build_package(pbtest::make_sample_app());
        t.write("app.zip", zip);
    }
    std::string entry() const { return t.file("entries/sample-app.json"); }
};

PB_TEST(a_valid_entry_passes_with_size_and_hash_and_the_skipped_notice) {
    Setup s;
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip")});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(has(r.out, sha256_hex(s.zip).c_str()) && has(r.out, std::to_string(s.zip.size()).c_str()));
    PB_CHECK(has(r.err, "notice[W-ENT-01] sample-app"));       // no live catalog given: comparison skipped, said so
    PB_CHECK(has(r.err, "skipped"));
}

PB_TEST(errors_are_printed_with_rule_entry_and_value_and_exit_1) {
    Value d = pbtest::sample_entry_doc();
    d.set("license", Value::from_string("Proprietary"));
    Setup s(d);
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip")});
    PB_CHECK_EQ(r.code, 1);
    PB_CHECK(has(r.err, "error[E-ENT-06] sample-app: "));
    PB_CHECK(has(r.err, "license") && has(r.err, "Proprietary"));
}

PB_TEST(github_annotations_point_at_the_entry_file) {
    Value d = pbtest::sample_entry_doc();
    d.set("license", Value::from_string("Proprietary"));
    Setup s(d);
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--github"});
    PB_CHECK_EQ(r.code, 1);
    PB_CHECK(has(r.out, ("::error file=" + s.entry()).c_str()));
    PB_CHECK(has(r.out, "[E-ENT-06]"));
    // a notice is annotated as a notice
    Setup ok;
    auto n = pbtest::run_tool({"check-entry", ok.entry(), "--package", ok.t.file("app.zip"), "--github"});
    PB_CHECK_EQ(n.code, 0);
    PB_CHECK(has(n.out, "::notice file="));
    PB_CHECK(!has(n.out, "::error"));
}

PB_TEST(the_job_summary_gets_size_hash_and_a_reviewer_section) {
    Setup s;
    s.t.write("summary.md", "earlier content\n");
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip")}, {"GITHUB_STEP_SUMMARY=" + s.t.file("summary.md")});
    PB_CHECK_EQ(r.code, 0);
    const std::string md = s.t.read("summary.md");
    PB_CHECK(has(md.c_str(), "earlier content"));                // appended, not overwritten
    PB_CHECK(has(md.c_str(), "sample-app"));
    PB_CHECK(has(md.c_str(), sha256_hex(s.zip).c_str()));
    PB_CHECK(has(md.c_str(), (std::to_string(s.zip.size()) + " bytes").c_str()));
    PB_CHECK(has(md.c_str(), "Needs reviewer attention"));
    PB_CHECK(has(md.c_str(), "W-ENT-01"));
    // a failing entry lists its errors
    Value d = pbtest::sample_entry_doc();
    d.set("license", Value::from_string("Proprietary"));
    s.t.write("entries/sample-app.json", json::write(d));
    pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip")}, {"GITHUB_STEP_SUMMARY=" + s.t.file("summary.md")});
    const std::string md2 = s.t.read("summary.md");
    PB_CHECK(has(md2.c_str(), "Failed") && has(md2.c_str(), "E-ENT-06"));
}

PB_TEST(notices_never_change_the_exit_code) {
    Value d = pbtest::sample_entry_doc();
    d.set("author", Value::from_string("Jane <jane@example.org>"));
    Setup s(d);
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip")});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(has(r.err, "notice[W-ENT-02]"));
}

static void make_live_dir(const pbtest::TmpDir& t, const pbtest::KeyPair& k, const std::string& repo) {
    ::mkdir(t.file("live").c_str(), 0755);
    ::mkdir(t.file("keys").c_str(), 0755);
    Catalog c = pbtest::sample_catalog(5000);
    c.apps.clear();
    CatalogEntry e = pbtest::sample_entry("sample-app");
    e.package_url = "https://github.com/" + repo + "/releases/download/v1.2.0/a.zip";
    e.source_url = "https://github.com/" + repo;
    c.apps.push_back(e);
    const std::string bytes = write_catalog(c);
    t.write("live/catalog.json", bytes);
    t.write("live/catalog.json.sig", pbtest::sign_text(k, bytes));
    t.write("keys/current.pub", k.pub_text());
}

PB_TEST(a_live_directory_enables_the_owner_comparison) {
    auto k = pbtest::make_keypair();
    Value d = pbtest::sample_entry_doc("sample-app", "1.2.0", "someone-else/sample-app");
    Setup s(d);
    make_live_dir(s.t, k, "example-org/sample-app");
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-dir", s.t.file("live"), "--keys", s.t.file("keys")});
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(has(r.err, "notice[W-ENT-01] sample-app"));
    PB_CHECK(has(r.err, "from example-org/sample-app to someone-else/sample-app"));
    PB_CHECK(!has(r.err, "skipped"));
}

PB_TEST(a_live_catalog_that_cannot_be_verified_fails_the_check_instead_of_skipping) {
    auto k = pbtest::make_keypair(), other = pbtest::make_keypair();
    Setup s;
    make_live_dir(s.t, k, "example-org/sample-app");
    s.t.write("keys/current.pub", other.pub_text());                  // the live catalog is signed by someone else
    auto r = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-dir", s.t.file("live"), "--keys", s.t.file("keys")});
    PB_CHECK_EQ(r.code, 1);
    PB_CHECK(has(r.err, "error[E-BLD-03]"));
    // an unreachable live URL fails too, with the cause named
    auto u = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-url", "https://localhost:1/catalog.json", "--keys", s.t.file("keys")});
    PB_CHECK_EQ(u.code, 1);
    PB_CHECK(has(u.err, "error[E-BLD-03]"));
    // --first-publish is the only way to go without a live catalog when one was asked for
    auto f = pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-url", "https://localhost:1/catalog.json", "--keys", s.t.file("keys"), "--first-publish"});
    PB_CHECK_EQ(f.code, 0);
    PB_CHECK(has(f.err, "skipped"));
}

PB_TEST(usage_and_io_errors_exit_2) {
    Setup s;
    PB_CHECK_EQ(pbtest::run_tool({"check-entry"}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"check-entry", s.t.file("entries/none.json"), "--package", s.t.file("app.zip")}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("none.zip")}).code, 2);
    PB_CHECK_EQ(pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-dir", s.t.file("live")}).code, 2);   // live without --keys
    PB_CHECK_EQ(pbtest::run_tool({"check-entry", s.entry(), "--package", s.t.file("app.zip"), "--live-dir", "a", "--live-url", "https://x/y"}).code, 2);
}

PB_TEST(check_pr_paths_prints_the_notice_and_exits_0) {
    auto r = pbtest::run_tool({"check-pr-paths"}, {}, {}, "", "catalog/entries/sample-app.json\ncore/src/json.cpp\n");
    PB_CHECK_EQ(r.code, 0);
    PB_CHECK(has(r.err, "notice[W-PR-01]"));
    PB_CHECK(has(r.err, "core/src/json.cpp"));
    auto g = pbtest::run_tool({"check-pr-paths", "--github"}, {}, {}, "", "catalog/entries/sample-app.json\n.github/workflows/publish.yml\n");
    PB_CHECK_EQ(g.code, 0);
    PB_CHECK(has(g.out, "::notice"));
    auto quiet = pbtest::run_tool({"check-pr-paths"}, {}, {}, "", "catalog/entries/sample-app.json\n");
    PB_CHECK_EQ(quiet.code, 0);
    PB_CHECK(quiet.err.empty());
    pbtest::TmpDir t;
    t.write("changed.txt", "catalog/entries/a.json\nscripts/pre-merge.sh\n");
    auto f = pbtest::run_tool({"check-pr-paths", "--changed-files", t.file("changed.txt")});
    PB_CHECK_EQ(f.code, 0);
    PB_CHECK(has(f.err, "scripts/pre-merge.sh"));
    PB_CHECK_EQ(pbtest::run_tool({"check-pr-paths", "--changed-files", t.file("none.txt")}).code, 2);
}

PB_TEST_MAIN()
