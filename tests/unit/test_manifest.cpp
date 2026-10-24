// T036: manifest parsing and the field rules of docs/contracts/package-manifest.md.
#include "pbstore/json.h"
#include "pbstore/manifest.h"
#include "sample_app.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore;
using json::Value;

static std::string read_example() {
    std::ifstream f(std::string(PBSTORE_SOURCE_DIR) + "/docs/contracts/examples/manifest.json", std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static Value example() { return *json::parse(read_example()); }
static Value& menu(Value& m) { return pbtest::member(m, "menu_entry"); }
static Value& files(Value& m) { return pbtest::member(m, "files"); }
static Value S(const std::string& s) { return Value::from_string(s); }

static void expect_rule(const Value& doc, const char* rule, const char* field, const char* what) {
    ManifestParseResult r = parse_manifest(json::write(doc));
    if (r.manifest) throw pbtest::Failure{std::string("accepted: ") + what};
    for (const Finding& f : r.findings)
        if (f.rule_id == rule && f.message.find(field) != std::string::npos) return;
    std::string all;
    for (const Finding& f : r.findings) all += format_finding(f) + " | ";
    throw pbtest::Failure{std::string("expected ") + rule + " naming '" + field + "' for: " + what + " [" + all + "]"};
}
static void expect_ok(const Value& doc, const char* what) {
    ManifestParseResult r = parse_manifest(json::write(doc));
    if (!r.manifest) {
        std::string all;
        for (const Finding& f : r.findings) all += format_finding(f) + " | ";
        throw pbtest::Failure{std::string("rejected: ") + what + " [" + all + "]"};
    }
}

PB_TEST(example_manifest_parses) {
    ManifestParseResult r = parse_manifest(read_example());
    PB_CHECK(r.manifest.has_value());
    PB_CHECK(r.findings.empty());
    const Manifest& m = *r.manifest;
    PB_CHECK_EQ(m.id, std::string("sample-app"));
    PB_CHECK_EQ(m.version, std::string("1.2.0"));
    PB_CHECK_EQ(m.files.size(), size_t(4));
    PB_CHECK_EQ(m.menu_entry.id, std::string("U_SampleApp"));
    PB_CHECK_EQ(m.menu_entry.group, std::string("@General"));
    PB_CHECK(m.data_dir.has_value() && *m.data_dir == "system/sample-app");
    PB_CHECK_EQ(m.min_firmware, std::string("6.8.0"));
    PB_CHECK_EQ(m.target_device, std::string("InkPad One"));
}

PB_TEST(manifest_json_problems_are_E_PKG_05) {
    for (const char* bad : {"", "{", "[]", "\"x\"", "null", "{\"a\":1,\"a\":2}"}) {
        ManifestParseResult r = parse_manifest(bad);
        PB_CHECK(!r.manifest);
        PB_CHECK(!r.findings.empty() && r.findings[0].rule_id == "E-PKG-05");
    }
    // a manifest larger than 256 KiB is refused
    ManifestParseResult r = parse_manifest(json::write(example()) + std::string(256 * 1024, ' '));
    PB_CHECK(!r.manifest && r.findings[0].rule_id == "E-PKG-05");
}

PB_TEST(schema_version_rules_are_E_PKG_06) {
    for (const char* v : {"1", "1.0.0", "v1.0", "", "x", "0.9"}) { Value m = example(); m.set("schema_version", S(v)); expect_rule(m, "E-PKG-06", "schema_version", v); }
    Value n = example(); n.set("schema_version", S("2.0"));
    ManifestParseResult r = parse_manifest(json::write(n));
    PB_CHECK(!r.manifest && r.findings[0].rule_id == "E-PKG-06");
    PB_CHECK(r.findings[0].message.find("needs updating") != std::string::npos);
    Value minor = example(); minor.set("schema_version", S("1.4")); expect_ok(minor, "newer minor");
    Value gone = example(); pbtest::remove_member(gone, "schema_version"); expect_rule(gone, "E-PKG-07", "schema_version", "missing schema_version");
}

PB_TEST(required_fields_and_types_are_E_PKG_07) {
    for (const char* key : {"id", "name", "version", "min_firmware", "target_device", "files", "menu_entry", "data_dir"}) {
        Value m = example(); pbtest::remove_member(m, key); expect_rule(m, "E-PKG-07", key, key);
    }
    for (const char* key : {"id", "name", "version", "min_firmware", "target_device"}) {
        Value m = example(); m.set(key, Value::from_int(5)); expect_rule(m, "E-PKG-07", key, "number instead of string");
    }
    { Value m = example(); m.set("files", S("x")); expect_rule(m, "E-PKG-07", "files", "files not an array"); }
    { Value m = example(); m.set("files", Value::make_array()); expect_rule(m, "E-PKG-07", "files", "empty files"); }
    { Value m = example(); m.set("menu_entry", S("x")); expect_rule(m, "E-PKG-07", "menu_entry", "menu_entry not an object"); }
    for (const char* key : {"id", "title", "path", "icon", "focused_icon", "group"}) {
        Value m = example(); pbtest::remove_member(menu(m), key); expect_rule(m, "E-PKG-07", key, key);
        Value t = example(); menu(t).set(key, Value::from_int(1)); expect_rule(t, "E-PKG-07", key, "menu field number");
    }
    for (const char* key : {"path", "size", "sha256"}) {
        Value m = example(); pbtest::remove_member(files(m).array[0], key); expect_rule(m, "E-PKG-07", key, key);
    }
    { Value m = example(); files(m).array[0] = S("applications/x"); expect_rule(m, "E-PKG-07", "files", "file entry not an object"); }
    { Value m = example(); m.set("data_dir", Value::from_int(3)); expect_rule(m, "E-PKG-07", "data_dir", "data_dir number"); }
}

PB_TEST(identity_fields_are_E_PKG_08) {
    for (const char* v : {"A", "a", "1abc", "ab_c", "Ab", "-ab", ""}) { Value m = example(); m.set("id", S(v)); expect_rule(m, "E-PKG-08", "id", v); }
    { Value m = example(); m.set("id", S("a" + std::string(40, 'b'))); expect_rule(m, "E-PKG-08", "id", "41 chars"); }
    { Value m = example(); m.set("id", S("a" + std::string(39, 'b'))); expect_ok(m, "40 chars"); }
    { Value m = example(); m.set("name", S("")); expect_rule(m, "E-PKG-08", "name", "empty name"); }
    { Value m = example(); m.set("name", S(std::string(41, 'x'))); expect_rule(m, "E-PKG-08", "name", "41 chars"); }
    { Value m = example(); m.set("name", S(std::string(40, 'x'))); expect_ok(m, "40 chars"); }
    for (const char* v : {"1.2", "v1.2.3", "01.2.3", "1.2.3+b", ""}) { Value m = example(); m.set("version", S(v)); expect_rule(m, "E-PKG-08", "version", v); }
    { Value m = example(); m.set("version", S("1.2.3-rc1")); expect_ok(m, "pre-release version"); }
    for (const char* v : {"6", "6.x", "v6.8", ""}) { Value m = example(); m.set("min_firmware", S(v)); expect_rule(m, "E-PKG-08", "min_firmware", v); }
    for (const char* v : {"Other", "inkpad one", "", "InkPad One "}) { Value m = example(); m.set("target_device", S(v)); expect_rule(m, "E-PKG-08", "target_device", v); }
}

PB_TEST(files_entries_are_E_PKG_09) {
    for (const char* v : {"system/x.bin", "applications", "applications/", "/applications/a", "applications//a", "applications/../a",
                          "applications/./a", "applications/.hidden", "applications/a\\b", "applications/caf\xc3\xa9", "applications/a:b", "Applications/a", ""}) {
        Value m = example(); files(m).array[0].set("path", S(v)); expect_rule(m, "E-PKG-09", "path", v);
    }
    { Value m = example(); files(m).array[0].set("path", S("applications/" + std::string(190, 'a'))); expect_rule(m, "E-PKG-09", "path", "too long"); }
    { Value m = example(); files(m).array[3].set("path", S("applications/ok name-1.2_x.bin")); expect_ok(m, "safe path"); }
    { Value m = example(); files(m).array.push_back(files(m).array[0]); expect_rule(m, "E-PKG-09", "path", "duplicate path"); }
    { Value m = example(); files(m).array[1].set("path", S("applications/ICONS/SAMPLE-APP.BMP")); files(m).array[0].set("path", S("applications/icons/sample-app.bmp")); expect_rule(m, "E-PKG-09", "path", "case-colliding path"); }
    for (int64_t v : {int64_t(-1)}) { Value m = example(); files(m).array[0].set("size", Value::from_int(v)); expect_rule(m, "E-PKG-09", "size", "negative size"); }
    { Value m = example(); Value f; f.type = Value::Type::Number; f.number = 1.5; files(m).array[0].set("size", f); expect_rule(m, "E-PKG-07", "size", "fractional size is the wrong type"); }
    { Value m = example(); files(m).array[0].set("size", Value::from_int(0)); expect_ok(m, "empty file"); }
    for (const char* v : {"", "NOT-A-HASH", "ABCDEF", "g000000000000000000000000000000000000000000000000000000000000001"}) {
        Value m = example(); files(m).array[0].set("sha256", S(v)); expect_rule(m, "E-PKG-09", "sha256", v); }
    { Value m = example(); files(m).array[0].set("sha256", S(std::string(64, 'A'))); expect_rule(m, "E-PKG-09", "sha256", "uppercase hash"); }
}

PB_TEST(menu_paths_must_be_listed_files_E_PKG_13) {
    for (const char* key : {"path", "icon", "focused_icon"}) {
        Value m = example(); menu(m).set(key, S("applications/nothere")); expect_rule(m, "E-PKG-13", key, key);
    }
}

PB_TEST(menu_entry_formats_are_E_PKG_14) {
    for (const char* v : {"SampleApp", "U_", "u_Sample", "U_bad-char", ""}) { Value m = example(); menu(m).set("id", S(v)); expect_rule(m, "E-PKG-14", "id", v); }
    { Value m = example(); menu(m).set("id", S("U_" + std::string(41, 'a'))); expect_rule(m, "E-PKG-14", "id", "41"); }
    { Value m = example(); menu(m).set("id", S("U_" + std::string(40, 'a'))); expect_ok(m, "40"); }
    { Value m = example(); menu(m).set("title", S("")); expect_rule(m, "E-PKG-14", "title", "empty title"); }
    { Value m = example(); menu(m).set("title", S(std::string(41, 'x'))); expect_rule(m, "E-PKG-14", "title", "41 chars"); }
    for (const char* v : {"General", "", "@"}) { Value m = example(); menu(m).set("group", S(v)); expect_rule(m, "E-PKG-14", "group", v); }
    { Value m = example(); menu(m).set("group", S("@" + std::string(40, 'g'))); expect_rule(m, "E-PKG-14", "group", "41 chars"); }
    { Value m = example(); menu(m).set("group", S("@" + std::string(39, 'g'))); expect_ok(m, "40 chars"); }
    { Value m = example(); menu(m).set("group", S("@My Group")); expect_ok(m, "group with a space"); }
}

PB_TEST(data_dir_rules_are_E_PKG_15) {
    for (const char* v : {"system", "system/", "system/A", "apps/x", "system/sample/sub", "system/a", "/system/ab", "system/ab/", "system/-ab",
                          "applications/mine", "data", ""}) {
        Value m = example(); m.set("data_dir", S(v)); expect_rule(m, "E-PKG-15", "data_dir", v); }
    for (const char* v : {"system/config", "system/profiles", "system/state", "system/fonts", "system/dict", "system/keymaps",
                          "system/themes", "system/bin", "system/lib", "system/tmp"}) {
        Value m = example(); m.set("data_dir", S(v)); expect_rule(m, "E-PKG-15", "reserved", v); }
    { Value m = example(); m.set("data_dir", S("system/" + std::string(40, 'a'))); expect_ok(m, "40 chars"); }
    { Value m = example(); m.set("data_dir", S("system/" + std::string(41, 'a'))); expect_rule(m, "E-PKG-15", "data_dir", "41 chars"); }
    { Value m = example(); m.set("data_dir", Value()); expect_ok(m, "null data_dir"); }
    { Value m = example(); m.set("data_dir", S("system/pbnotes")); expect_ok(m, "pbnotes"); }
}

PB_TEST(unknown_fields_are_ignored) {
    Value m = example();
    m.set("future_field", S("x"));
    menu(m).set("future_menu_field", Value::from_int(1));
    files(m).array[0].set("future_file_field", S("y"));
    expect_ok(m, "unknown fields");
}

PB_TEST(a_manifest_built_by_the_sample_helper_is_valid) {
    auto app = pbtest::make_sample_app("other-tool", "0.3.0", "U_OtherTool", "system/other-tool", 5);
    ManifestParseResult r = parse_manifest(json::write(app.manifest));
    PB_CHECK(r.manifest.has_value());
    PB_CHECK_EQ(r.manifest->files.size(), size_t(4));
    for (size_t i = 1; i < r.manifest->files.size(); ++i) PB_CHECK(r.manifest->files[i - 1].path < r.manifest->files[i].path);
}

PB_TEST_MAIN()
