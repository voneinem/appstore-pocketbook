// T049: entry file parsing and the field rules of docs/contracts/entry-file.md.
#include "pbstore/entry.h"
#include "pbstore/json.h"
#include "entry_builder.h"
#include "sample_app.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore;
using json::Value;

static const char* kToday = "2026-10-24";

static std::string read_example() {
    std::ifstream f(std::string(PBSTORE_SOURCE_DIR) + "/docs/contracts/examples/entry.json", std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static Value example() { return *json::parse(read_example()); }
static Value S(const std::string& s) { return Value::from_string(s); }

static EntryParseResult parse(const Value& v, const std::string& file = "sample-app.json") { return parse_entry(json::write(v), file, kToday); }
static bool has_rule(const EntryParseResult& r, const char* id, const char* needle = "") {
    for (const Finding& f : r.findings)
        if (f.rule_id == id && (f.message.find(needle) != std::string::npos || f.subject.find(needle) != std::string::npos)) return true;
    return false;
}
static void expect_rule(const Value& v, const char* rule, const char* needle, const char* what) {
    EntryParseResult r = parse(v);
    if (r.entry) throw pbtest::Failure{std::string("accepted: ") + what};
    if (!has_rule(r, rule, needle)) {
        std::string all;
        for (const Finding& f : r.findings) all += format_finding(f) + " | ";
        throw pbtest::Failure{std::string("expected ") + rule + " naming '" + needle + "' for: " + what + " [" + all + "]"};
    }
}
static void expect_ok(const Value& v, const char* what) {
    EntryParseResult r = parse(v);
    if (!r.entry) {
        std::string all;
        for (const Finding& f : r.findings) all += format_finding(f) + " | ";
        throw pbtest::Failure{std::string("rejected: ") + what + " [" + all + "]"};
    }
}

PB_TEST(the_example_entry_parses) {
    EntryParseResult r = parse_entry(read_example(), "sample-app.json", kToday);
    PB_CHECK(r.entry.has_value());
    PB_CHECK(r.findings.empty());
    PB_CHECK_EQ(r.entry->id, std::string("sample-app"));
    PB_CHECK_EQ(r.entry->license, std::string("MIT"));
    PB_CHECK_EQ(r.entry->withdrawn_versions.size(), size_t(1));
    PB_CHECK(r.entry->short_description.de.has_value());
    PB_CHECK_EQ(r.entry->release_date, std::string("2026-10-20"));
}

PB_TEST(file_problems_are_E_ENT_01) {
    for (const char* bad : {"", "{", "[]", "\"x\"", "null", "{\"a\":1,\"a\":2}"}) {
        EntryParseResult r = parse_entry(bad, "sample-app.json", kToday);
        PB_CHECK(!r.entry && !r.findings.empty() && r.findings[0].rule_id == "E-ENT-01");
    }
    EntryParseResult big = parse_entry(json::write(example()) + std::string(16 * 1024, ' '), "sample-app.json", kToday);
    PB_CHECK(!big.entry && big.findings[0].rule_id == "E-ENT-01");
    // a file just under the limit is fine
    std::string ok = json::write(example());
    ok += std::string(16 * 1024 - ok.size(), ' ');
    PB_CHECK(parse_entry(ok, "sample-app.json", kToday).entry.has_value());
}

PB_TEST(required_fields_and_forbidden_fields_are_E_ENT_02) {
    for (const char* key : {"schema_version", "id", "package_url", "release_date", "short_description", "long_description", "source_url",
                            "author", "icon_url", "changelog_url"}) {
        Value d = example(); pbtest::remove_member(d, key); expect_rule(d, "E-ENT-02", key, key); }
    for (const char* key : {"id", "package_url", "release_date", "source_url", "author", "icon_url", "changelog_url", "license"}) {
        Value d = example(); d.set(key, Value::from_int(5)); expect_rule(d, key == std::string("license") ? "E-ENT-06" : "E-ENT-02", key, "wrong type"); }
    { Value d = example(); d.set("short_description", S("text")); expect_rule(d, "E-ENT-02", "short_description", "description not an object"); }
    { Value d = example(); d.set("withdrawn_versions", S("1.0.0")); expect_rule(d, "E-ENT-02", "withdrawn_versions", "withdrawn_versions not an array"); }
    // these come only from the package manifest
    for (const char* key : {"version", "min_firmware", "target_device", "name"}) {
        Value d = example(); d.set(key, S("x")); expect_rule(d, "E-ENT-02", key, key); }
}

PB_TEST(schema_version_is_E_ENT_03) {
    for (const char* v : {"1", "1.0.0", "v1.0", "", "0.9"}) { Value d = example(); d.set("schema_version", S(v)); expect_rule(d, "E-ENT-03", "schema_version", v); }
    Value n = example(); n.set("schema_version", S("2.0")); n.set("id", Value::from_int(1));   // content of a newer schema is not judged
    EntryParseResult r = parse(n);
    PB_CHECK(!r.entry && r.findings.size() == 1 && r.findings[0].rule_id == "E-ENT-03");
    PB_CHECK(r.findings[0].message.find("needs updating") != std::string::npos);
    Value m = example(); m.set("schema_version", S("1.3")); expect_ok(m, "newer minor");
}

PB_TEST(id_and_file_name_are_E_ENT_04) {
    for (const char* v : {"A", "a", "1abc", "ab_c", "Ab", ""}) { Value d = example(); d.set("id", S(v)); expect_rule(d, "E-ENT-04", "id", v); }
    { Value d = example(); d.set("id", S("a" + std::string(40, 'b'))); expect_rule(d, "E-ENT-04", "id", "41 chars"); }
    { Value d = example(); EntryParseResult r = parse(d, "other-name.json"); PB_CHECK(!r.entry && has_rule(r, "E-ENT-04", "other-name.json")); }
    { Value d = example(); EntryParseResult r = parse(d, "sample-app.JSON"); PB_CHECK(!r.entry && has_rule(r, "E-ENT-04")); }
    { Value d = example(); EntryParseResult r = parse(d, "sample-app"); PB_CHECK(!r.entry && has_rule(r, "E-ENT-04")); }
}

PB_TEST(package_link_is_E_ENT_05) {
    for (const char* v : {"http://github.com/o/r/releases/download/v1/a.zip", "https://example.com/a.zip",
                          "https://github.com/o/r/releases/download/v1/a.zip?x=1", "https://github.com/o/r/releases/download/v1/a.zip#f",
                          "https://github.com/o/r/releases/download/v1/a.txt", "https://github.com/o/r/archive/v1.zip",
                          "https://github.com/o/releases/download/v1/a.zip", "https://raw.githubusercontent.com/o/r/v1/a.zip", ""}) {
        Value d = example(); d.set("package_url", S(v)); expect_rule(d, "E-ENT-05", "package_url", v); }
}

PB_TEST(license_is_E_ENT_06) {
    for (const char* v : {"Proprietary", "", "GPL-3.0", "MIT License", "mit", "CC0-1.0", "Commercial", "NONE", "BUSL-1.1"}) {
        Value d = example(); d.set("license", S(v)); expect_rule(d, "E-ENT-06", "license", v); }
    for (const char* v : {"MIT", "Apache-2.0", "BSD-3-Clause", "GPL-3.0-only", "GPL-3.0-or-later", "LGPL-2.1-only", "AGPL-3.0-only", "MPL-2.0", "ISC", "Zlib", "Unlicense"}) {
        Value d = example(); d.set("license", S(v)); expect_ok(d, v); }
    { Value d = example(); pbtest::remove_member(d, "license"); expect_rule(d, "E-ENT-06", "license", "missing license"); }
}

PB_TEST(urls_are_E_ENT_07) {
    for (const char* f : {"source_url", "icon_url", "changelog_url"})
        for (const char* v : {"http://example.com/x", "ftp://example.com/x", "example.com", "", "https://", "https:///x", "https://a b.com"}) {
            Value d = example(); d.set(f, S(v)); expect_rule(d, "E-ENT-07", f, v); }
    { Value d = example(); d.set("source_url", S("https://codeberg.org/example/app")); expect_ok(d, "non-GitHub source"); }
}

PB_TEST(descriptions_are_E_ENT_08) {
    auto text = [](const std::string& en, const std::string* de) { Value o = Value::make_object(); o.set("en", S(en)); if (de) o.set("de", S(*de)); return o; };
    { Value d = example(); d.set("short_description", Value::make_object()); expect_rule(d, "E-ENT-08", "short_description", "no en"); }
    { Value d = example(); d.set("short_description", text("", nullptr)); expect_rule(d, "E-ENT-08", "short_description", "empty en"); }
    { Value d = example(); d.set("short_description", text(std::string(121, 'x'), nullptr)); expect_rule(d, "E-ENT-08", "short_description", "121"); }
    { Value d = example(); d.set("short_description", text(std::string(120, 'x'), nullptr)); expect_ok(d, "120"); }
    { std::string de(121, 'x'); Value d = example(); d.set("short_description", text("ok", &de)); expect_rule(d, "E-ENT-08", "short_description", "de 121"); }
    { std::string de(120, 'x'); Value d = example(); d.set("short_description", text("ok", &de)); expect_ok(d, "de 120"); }
    { Value d = example(); d.set("long_description", text(std::string(2001, 'x'), nullptr)); expect_rule(d, "E-ENT-08", "long_description", "2001"); }
    { Value d = example(); d.set("long_description", text(std::string(2000, 'x'), nullptr)); expect_ok(d, "2000"); }
    { Value d = example(); d.set("long_description", text("bad\xff", nullptr)); expect_rule(d, "E-ENT-08", "long_description", "invalid UTF-8"); }
}

PB_TEST(release_date_is_E_ENT_09) {
    for (const char* v : {"2026-02-29", "2026-13-01", "2026-1-01", "26-10-20", "2026/10/20", "2026-10-20T00:00:00Z", "", "2026-10-26", "2999-01-01"}) {
        Value d = example(); d.set("release_date", S(v)); expect_rule(d, "E-ENT-09", "release_date", v); }
    for (const char* v : {"2026-10-24", "2026-10-25", "2020-01-01", "2024-02-29"}) { Value d = example(); d.set("release_date", S(v)); expect_ok(d, v); }
    // "today" at a month and year boundary: tomorrow is allowed
    EntryParseResult r = parse_entry([] { Value d = example(); d.set("release_date", S("2027-01-01")); return json::write(d); }(), "sample-app.json", "2026-12-31");
    PB_CHECK(r.entry.has_value());
    EntryParseResult r2 = parse_entry([] { Value d = example(); d.set("release_date", S("2027-01-02")); return json::write(d); }(), "sample-app.json", "2026-12-31");
    PB_CHECK(!r2.entry);
}

PB_TEST(author_is_E_ENT_10) {
    { Value d = example(); d.set("author", S("")); expect_rule(d, "E-ENT-10", "author", "empty"); }
    { Value d = example(); d.set("author", S(std::string(81, 'x'))); expect_rule(d, "E-ENT-10", "author", "81"); }
    { Value d = example(); d.set("author", S(std::string(80, 'x'))); expect_ok(d, "80"); }
}

PB_TEST(withdrawn_versions_format_is_E_ENT_11) {
    { Value d = example(); Value a = Value::make_array(); a.push(S("1.1")); d.set("withdrawn_versions", a); expect_rule(d, "E-ENT-11", "withdrawn_versions", "bad semver"); }
    { Value d = example(); Value a = Value::make_array(); a.push(Value::from_int(1)); d.set("withdrawn_versions", a); expect_rule(d, "E-ENT-11", "withdrawn_versions", "number"); }
    { Value d = example(); Value a = Value::make_array(); a.push(S("1.1.0")); a.push(S("1.1.0")); d.set("withdrawn_versions", a); expect_rule(d, "E-ENT-11", "withdrawn_versions", "duplicate"); }
    { Value d = example(); pbtest::remove_member(d, "withdrawn_versions"); EntryParseResult r = parse(d); PB_CHECK(r.entry && r.entry->withdrawn_versions.empty()); }
    { Value d = example(); d.set("withdrawn_versions", Value::make_array()); expect_ok(d, "empty list"); }
}

PB_TEST(link_and_source_must_name_the_same_github_repository_E_ENT_16) {
    { Value d = example(); d.set("source_url", S("https://github.com/someone-else/sample-app"));
      EntryParseResult r = parse(d); PB_CHECK(!r.entry);
      PB_CHECK(has_rule(r, "E-ENT-16", "example-org/sample-app") && has_rule(r, "E-ENT-16", "someone-else/sample-app")); }   // both repositories named
    { Value d = example(); d.set("source_url", S("https://github.com/example-org/another-repo")); expect_rule(d, "E-ENT-16", "another-repo", "other repository"); }
    // comparison ignores letter case, a trailing slash and .git
    for (const char* v : {"https://github.com/Example-Org/Sample-App", "https://github.com/example-org/sample-app/", "https://github.com/example-org/sample-app.git"}) {
        Value d = example(); d.set("source_url", S(v)); expect_ok(d, v); }
    // not on GitHub: the rule does not apply
    { Value d = example(); d.set("source_url", S("https://codeberg.org/example/app")); expect_ok(d, "non-GitHub source"); }
    // a GitHub URL that is not a repository (a subpage) is not "the repository": the rule applies to repository URLs only
    { Value d = example(); d.set("source_url", S("https://github.com/example-org/sample-app/tree/main")); expect_ok(d, "GitHub subpage"); }
}

PB_TEST(email_like_text_is_a_notice_and_not_an_error_W_ENT_02) {
    for (const char* text : {"Jane Doe <jane@example.org>", "write to jane.doe+apps@mail.example.com", "a@b.co"}) {
        Value d = example(); d.set("author", S(text));
        EntryParseResult r = parse(d);
        PB_CHECK(r.entry.has_value());                 // not an error
        bool notice = false;
        for (const Finding& f : r.findings) if (f.rule_id == "W-ENT-02" && f.severity == Severity::Notice) notice = true;
        PB_CHECK(notice);
    }
    { Value d = example(); Value o = Value::make_object(); o.set("en", S("Contact me at someone@example.org for help")); d.set("long_description", o);
      EntryParseResult r = parse(d); bool n = false; for (const Finding& f : r.findings) if (f.rule_id == "W-ENT-02") n = true; PB_CHECK(r.entry && n); }
    for (const char* text : {"Example Author", "Version 2 @ launch", "mail: @home", "a@b", "user@localhost", "@handle"}) {
        Value d = example(); d.set("author", S(text));
        EntryParseResult r = parse(d);
        PB_CHECK(r.entry.has_value());
        for (const Finding& f : r.findings) PB_CHECK(f.rule_id != "W-ENT-02");
    }
}

PB_TEST(unknown_fields_are_otherwise_ignored) {
    Value d = example();
    d.set("future_field", S("x"));
    Value o = Value::make_object(); o.set("anything", S("y"));
    d.set("extra_object", o);
    expect_ok(d, "unknown fields");
}

PB_TEST(entries_built_by_the_test_helper_are_valid) {
    EntryParseResult r = parse_entry(json::write(pbtest::sample_entry_doc("other-tool", "0.3.0")), "other-tool.json", kToday);
    PB_CHECK(r.entry.has_value());
    PB_CHECK_EQ(r.entry->package_url, std::string("https://github.com/example-org/other-tool/releases/download/v0.3.0/other-tool-inkpad-one-0.3.0.zip"));
}

PB_TEST_MAIN()
