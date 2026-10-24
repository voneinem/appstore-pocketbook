// T026: catalog parsing and the field rules of docs/contracts/catalog-format.md.
#include "pbstore/catalog.h"
#include "pbstore/json.h"
#include "sample_catalog.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore;

static std::string read_example() {
    std::ifstream f(std::string(PBSTORE_SOURCE_DIR) + "/docs/contracts/examples/catalog.json", std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static json::Value example_doc() { return *json::parse(read_example()); }

static json::Value& app0(json::Value& doc) {
    for (auto& kv : doc.object)
        if (kv.first == "apps") return kv.second.array[0];
    throw pbtest::Failure{"example has no apps"};
}
static void set_in(json::Value& obj, const std::string& key, json::Value v) { obj.set(key, std::move(v)); }
static void remove_key(json::Value& obj, const std::string& key) {
    for (size_t i = 0; i < obj.object.size(); ++i)
        if (obj.object[i].first == key) {
            obj.object.erase(obj.object.begin() + static_cast<long>(i));
            return;
        }
}

// Expects E-CAT-02 and that the message names `field`.
static void expect_bad(const json::Value& doc, const std::string& field, const char* what) {
    CatalogParseResult r = parse_catalog(json::write(doc));
    if (r.catalog.has_value()) throw pbtest::Failure{std::string("accepted: ") + what};
    bool found = false;
    for (const Finding& f : r.findings)
        if (f.rule_id == "E-CAT-02" && f.message.find(field) != std::string::npos) found = true;
    if (!found) {
        std::string all;
        for (const Finding& f : r.findings) all += format_finding(f) + " | ";
        throw pbtest::Failure{std::string("not reported as E-CAT-02 naming '") + field + "' for: " + what + " [" + all + "]"};
    }
}
static void expect_ok(const json::Value& doc, const char* what) {
    CatalogParseResult r = parse_catalog(json::write(doc));
    if (!r.catalog.has_value()) {
        std::string all;
        for (const Finding& f : r.findings) all += format_finding(f) + " | ";
        throw pbtest::Failure{std::string("rejected: ") + what + " [" + all + "]"};
    }
}

using json::Value;
static Value S(const std::string& s) { return Value::from_string(s); }
static Value N(int64_t n) { return Value::from_int(n); }

PB_TEST(example_catalog_parses) {
    CatalogParseResult r = parse_catalog(read_example());
    PB_CHECK(r.catalog.has_value());
    PB_CHECK(r.findings.empty());
    const Catalog& c = *r.catalog;
    PB_CHECK_EQ(c.schema_version, std::string("1.0"));
    PB_CHECK_EQ(c.serial, int64_t(1792843200));
    PB_CHECK_EQ(c.apps.size(), size_t(1));
    PB_CHECK_EQ(c.apps[0].id, std::string("sample-app"));
    PB_CHECK_EQ(c.apps[0].listed_versions.size(), size_t(3));
    PB_CHECK(c.apps[0].data_dir.has_value());
    PB_CHECK(!c.apps[0].short_description.de->empty());
    PB_CHECK(!c.apps[0].long_description.de.has_value());
}

PB_TEST(writer_reproduces_the_example_byte_for_byte) {
    CatalogParseResult r = parse_catalog(read_example());
    PB_CHECK(r.catalog.has_value());
    PB_CHECK_EQ(write_catalog(*r.catalog), read_example());
}

PB_TEST(writer_is_deterministic_and_sorts_apps_by_id) {
    Catalog c = pbtest::sample_catalog();
    std::swap(c.apps[0], c.apps[1]);   // beta first
    const std::string a = write_catalog(c), b = write_catalog(c);
    PB_CHECK_EQ(a, b);
    CatalogParseResult r = parse_catalog(a);
    PB_CHECK(r.catalog.has_value());
    PB_CHECK_EQ(r.catalog->apps[0].id, std::string("alpha-app"));
    PB_CHECK_EQ(r.catalog->apps[1].id, std::string("beta-app"));
    PB_CHECK_EQ(write_catalog(*r.catalog), a);
}

PB_TEST(not_json_is_malformed) {
    for (const char* bad : {"", "{", "[]", "\"x\"", "null", "{\"a\":1,\"a\":2}"}) {
        CatalogParseResult r = parse_catalog(bad);
        PB_CHECK(!r.catalog.has_value());
        PB_CHECK(!r.findings.empty() && r.findings[0].rule_id == "E-CAT-02");
    }
}

PB_TEST(top_level_fields) {
    for (const char* v : {"1", "1.0.0", "v1.0", "", "1.x"}) { Value d = example_doc(); set_in(d, "schema_version", S(v)); expect_bad(d, "schema_version", v); }
    { Value d = example_doc(); set_in(d, "schema_version", N(1)); expect_bad(d, "schema_version", "number"); }
    for (int64_t v : {int64_t(0), int64_t(-1)}) { Value d = example_doc(); set_in(d, "serial", N(v)); expect_bad(d, "serial", "serial < 1"); }
    { Value d = example_doc(); set_in(d, "serial", S("5")); expect_bad(d, "serial", "serial string"); }
    { Value d = example_doc(); Value f; f.type = Value::Type::Number; f.number = 1.5; set_in(d, "serial", f); expect_bad(d, "serial", "serial fraction"); }
    for (const char* v : {"2026-10-24 12:00:00", "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z", "2026-10-24T24:00:00Z",
                          "2026-10-24T12:00:00", "2026-10-24T12:00:00+00:00", "2026-10-24T12:60:00Z", "x"}) {
        Value d = example_doc(); set_in(d, "generated_at", S(v)); expect_bad(d, "generated_at", v);
    }
    { Value d = example_doc(); set_in(d, "apps", S("x")); expect_bad(d, "apps", "apps not an array"); }
    for (const char* key : {"schema_version", "serial", "generated_at", "apps"}) { Value d = example_doc(); remove_key(d, key); expect_bad(d, key, key); }
}

PB_TEST(app_list_is_sorted_with_unique_ids) {
    Catalog c = pbtest::sample_catalog();
    std::swap(c.apps[0], c.apps[1]);
    // hand-written unsorted document: the writer would sort, so build the JSON from the sorted text
    Value d = *json::parse(write_catalog(pbtest::sample_catalog()));
    for (auto& kv : d.object)
        if (kv.first == "apps") std::swap(kv.second.array[0], kv.second.array[1]);
    expect_bad(d, "apps", "unsorted apps");
    Value dup = *json::parse(write_catalog(pbtest::sample_catalog()));
    for (auto& kv : dup.object)
        if (kv.first == "apps") kv.second.array[1] = kv.second.array[0];
    expect_bad(dup, "id", "duplicate id");
}

PB_TEST(app_id_name_and_descriptions) {
    for (const char* v : {"A", "a", "1abc", "ab_c", "ab c", "Ab", "-ab", ""}) { Value d = example_doc(); set_in(app0(d), "id", S(v)); expect_bad(d, "id", v); }
    { Value d = example_doc(); set_in(app0(d), "id", S("a" + std::string(40, 'b'))); expect_bad(d, "id", "41 chars"); }
    { Value d = example_doc(); set_in(app0(d), "id", S("a" + std::string(39, 'b'))); expect_ok(d, "40 chars"); }
    { Value d = example_doc(); set_in(app0(d), "id", S("ab")); expect_ok(d, "2 chars"); }
    { Value d = example_doc(); set_in(app0(d), "name", S("")); expect_bad(d, "name", "empty name"); }
    { Value d = example_doc(); set_in(app0(d), "name", S(std::string(41, 'x'))); expect_bad(d, "name", "41 chars"); }
    { Value d = example_doc(); set_in(app0(d), "name", S(std::string(40, 'x'))); expect_ok(d, "40 chars"); }
    { std::string s; for (int i = 0; i < 40; ++i) s += "\xc3\xa4"; Value d = example_doc(); set_in(app0(d), "name", S(s)); expect_ok(d, "40 code points"); }
    { std::string s; for (int i = 0; i < 41; ++i) s += "\xc3\xa4"; Value d = example_doc(); set_in(app0(d), "name", S(s)); expect_bad(d, "name", "41 code points"); }
    { Value d = example_doc(); set_in(app0(d), "name", S("bad\xff")); expect_bad(d, "name", "invalid UTF-8"); }

    auto text_obj = [](const std::string& en, const std::string* de) {
        Value o = Value::make_object(); o.set("en", S(en)); if (de) o.set("de", S(*de)); return o; };
    { Value d = example_doc(); Value o = Value::make_object(); set_in(app0(d), "short_description", o); expect_bad(d, "short_description", "missing en"); }
    { Value d = example_doc(); set_in(app0(d), "short_description", text_obj("", nullptr)); expect_bad(d, "short_description", "empty en"); }
    { Value d = example_doc(); set_in(app0(d), "short_description", text_obj(std::string(121, 'x'), nullptr)); expect_bad(d, "short_description", "en 121"); }
    { Value d = example_doc(); set_in(app0(d), "short_description", text_obj(std::string(120, 'x'), nullptr)); expect_ok(d, "en 120"); }
    { std::string de(121, 'x'); Value d = example_doc(); set_in(app0(d), "short_description", text_obj("ok", &de)); expect_bad(d, "short_description", "de 121"); }
    { std::string de(120, 'x'); Value d = example_doc(); set_in(app0(d), "short_description", text_obj("ok", &de)); expect_ok(d, "de 120"); }
    { Value d = example_doc(); set_in(app0(d), "short_description", S("text")); expect_bad(d, "short_description", "not an object"); }
    { Value d = example_doc(); set_in(app0(d), "long_description", text_obj(std::string(2001, 'x'), nullptr)); expect_bad(d, "long_description", "en 2001"); }
    { Value d = example_doc(); set_in(app0(d), "long_description", text_obj(std::string(2000, 'x'), nullptr)); expect_ok(d, "en 2000"); }
    { std::string de(2001, 'x'); Value d = example_doc(); set_in(app0(d), "long_description", text_obj("ok", &de)); expect_bad(d, "long_description", "de 2001"); }
}

PB_TEST(versions_dates_and_package_fields) {
    for (const char* v : {"1.2", "v1.2.3", "01.2.3", "1.2.3+b", ""}) { Value d = example_doc(); set_in(app0(d), "version", S(v)); expect_bad(d, "version", v); }
    for (const char* v : {"2026-02-29", "2026-13-01", "2026-1-01", "26-10-20", "2026/10/20", "2026-10-20T00:00:00Z", ""}) {
        Value d = example_doc(); set_in(app0(d), "release_date", S(v)); expect_bad(d, "release_date", v); }
    { Value d = example_doc(); set_in(app0(d), "release_date", S("2024-02-29")); expect_ok(d, "leap day"); }
    for (const char* v : {"http://github.com/o/r/releases/download/v1/a.zip", "https://example.com/a.zip",
                          "https://github.com/o/r/releases/download/v1/a.zip?x=1", "https://github.com/o/r/releases/download/v1/a.zip#f",
                          "https://github.com/o/r/releases/download/v1/a.txt", "https://github.com/o/r/archive/v1.zip",
                          "https://github.com/o/releases/download/v1/a.zip", ""}) {
        Value d = example_doc(); set_in(app0(d), "package_url", S(v)); expect_bad(d, "package_url", v); }
    for (int64_t v : {int64_t(0), int64_t(-5), int64_t(67108865)}) { Value d = example_doc(); set_in(app0(d), "package_size", N(v)); expect_bad(d, "package_size", "size out of range"); }
    { Value d = example_doc(); set_in(app0(d), "package_size", S("12")); expect_bad(d, "package_size", "string"); }
    { Value d = example_doc(); Value f; f.type = Value::Type::Number; f.number = 1.5; set_in(app0(d), "package_size", f); expect_bad(d, "package_size", "fraction"); }
    { Value d = example_doc(); set_in(app0(d), "package_size", N(67108864)); expect_ok(d, "64 MiB"); }
    { Value d = example_doc(); set_in(app0(d), "package_size", N(1)); expect_ok(d, "1 byte"); }
    for (const std::string& v : std::vector<std::string>{std::string(63, 'a'), std::string(65, 'a'), std::string(63, 'a') + "A", std::string(63, 'a') + "g", std::string()}) {
        Value d = example_doc(); set_in(app0(d), "package_sha256", S(v)); expect_bad(d, "package_sha256", "bad hash"); }
}

PB_TEST(text_url_and_device_fields) {
    { Value d = example_doc(); set_in(app0(d), "license", S("")); expect_bad(d, "license", "empty license"); }
    for (const char* f : {"source_url", "icon_url", "changelog_url"})
        for (const char* v : {"http://example.com/x", "ftp://example.com/x", "example.com", "", "https://", "https:///x"}) {
            Value d = example_doc(); set_in(app0(d), f, S(v)); expect_bad(d, f, v); }
    { Value d = example_doc(); set_in(app0(d), "author", S("")); expect_bad(d, "author", "empty author"); }
    { Value d = example_doc(); set_in(app0(d), "author", S(std::string(81, 'x'))); expect_bad(d, "author", "81 chars"); }
    { Value d = example_doc(); set_in(app0(d), "author", S(std::string(80, 'x'))); expect_ok(d, "80 chars"); }
    for (const char* v : {"6", "6.x", "v6.8", ""}) { Value d = example_doc(); set_in(app0(d), "min_firmware", S(v)); expect_bad(d, "min_firmware", v); }
    for (const char* v : {"Other", "inkpad one", "", "InkPad One "}) { Value d = example_doc(); set_in(app0(d), "target_device", S(v)); expect_bad(d, "target_device", v); }
}

PB_TEST(version_lists_and_menu_fields) {
    { Value d = example_doc(); Value a = Value::make_array(); a.push(S("1.1.0")); a.push(S("1.0.0")); a.push(S("1.2.0")); set_in(app0(d), "listed_versions", a); expect_bad(d, "listed_versions", "not ascending"); }
    { Value d = example_doc(); Value a = Value::make_array(); a.push(S("1.0.0")); a.push(S("1.0.0")); a.push(S("1.2.0")); set_in(app0(d), "listed_versions", a); expect_bad(d, "listed_versions", "duplicate"); }
    { Value d = example_doc(); Value a = Value::make_array(); a.push(S("1.0.0")); a.push(S("1.1.0")); set_in(app0(d), "listed_versions", a); expect_bad(d, "listed_versions", "missing current version"); }
    { Value d = example_doc(); Value a = Value::make_array(); a.push(S("1.2")); a.push(S("1.2.0")); set_in(app0(d), "listed_versions", a); expect_bad(d, "listed_versions", "bad semver"); }
    { Value d = example_doc(); set_in(app0(d), "listed_versions", S("1.2.0")); expect_bad(d, "listed_versions", "not an array"); }
    { Value d = example_doc(); Value a = Value::make_array(); a.push(S("1.1")); set_in(app0(d), "withdrawn_versions", a); expect_bad(d, "withdrawn_versions", "bad semver"); }
    { Value d = example_doc(); set_in(app0(d), "withdrawn_versions", Value::make_array()); expect_ok(d, "no withdrawn versions"); }
    for (const char* v : {"SampleApp", "U_", "u_Sample", "U_bad-char", ""}) { Value d = example_doc(); set_in(app0(d), "menu_entry_id", S(v)); expect_bad(d, "menu_entry_id", v); }
    { Value d = example_doc(); set_in(app0(d), "menu_entry_id", S("U_" + std::string(41, 'a'))); expect_bad(d, "menu_entry_id", "41"); }
    { Value d = example_doc(); set_in(app0(d), "menu_entry_id", S("U_" + std::string(40, 'a'))); expect_ok(d, "40"); }
    for (const char* v : {"system", "system/", "system/A", "apps/x", "system/sample/sub", "system/a", "/system/ab", "system/ab/", "system/-ab"}) {
        Value d = example_doc(); set_in(app0(d), "data_dir", S(v)); expect_bad(d, "data_dir", v); }
    { Value d = example_doc(); set_in(app0(d), "data_dir", N(3)); expect_bad(d, "data_dir", "number"); }
    { Value d = example_doc(); set_in(app0(d), "data_dir", Value()); expect_ok(d, "null data_dir"); }
}

PB_TEST(menu_ids_and_data_dirs_are_unique_across_apps) {
    Value d = *json::parse(write_catalog(pbtest::sample_catalog()));
    for (auto& kv : d.object)
        if (kv.first == "apps") kv.second.array[1].set("menu_entry_id", kv.second.array[0].find("menu_entry_id") ? *kv.second.array[0].find("menu_entry_id") : Value());
    expect_bad(d, "menu_entry_id", "duplicate menu id");
    Value e = *json::parse(write_catalog(pbtest::sample_catalog()));
    for (auto& kv : e.object)
        if (kv.first == "apps") kv.second.array[1].set("data_dir", *kv.second.array[0].find("data_dir"));
    expect_bad(e, "data_dir", "duplicate data dir");
    // several apps with data_dir null do not collide
    Catalog c = pbtest::sample_catalog();
    c.apps[0].data_dir.reset();
    c.apps[1].data_dir.reset();
    PB_CHECK(parse_catalog(write_catalog(c)).catalog.has_value());
}

PB_TEST(every_required_field_is_required) {
    for (const char* key : {"id", "name", "short_description", "long_description", "version", "release_date", "package_url",
                            "package_size", "package_sha256", "license", "source_url", "author", "min_firmware",
                            "target_device", "icon_url", "changelog_url", "withdrawn_versions", "listed_versions",
                            "menu_entry_id", "data_dir"}) {
        Value d = example_doc();
        remove_key(app0(d), key);
        expect_bad(d, key, key);
    }
}

PB_TEST(unknown_fields_are_ignored) {
    Value d = example_doc();
    d.set("future_top_level", S("x"));
    app0(d).set("future_field", N(1));
    Value o = Value::make_object(); o.set("anything", S("y"));
    app0(d).find("short_description");
    set_in(app0(d), "extra_object", o);
    expect_ok(d, "unknown fields");
}

PB_TEST(newer_major_schema_is_reported_without_judging_the_fields) {
    Value d = example_doc();
    d.set("schema_version", S("2.0"));
    d.set("apps", S("a completely different shape"));
    CatalogParseResult r = parse_catalog(json::write(d));
    PB_CHECK(!r.catalog.has_value());
    PB_CHECK_EQ(r.findings.size(), size_t(1));
    PB_CHECK_EQ(r.findings[0].rule_id, std::string("E-CAT-03"));
    PB_CHECK(r.findings[0].message.find("needs updating") != std::string::npos);
    // an older major than supported is malformed for this program
    Value old = example_doc();
    old.set("schema_version", S("0.9"));
    CatalogParseResult o = parse_catalog(json::write(old));
    PB_CHECK(!o.catalog.has_value());
    PB_CHECK_EQ(o.findings[0].rule_id, std::string("E-CAT-02"));
    // a newer minor is fine
    Value minor = example_doc();
    minor.set("schema_version", S("1.9"));
    expect_ok(minor, "1.9");
}

PB_TEST_MAIN()
