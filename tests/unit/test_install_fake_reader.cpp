// T016: tests/support/fake_reader.h builds trees shaped like the observed InkPad One storage, with
// anonymized ids; it never contains real data.
#include "crash_fs.h"
#include "fake_reader.h"
#include "pbstore/json.h"
#include "test_support.h"
#include "tmp_dir.h"

using namespace pbtest;

namespace {
const pbstore::json::Value* get(const pbstore::json::Value& v, const char* k) { return v.find(k); }
}  // namespace

PB_TEST(the_default_tree_has_the_markers_and_a_parseable_desktop_configuration) {
    FakeReader r;
    PB_CHECK(r.dirs.count("applications") == 1);
    PB_CHECK(r.files.count("system/config/desktop/view.json") == 1);
    std::string err;
    auto v = pbstore::json::parse(r.files["system/config/desktop/view.json"], &err);
    PB_CHECK(v.has_value());
    const auto* apps = get(*v, "applications");
    PB_CHECK(apps && apps->is_object());
    int u_entries = 0, vendor_entries = 0;
    for (const auto& kv : apps->object) if (kv.first.compare(0, 1, "_") != 0) (kv.first.compare(0, 2, "U_") == 0 ? u_entries : vendor_entries)++;
    PB_CHECK(u_entries >= 2);
    PB_CHECK(vendor_entries >= 1);                       // an entry without the U_ prefix
    const auto* view = get(*v, "view");                  // as on a real reader, the groups are inside "view"
    PB_CHECK(view && view->is_object());
    const auto* groups = get(*view, "groups");
    PB_CHECK(groups && groups->is_array() && groups->array.size() >= 2);
    bool general = false;
    for (const auto& g : groups->array) if (g.get_string("title") == std::string("@General")) general = true;
    PB_CHECK(general);
    PB_CHECK(v->object.size() > 2);                      // unknown top-level keys besides applications and view
}

PB_TEST(the_minified_and_tab_variants_differ_in_layout_only) {
    FakeReader pretty(FakeReader::Pretty2), mini(FakeReader::Minified), tabs(FakeReader::Tabs), four(FakeReader::Pretty4);
    const std::string p = pretty.files["system/config/desktop/view.json"];
    const std::string m = mini.files["system/config/desktop/view.json"];
    const std::string t = tabs.files["system/config/desktop/view.json"];
    PB_CHECK(m.find('\n') == std::string::npos);
    PB_CHECK(t.find('\t') != std::string::npos);
    PB_CHECK(p.find('\t') == std::string::npos && p.find("\n  \"") != std::string::npos);
    PB_CHECK(four.files["system/config/desktop/view.json"].find("\n    \"") != std::string::npos);
    auto a = pbstore::json::parse(p), b = pbstore::json::parse(m), c = pbstore::json::parse(t);
    PB_CHECK(a && b && c);
    PB_CHECK_EQ(pbstore::json::write(*a), pbstore::json::write(*b));
    PB_CHECK_EQ(pbstore::json::write(*a), pbstore::json::write(*c));
}

PB_TEST(ids_are_anonymized_and_no_real_names_appear) {
    FakeReader r;
    r.add_data_dir("testapp", {{"a.db", "x"}});
    const std::string all = r.files["system/config/desktop/view.json"];
    for (const char* real : {"pbnotes", "PbNotes", "pbcloud", "PbCloud", "pboffice", "PbOffice", "voneinem", "/home/"})
        PB_CHECK(all.find(real) == std::string::npos);
    PB_CHECK(all.find("U_AlphaApp") != std::string::npos);
}

PB_TEST(optional_parts_can_be_added_hand_installed_apps_data_folders_and_records) {
    FakeReader r;
    pbtest::SampleApp app = pbtest::make_sample_app("testapp", "1.2.0", "U_TestApp", "system/testapp");
    r.add_hand_installed(app);
    PB_CHECK(r.files.count("applications/testapp.app") == 1);
    PB_CHECK(r.files.count("applications/icons/testapp.bmp") == 1);
    PB_CHECK(r.files["system/config/desktop/view.json"].find("U_TestApp") != std::string::npos);
    r.add_data_dir("testapp", {{"notes.db", "1234567"}, {"sub/x.txt", "x"}});
    PB_CHECK(r.files.count("system/testapp/notes.db") == 1);
    PB_CHECK(r.dirs.count("system/testapp/sub") == 1);
    r.add_record("testapp", "{\"app_id\":\"testapp\"}");
    PB_CHECK(r.files.count("system/pbstore/installed/testapp.json") == 1);
    PB_CHECK(r.dirs.count("system/pbstore/installed") == 1);
}

PB_TEST(it_can_be_written_to_a_directory_and_loaded_into_the_in_memory_volume) {
    FakeReader r;
    r.add_data_dir("testapp", {{"n.db", "abc"}});
    TmpDir dir;
    r.write_to(dir.path());
    PB_CHECK_EQ(dir.read("system/testapp/n.db"), std::string("abc"));
    PB_CHECK(dir.exists("applications"));
    PB_CHECK_EQ(dir.read("system/config/desktop/view.json"), r.files["system/config/desktop/view.json"]);
    CrashFs fs;
    r.load_into(fs);
    std::string out;
    PB_CHECK(fs.read_file("system/testapp/n.db", 10, &out));
    PB_CHECK_EQ(out, std::string("abc"));
    PB_CHECK(fs.stat("applications").kind == pbstore::install::Kind::Dir);
}

PB_TEST_MAIN()
