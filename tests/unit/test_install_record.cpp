// T021: the install record (contracts/install-record.md), one JSON file per installed app.
#include "pbstore/install/install_record.h"
#include "pbstore/json.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace pbstore;
using namespace pbstore::install;

namespace {
const std::string h(char c) { return std::string(64, c); }

InstallRecord sample() {
    InstallRecord r;
    r.app_id = "testapp";
    r.version = "1.2.0";
    r.source = "catalog";
    r.package_sha256 = h('3');
    r.files = {{"applications/testapp.app", 1204, h('9')}, {"applications/icons/testapp.bmp", 15094, h('0')}};
    r.directories = {"applications/icons"};
    r.menu_entry = {"U_TestApp", "Test App", "applications/testapp.app", "applications/icons/testapp.bmp",
                    "applications/icons/testapp_f.bmp", "@General"};
    r.entry_sha256 = h('7');
    r.data_dir = std::string("system/testapp");
    r.written_at = "2026-10-25T12:00:00Z";
    r.installed_by = "pbstore-install 0.2.0";
    return r;
}

json::Value as_json(const InstallRecord& r) { return *json::parse(write_install_record(r)); }
RecordParse parse_json(const json::Value& v) { return parse_install_record(json::write(v), "testapp"); }
bool refused(const json::Value& v) { return !parse_json(v).record.has_value(); }
void set(json::Value& v, const char* k, json::Value x) { v.set(k, std::move(x)); }

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

PB_TEST(a_record_is_written_and_read_back_unchanged) {
    const InstallRecord r = sample();
    const std::string text = write_install_record(r);
    RecordParse p = parse_install_record(text, "testapp");
    PB_CHECK(p.record.has_value());
    PB_CHECK(p.findings.empty());
    PB_CHECK_EQ(write_install_record(*p.record), text);      // deterministic
    PB_CHECK_EQ(p.record->files.size(), size_t(2));
    PB_CHECK_EQ(p.record->menu_entry.id, std::string("U_TestApp"));
    PB_CHECK(p.record->data_dir.has_value());
    auto v = json::parse(text);
    PB_CHECK_EQ(v->get_string("schema_version").value_or(""), std::string("1.0"));
}

PB_TEST(the_example_of_the_contract_parses) {
    // the first json block of the contract, with its hashes (the contract uses full-length values)
    const std::string md = slurp(std::string(PBSTORE_SOURCE_DIR) + "/specs/002-pb-installer-linux/contracts/install-record.md");
    const size_t a = md.find("```json");
    PB_CHECK(a != std::string::npos);
    const size_t b = md.find("```", a + 7);
    const std::string block = md.substr(a + 7, b - a - 7);
    RecordParse p = parse_install_record(block, "testapp");
    PB_CHECK(p.record.has_value());
    PB_CHECK_EQ(p.record->version, std::string("1.2.0"));
    PB_CHECK_EQ(p.record->source, std::string("catalog"));
}

PB_TEST(app_id_must_match_its_format_and_the_file_name) {
    json::Value v = as_json(sample());
    PB_CHECK(!refused(v));
    for (const char* bad : {"Test", "1abc", "a", "has_underscore", "UPPER", "-x", ""}) {
        json::Value w = v;
        set(w, "app_id", json::Value::from_string(bad));
        PB_CHECK(parse_install_record(json::write(w), bad).record.has_value() == false);
    }
    PB_CHECK(!parse_install_record(json::write(v), "otherapp").record.has_value());   // differs from the file name
}

PB_TEST(version_source_and_hashes_are_checked) {
    json::Value v = as_json(sample());
    { json::Value w = v; set(w, "version", json::Value::from_string("1.2")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "source", json::Value::from_string("web")); PB_CHECK(refused(w)); }
    for (const char* s : {"catalog", "local", "adopted"}) { json::Value w = v; set(w, "source", json::Value::from_string(s)); PB_CHECK(!refused(w)); }
    { json::Value w = v; set(w, "package_sha256", json::Value::from_string(std::string(64, 'A'))); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "package_sha256", json::Value::from_string("abc")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "entry_sha256", json::Value::from_string("zz")); PB_CHECK(refused(w)); }
}

