// T050: entry rules end to end through check_entry: one case per E-ENT-01 … 14 and 16, the notices,
// and independent verdicts for several entries (User Story 2).
#include "pbstore/entry_check.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "sample_app.h"
#include "sample_catalog.h"
#include "test_support.h"

using namespace pbstore;
using json::Value;

static const char* kToday = "2026-10-24";

struct Calls {
    int count = 0;
    std::string url;
    uint64_t max_bytes = 0;
};

static Fetcher serving(const std::string& zip, Calls* calls = nullptr) {
    return [zip, calls](const std::string& url, uint64_t max_bytes) {
        if (calls) { ++calls->count; calls->url = url; calls->max_bytes = max_bytes; }
        FetchResult r;
        r.ok = true;
        r.http_status = 200;
        r.body = zip;
        return r;
    };
}
static Fetcher failing(FetchKind kind, const std::string& error, long status = 0) {
    return [=](const std::string&, uint64_t) {
        FetchResult r;
        r.ok = false;
        r.kind = kind;
        r.http_status = status;
        r.error = error;
        return r;
    };
}

static EntryCheckResult check(const Value& entry, const std::string& zip, const std::string& file = "sample-app.json",
                              std::optional<Catalog> live = std::nullopt, Calls* calls = nullptr) {
    PackageSource src;
    src.fetcher = serving(zip, calls);
    EntryCheckOptions opt;
    opt.today = kToday;
    opt.live = std::move(live);
    return check_entry(json::write(entry), file, src, opt);
}
static bool has(const EntryCheckResult& r, const char* rule, const char* needle = "") {
    for (const Finding& f : r.findings)
        if (f.rule_id == rule && (f.message.find(needle) != std::string::npos || f.subject.find(needle) != std::string::npos)) return true;
    return false;
}
static std::string dump(const EntryCheckResult& r) {
    std::string all;
    for (const Finding& f : r.findings) all += format_finding(f) + " | ";
    return all;
}
static bool errors(const EntryCheckResult& r) {
    for (const Finding& f : r.findings) if (f.severity == Severity::Error) return true;
    return false;
}

static const std::string& good_zip() {
    static const std::string zip = pbtest::build_package(pbtest::make_sample_app());
    return zip;
}

PB_TEST(a_valid_entry_passes_and_reports_size_and_hash) {
    Calls calls;
    EntryCheckResult r = check(pbtest::sample_entry_doc(), good_zip(), "sample-app.json", std::nullopt, &calls);
    PB_CHECK(r.ok());
    PB_CHECK(!errors(r));
    PB_CHECK_EQ(r.package_size, static_cast<int64_t>(good_zip().size()));
    PB_CHECK_EQ(r.package_sha256, sha256_hex(good_zip()));
    PB_CHECK(r.manifest.has_value() && r.manifest->version == "1.2.0");
    PB_CHECK_EQ(calls.count, 1);
    PB_CHECK_EQ(calls.url, std::string("https://github.com/example-org/sample-app/releases/download/v1.2.0/sample-app-inkpad-one-1.2.0.zip"));
    PB_CHECK_EQ(calls.max_bytes, static_cast<uint64_t>(kMaxPackageBytes));
}

PB_TEST(an_invalid_entry_is_not_downloaded) {
    Calls calls;
    Value d = pbtest::sample_entry_doc();
    d.set("license", Value::from_string("Proprietary"));
    EntryCheckResult r = check(d, good_zip(), "sample-app.json", std::nullopt, &calls);
    PB_CHECK(!r.ok());
    PB_CHECK_EQ(calls.count, 0);
    PB_CHECK(has(r, "E-ENT-06", "license"));        // User Story 2 scenario 2: the message names the license field
    PB_CHECK(has(r, "E-ENT-06", "Proprietary"));
}

