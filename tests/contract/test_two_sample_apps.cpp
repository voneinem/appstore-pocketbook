// T038 (SC-008): two unrelated apps are handled from their manifests alone.
#include "pbstore/package_check.h"
#include "sample_app.h"
#include "test_support.h"

#include <set>

using namespace pbstore;

static Manifest manifest_of(const pbtest::SampleApp& app) {
    PackageCheckResult r = check_package(pbtest::build_package(app));
    if (!r.manifest) throw pbtest::Failure{"sample app is not valid"};
    return *r.manifest;
}

PB_TEST(the_manifest_alone_gives_files_menu_entry_and_data_folder) {
    auto one = pbtest::make_sample_app("sample-app", "1.2.0", "U_SampleApp", "system/sample-app", 1);
    auto two = pbtest::make_sample_app("other-tool", "0.3.0", "U_OtherTool", "system/other-tool", 9);
    two.files.push_back(pbtest::file("applications/other-tool-data/readme.txt", "docs"));
    pbstore::json::Value m = two.manifest;
    pbtest::set_member(m, "files", pbtest::files_value(two.files));
    two.manifest = m;
    for (const auto* app : {&one, &two}) {
        Manifest m2 = manifest_of(*app);
        // the install set: every file under applications/, each with size and hash
        std::set<std::string> install;
        for (const FileEntry& f : m2.files) { PB_CHECK(f.path.rfind("applications/", 0) == 0); install.insert(f.path); }
        std::set<std::string> in_zip;
        for (const auto& f : app->files) in_zip.insert(f.name);
        PB_CHECK(install == in_zip);
        // the menu entry to register
        PB_CHECK(install.count(m2.menu_entry.path) && install.count(m2.menu_entry.icon) && install.count(m2.menu_entry.focused_icon));
        PB_CHECK(m2.menu_entry.id.rfind("U_", 0) == 0);
        // the removal set is exactly the install set: the data folder is never part of it
        PB_CHECK(m2.data_dir.has_value());
        for (const std::string& p : install) PB_CHECK(p.find(*m2.data_dir) == std::string::npos);
        PB_CHECK(m2.data_dir->rfind("system/", 0) == 0);
    }
    Manifest a = manifest_of(one), b = manifest_of(two);
    PB_CHECK(a.id != b.id && a.menu_entry.id != b.menu_entry.id && *a.data_dir != *b.data_dir);
    PB_CHECK_EQ(b.files.size(), size_t(5));
}

PB_TEST(two_apps_sharing_a_menu_id_or_data_folder_are_detectable_from_manifests_alone) {
    auto one = pbtest::make_sample_app("sample-app", "1.0.0", "U_Shared", "system/shared", 1);
    auto two = pbtest::make_sample_app("other-tool", "1.0.0", "U_Shared", "system/shared", 2);
    Manifest a = manifest_of(one), b = manifest_of(two);
    PB_CHECK_EQ(a.menu_entry.id, b.menu_entry.id);
    PB_CHECK_EQ(*a.data_dir, *b.data_dir);
}

PB_TEST_MAIN()
