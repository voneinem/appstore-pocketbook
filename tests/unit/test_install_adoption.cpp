// T093: comparing an app copied by hand with the package of the catalog's current version.
#include "install_fixtures.h"
#include "pbstore/install/adoption.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }

AdoptionCheck check(const FakeReader& r, const SampleApp& package) {
    CrashFs mem;
    r.load_into(mem);
    FatRulesFs fat(mem);
    Manifest m = manifest_of(package);
    ScanRequest req;
    req.app_id = m.id;
    req.manifest = &m;
    return compare_to_package(scan(fat, known(), req), m);
}

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

FakeReader by_hand(const SampleApp& app) {
    FakeReader r;
    r.add_hand_installed(app);
    return r;
}

}  // namespace

PB_TEST(files_that_equal_the_package_with_the_entry_pointing_at_the_app_can_be_adopted) {
    SampleApp app = nested_sample_app();
    AdoptionCheck c = check(by_hand(app), app);
    PB_CHECK(c.adoptable);
    PB_CHECK(c.differing.empty() && c.missing.empty());
    PB_CHECK(c.entry_problem.empty());
}

PB_TEST(one_file_that_differs_makes_the_app_installed_outside_the_installer) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.files["applications/testapp.bin"][5] ^= 1;
    AdoptionCheck c = check(r, app);
    PB_CHECK(!c.adoptable);
    PB_CHECK(contains(c.differing, "applications/testapp.bin"));
    PB_CHECK_EQ(c.differing.size(), size_t(1));
    PB_CHECK(has(c.text(), "applications/testapp.bin"));
    PB_CHECK(has(c.text(), "differs"));
}

PB_TEST(a_missing_file_a_size_difference_and_a_wrong_entry_path_are_each_named) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.files.erase("applications/icons/testapp.bmp");
    r.files["applications/testapp/lib/core.bin"] += "x";            // another size
    AdoptionCheck c = check(r, app);
    PB_CHECK(!c.adoptable);
    PB_CHECK(contains(c.missing, "applications/icons/testapp.bmp"));
    PB_CHECK(contains(c.differing, "applications/testapp/lib/core.bin"));
    // the entry points elsewhere
    FakeReader r2 = by_hand(app);
    ParseResult p = parse_desktop_config(r2.files["system/config/desktop/view.json"]);
    EntryValue wrong = {"/mnt/ext1/applications/elsewhere.app", "App", "applications/icons/testapp.bmp", "applications/icons/testapp_f.bmp"};
    r2.files["system/config/desktop/view.json"] = *edit(*p.config, replace_entry("U_TestApp", wrong, "@General")).text;
    AdoptionCheck c2 = check(r2, app);
    PB_CHECK(!c2.adoptable);
    PB_CHECK(!c2.entry_problem.empty());
    PB_CHECK(has(c2.text(), "menu entry"));
}

PB_TEST(extra_files_in_the_apps_own_folder_are_reported_but_not_claimed_and_do_not_block_adoption) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.put_file("applications/testapp/lib/user-notes.txt", "added by the user");
    AdoptionCheck c = check(r, app);
    PB_CHECK(c.adoptable);
    PB_CHECK(contains(c.extra, "applications/testapp/lib/user-notes.txt"));
}

PB_TEST(the_data_folder_is_not_read_beyond_its_count_and_size) {
    SampleApp app = nested_sample_app();
    FakeReader r = by_hand(app);
    r.add_data_dir("testapp", {{"notes.db", "12345"}});
    CrashFs mem;
    r.load_into(mem);
    TraceFs fs(mem);
    Manifest m = manifest_of(app);
    ScanRequest req;
    req.app_id = m.id;
    req.manifest = &m;
    AdoptionCheck c = compare_to_package(scan(fs, known(), req), m);
    PB_CHECK(c.adoptable);
    for (const auto& op : fs.ops) PB_CHECK(!(op.name == "read" && op.a.compare(0, 14, "system/testapp") == 0));
}

PB_TEST(an_app_that_is_not_on_the_reader_is_not_adoptable) {
    SampleApp app = nested_sample_app();
    AdoptionCheck c = check(FakeReader(), app);
    PB_CHECK(!c.adoptable);
    PB_CHECK(!c.missing.empty());
    PB_CHECK(!c.entry_problem.empty());
}

PB_TEST(an_older_hand_installed_version_differs_and_is_not_offered_for_adoption) {
    SampleApp old_version = nested_sample_app("testapp", "1.0.0", "U_TestApp", 2);       // other content than the catalog's version
    SampleApp current = nested_sample_app("testapp", "1.2.0", "U_TestApp", 1);
    AdoptionCheck c = check(by_hand(old_version), current);
    PB_CHECK(!c.adoptable);
    PB_CHECK(!c.differing.empty());
}

PB_TEST_MAIN()