PB_TEST(files_are_path_size_and_hash_under_applications_unique_ignoring_case) {
    json::Value v = as_json(sample());
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "files") kv.second.array[0].set("path", json::Value::from_string("system/x.bin"));
        }
        PB_CHECK(refused(w));
    }
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "files") {
                json::Value dup = kv.second.array[0];
                dup.set("path", json::Value::from_string("Applications/TestApp.app"));
                kv.second.array.push_back(dup);
            }
        }
        PB_CHECK(refused(w));
    }
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "files") kv.second.array[0].set("sha256", json::Value::from_string("nothex"));
        }
        PB_CHECK(refused(w));
    }
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "files") kv.second.array[0].set("size", json::Value::from_int(-1));
        }
        PB_CHECK(refused(w));
    }
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "files") kv.second.array[0].set("path", json::Value::from_string("applications/../x"));
        }
        PB_CHECK(refused(w));
    }
}

PB_TEST(directories_and_the_menu_entry_and_the_data_folder_are_checked) {
    json::Value v = as_json(sample());
    { json::Value w = v; json::Value d = json::Value::make_array(); d.push(json::Value::from_string("system/evil")); set(w, "directories", d); PB_CHECK(refused(w)); }
    { json::Value w = v; json::Value d = json::Value::make_array(); set(w, "directories", d); PB_CHECK(!refused(w)); }   // may be empty
    {
        json::Value w = v;
        for (auto& kv : w.object) {
            if (kv.first == "menu_entry") kv.second.object.clear();
        }
        PB_CHECK(refused(w));
    }
    { json::Value w = v; set(w, "data_dir", json::Value()); PB_CHECK(!refused(w)); }                                      // null is allowed
    { json::Value w = v; set(w, "data_dir", json::Value::from_string("system/a/b")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "data_dir", json::Value::from_string("applications/x")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "data_dir", json::Value::from_string("system/pbstore")); PB_CHECK(refused(w)); }
}

PB_TEST(written_at_and_installed_by_are_required_and_formatted) {
    json::Value v = as_json(sample());
    { json::Value w = v; set(w, "written_at", json::Value::from_string("2026-10-25 12:00:00")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "written_at", json::Value::from_string("2026-10-25T12:00:00+02:00")); PB_CHECK(refused(w)); }
    { json::Value w = v; set(w, "installed_by", json::Value::from_int(3)); PB_CHECK(refused(w)); }
    for (const char* field : {"app_id", "version", "source", "package_sha256", "files", "directories", "menu_entry", "entry_sha256", "data_dir", "written_at", "installed_by", "schema_version"}) {
        json::Value w = v;
        for (size_t i = 0; i < w.object.size(); ++i) if (w.object[i].first == field) { w.object.erase(w.object.begin() + static_cast<long>(i)); break; }
        PB_CHECK(refused(w));
    }
}

PB_TEST(unknown_fields_are_ignored_a_newer_major_touches_nothing) {
    json::Value v = as_json(sample());
    { json::Value w = v; set(w, "future_field", json::Value::from_string("x")); PB_CHECK(!refused(w)); }
    { json::Value w = v; set(w, "schema_version", json::Value::from_string("1.7")); PB_CHECK(!refused(w)); }
    json::Value w = v;
    set(w, "schema_version", json::Value::from_string("2.0"));
    RecordParse p = parse_json(w);
    PB_CHECK(!p.record.has_value());
    PB_CHECK(!p.findings.empty());
    PB_CHECK_EQ(p.findings[0].rule_id, std::string("E-INS-35"));
    set(w, "schema_version", json::Value::from_string("one"));
    PB_CHECK(refused(w));
}

PB_TEST(a_record_larger_than_256_kib_or_not_json_is_refused) {
    PB_CHECK(!parse_install_record("not json", "testapp").record.has_value());
    PB_CHECK(!parse_install_record("[]", "testapp").record.has_value());
    PB_CHECK(!parse_install_record(write_install_record(sample()) + std::string(256 * 1024, ' '), "testapp").record.has_value());
}

PB_TEST(no_field_holds_personal_data_and_the_written_text_has_only_the_documented_fields) {
    auto v = json::parse(write_install_record(sample()));
    std::vector<std::string> keys;
    for (const auto& kv : v->object) keys.push_back(kv.first);
    PB_CHECK_EQ(keys, (std::vector<std::string>{"schema_version", "app_id", "version", "source", "package_sha256", "files", "directories",
                                                "menu_entry", "entry_sha256", "data_dir", "written_at", "installed_by"}));
}

PB_TEST_MAIN()