PB_TEST(each_entry_rule_has_a_failing_case_that_names_the_value) {
    struct Case { const char* rule; const char* needle; std::function<void(Value&)> mutate; std::string file; };
    const std::vector<Case> cases = {
        {"E-ENT-02", "short_description", [](Value& d) { pbtest::remove_member(d, "short_description"); }, "sample-app.json"},
        {"E-ENT-02", "version", [](Value& d) { d.set("version", Value::from_string("1.2.0")); }, "sample-app.json"},
        {"E-ENT-03", "schema_version", [](Value& d) { d.set("schema_version", Value::from_string("1")); }, "sample-app.json"},
        {"E-ENT-04", "Sample_App", [](Value& d) { d.set("id", Value::from_string("Sample_App")); }, "sample-app.json"},
        {"E-ENT-04", "wrong-name.json", [](Value&) {}, "wrong-name.json"},
        {"E-ENT-05", "package_url", [](Value& d) { d.set("package_url", Value::from_string("http://github.com/example-org/sample-app/releases/download/v1.2.0/a.zip")); }, "sample-app.json"},
        {"E-ENT-06", "license", [](Value& d) { d.set("license", Value::from_string("Proprietary")); }, "sample-app.json"},
        {"E-ENT-07", "icon_url", [](Value& d) { d.set("icon_url", Value::from_string("http://example.com/i.png")); }, "sample-app.json"},
        {"E-ENT-08", "long_description", [](Value& d) { Value o = Value::make_object(); o.set("en", Value::from_string("")); d.set("long_description", o); }, "sample-app.json"},
        {"E-ENT-09", "release_date", [](Value& d) { d.set("release_date", Value::from_string("2999-01-01")); }, "sample-app.json"},
        {"E-ENT-10", "author", [](Value& d) { d.set("author", Value::from_string("")); }, "sample-app.json"},
        {"E-ENT-11", "withdrawn_versions", [](Value& d) { Value a = Value::make_array(); a.push(Value::from_string("1.1")); d.set("withdrawn_versions", a); }, "sample-app.json"},
        {"E-ENT-16", "other-org/sample-app", [](Value& d) { d.set("source_url", Value::from_string("https://github.com/other-org/sample-app")); }, "sample-app.json"},
    };
    for (const Case& c : cases) {
        Value d = pbtest::sample_entry_doc();
        c.mutate(d);
        EntryCheckResult r = check(d, good_zip(), c.file);
        if (r.ok() || !has(r, c.rule, c.needle)) throw pbtest::Failure{std::string("expected ") + c.rule + " naming '" + c.needle + "': " + dump(r)};
    }
    // E-ENT-01: not JSON at all
    PackageSource src; src.fetcher = serving(good_zip()); EntryCheckOptions opt; opt.today = kToday;
    EntryCheckResult bad = check_entry("{ nope", "sample-app.json", src, opt);
    PB_CHECK(!bad.ok() && has(bad, "E-ENT-01"));
}

PB_TEST(download_failures_are_E_ENT_12_and_name_the_cause) {
    const Value d = pbtest::sample_entry_doc();
    struct F { FetchKind kind; const char* error; long status; const char* word; };
    const std::vector<F> failures = {
        {FetchKind::Unreachable, "cannot reach github.com", 0, "reach"},
        {FetchKind::HttpStatus, "the server answered with status 404", 404, "404"},
        {FetchKind::Tls, "the server's certificate is not trusted", 0, "certificate"},
        {FetchKind::Protocol, "redirect to a non-HTTPS address refused", 0, "redirect"},
        {FetchKind::TooLarge, "the download is larger than 67108864 bytes", 0, "larger"},
        {FetchKind::Timeout, "the download timed out", 0, "timed out"},
    };
    for (const F& f : failures) {
        PackageSource src;
        src.fetcher = failing(f.kind, f.error, f.status);
        EntryCheckOptions opt;
        opt.today = kToday;
        EntryCheckResult r = check_entry(json::write(d), "sample-app.json", src, opt);
        PB_CHECK(!r.ok());
        PB_CHECK(has(r, "E-ENT-12", f.word));
        PB_CHECK(has(r, "E-ENT-12", "sample-app"));    // names the entry
        PB_CHECK(!r.manifest.has_value());
    }
}

PB_TEST(a_local_package_is_used_instead_of_a_download) {
    PackageSource src;
    src.local_zip = good_zip();
    src.fetcher = [](const std::string&, uint64_t) -> FetchResult { throw pbtest::Failure{"must not download"}; };
    EntryCheckOptions opt;
    opt.today = kToday;
    EntryCheckResult r = check_entry(json::write(pbtest::sample_entry_doc()), "sample-app.json", src, opt);
    PB_CHECK(r.ok());
    // the link pattern is still checked
    Value d = pbtest::sample_entry_doc();
    d.set("package_url", Value::from_string("https://example.com/a.zip"));
    PB_CHECK(!check_entry(json::write(d), "sample-app.json", src, opt).ok());
}

PB_TEST(package_problems_are_reported_with_their_E_PKG_rule) {
    auto app = pbtest::make_sample_app();
    std::vector<pbtest::ZEntry> f = app.files;
    f[1].data[5] = static_cast<char>(f[1].data[5] ^ 1);
    EntryCheckResult r = check(pbtest::sample_entry_doc(), pbtest::build_package(app.manifest, f));
    PB_CHECK(!r.ok());
    PB_CHECK(has(r, "E-PKG-12", "applications/sample-app.bin"));
}

PB_TEST(manifest_id_and_release_tag_must_match_E_ENT_13_and_14) {
    // E-ENT-13: the package belongs to another app
    EntryCheckResult a = check(pbtest::sample_entry_doc(), pbtest::build_package(pbtest::make_sample_app("other-tool", "1.2.0", "U_Other", "system/other-tool")));
    PB_CHECK(!a.ok() && has(a, "E-ENT-13", "other-tool") && has(a, "E-ENT-13", "sample-app"));
    // E-ENT-14: the release tag differs from the manifest version
    Value d = pbtest::sample_entry_doc("sample-app", "1.3.0");
    EntryCheckResult b = check(d, good_zip());
    PB_CHECK(!b.ok() && has(b, "E-ENT-14", "1.3.0") && has(b, "E-ENT-14", "1.2.0"));
    // one leading v is optional; anything else is not
    for (const char* tag : {"1.2.0", "v1.2.0"}) {
        Value e = pbtest::sample_entry_doc();
        e.set("package_url", Value::from_string(std::string("https://github.com/example-org/sample-app/releases/download/") + tag + "/a.zip"));
        PB_CHECK(check(e, good_zip()).ok());
    }
    for (const char* tag : {"V1.2.0", "vv1.2.0", "release-1.2.0", "v1.2"}) {
        Value e = pbtest::sample_entry_doc();
        e.set("package_url", Value::from_string(std::string("https://github.com/example-org/sample-app/releases/download/") + tag + "/a.zip"));
        PB_CHECK(has(check(e, good_zip()), "E-ENT-14"));
    }
}

static Catalog live_with(const std::string& id, const std::string& repo) {
    Catalog c = pbtest::sample_catalog();
    c.apps.clear();
    CatalogEntry e = pbtest::sample_entry(id);
    e.package_url = "https://github.com/" + repo + "/releases/download/v1.2.0/a.zip";
    e.source_url = "https://github.com/" + repo;
    c.apps.push_back(e);
    return c;
}

PB_TEST(a_changed_owner_or_repository_is_a_notice_for_the_reviewer_W_ENT_01) {
    // the live entry points at example-org/sample-app; the pull request moves it to someone else
    Value d = pbtest::sample_entry_doc("sample-app", "1.2.0", "someone-else/sample-app");
    EntryCheckResult r = check(d, good_zip(), "sample-app.json", live_with("sample-app", "example-org/sample-app"));
    PB_CHECK(r.ok());                               // a notice, not an error
    bool found = false;
    for (const Finding& f : r.findings)
        if (f.rule_id == "W-ENT-01" && f.severity == Severity::Notice && f.message.find("from example-org/sample-app to someone-else/sample-app") != std::string::npos) found = true;
    PB_CHECK(found);
    // unchanged owner and repository: no notice
    EntryCheckResult same = check(pbtest::sample_entry_doc(), good_zip(), "sample-app.json", live_with("sample-app", "example-org/sample-app"));
    PB_CHECK(same.ok() && !has(same, "W-ENT-01"));
    // a new app (not in the live catalog) has nothing to compare with
    EntryCheckResult fresh = check(pbtest::sample_entry_doc(), good_zip(), "sample-app.json", live_with("another-app", "example-org/another-app"));
    PB_CHECK(fresh.ok() && !has(fresh, "W-ENT-01"));
}

PB_TEST(without_a_live_catalog_the_comparison_is_reported_as_skipped) {
    EntryCheckResult r = check(pbtest::sample_entry_doc(), good_zip());
    PB_CHECK(r.ok());
    PB_CHECK(has(r, "W-ENT-01", "skipped"));
}

PB_TEST(several_entries_each_get_their_own_verdict) {
    Value good = pbtest::sample_entry_doc();
    Value bad = pbtest::sample_entry_doc();
    bad.set("license", Value::from_string("Proprietary"));
    EntryCheckResult a = check(bad, good_zip());
    EntryCheckResult b = check(good, good_zip());
    EntryCheckResult c = check(bad, good_zip());
    PB_CHECK(!a.ok() && b.ok() && !c.ok());          // one bad entry does not influence the others
}

PB_TEST_MAIN()
