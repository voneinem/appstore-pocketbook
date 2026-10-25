// T064 (spec SC-011): hostile packages are refused 100%, with the matching rule id, and the reader stays
// byte-identical. The whole path is exercised: catalog, download, checks, scan, plan.
#include "install_rig.h"
#include "package_fixtures.h"
#include "test_support.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const std::string& id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}

// Offers `zip` as the package of "testapp" and tries to plan an installation; returns the findings.
struct Attempt {
    std::vector<Finding> findings;
    bool planned = false;
    Diff diff;
};

Attempt try_install(EngineRig& rig, const std::string& zip, const std::function<void(CatalogEntry&)>& tweak_entry = nullptr) {
    SampleApp app = nested_sample_app();
    CatalogEntry& e = rig.host.add_app(app);
    e.package_size = static_cast<int64_t>(zip.size());
    e.package_sha256 = sha256_hex(zip);
    rig.host.files[package_path_of(e)] = zip;
    if (tweak_entry) tweak_entry(e);
    rig.host.publish(100);
    rig.write_reader();
    Snapshot before = snapshot(rig.root());
    Engine& engine = rig.make_engine();
    CancelToken cancel;
    auto reader = engine.open_reader(rig.root());
    PB_CHECK(reader.ok());
    Attempt a;
    Action action;
    action.type = Action::Type::Install;
    action.app_id = "testapp";
    auto plan = engine.plan(**reader.value, action, nullptr, cancel);
    a.planned = plan.ok();
    a.findings = plan.findings;
    a.diff = compare(before, snapshot(rig.root()));
    return a;
}

}  // namespace

PB_TEST(every_broken_package_of_feature_001_is_refused_with_its_rule_id_and_the_reader_is_untouched) {
    int refused = 0;
    for (const Fixture& f : invalid_package_fixtures()) {
        EngineRig rig;
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        Attempt a = try_install(rig, f.zip);
        if (a.planned) PB_FAIL_AT(f.rule + "/" + f.what + ": a plan was made");
        if (!has_rule(a.findings, f.rule)) PB_FAIL_AT(f.rule + "/" + f.what + ": the rule id is missing from the findings");
        if (!a.diff.identical) PB_FAIL_AT(f.rule + "/" + f.what + ": the reader changed: " + a.diff.describe());
        ++refused;
    }
    PB_CHECK(refused > 30);
}

PB_TEST(a_name_that_is_too_long_a_hidden_name_and_a_file_in_the_data_folder_are_refused) {
    SampleApp base = nested_sample_app();
    struct Case { const char* what; std::string name; };
    const Case cases[] = {
        {"over-long name", "applications/" + std::string(300, 'a') + ".bin"},
        {"hidden name", "applications/.hidden"},
        {"hidden folder", "applications/.cache/x.bin"},
        {"file in the data folder", "system/testapp/own.db"},
        {"in the installer area", "system/pbstore/installed/x.json"},
    };
    for (const Case& c : cases) {
        EngineRig rig;
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        SampleApp app = nested_sample_app();
        std::vector<ZEntry> files = app.files;
        files.push_back(file(c.name, "payload"));
        pbstore::json::Value m = app.manifest;
        m.set("files", files_value(files));
        Attempt a = try_install(rig, build_package(m, files));
        if (a.planned) PB_FAIL_AT(std::string(c.what) + ": a plan was made");
        if (a.findings.empty()) PB_FAIL_AT(std::string(c.what) + ": no finding");
        if (!a.diff.identical) PB_FAIL_AT(std::string(c.what) + ": the reader changed");
    }
}

PB_TEST(claimed_entry_ids_claimed_data_folders_and_owned_paths_are_refused_by_the_planner) {
    {   // the menu entry id of an entry that is already on the reader (installed by hand)
        FakeReader r;
        r.add_hand_installed(make_sample_app("handapp", "1.0.0", "U_TestApp", "system/handapp"));
        EngineRig rig(r);
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        SampleApp app = nested_sample_app();
        Attempt a = try_install(rig, build_package(app));
        PB_CHECK(!a.planned);
        PB_CHECK(has_rule(a.findings, "E-INS-15"));
        PB_CHECK(a.diff.identical);
    }
    {   // the data folder of an installed app
        FakeReader r = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
        EngineRig rig(r);
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        SampleApp app = nested_sample_app("testapp", "1.2.0", "U_TestApp");
        app.manifest.set("data_dir", pbstore::json::Value::from_string("system/otherapp"));
        Attempt a = try_install(rig, build_package(app), [](CatalogEntry& e) { e.data_dir = "system/otherapp"; });
        PB_CHECK(!a.planned);
        PB_CHECK(has_rule(a.findings, "E-INS-16"));
        PB_CHECK(a.diff.identical);
    }
    {   // a path that the record of another app lists
        FakeReader r = with_installed(FakeReader(), nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
        EngineRig rig(r);
        if (!rig.ready()) PB_SKIP("openssl CLI not available");
        SampleApp app = nested_sample_app();
        app.files.push_back(file("applications/otherapp.bin", "mine now"));
        app.manifest.set("files", files_value(app.files));
        Attempt a = try_install(rig, build_package(app));
        PB_CHECK(!a.planned);
        PB_CHECK(has_rule(a.findings, "E-INS-21"));
        PB_CHECK(a.diff.identical);
    }
}

PB_TEST(a_link_planted_in_the_target_tree_of_a_folder_chosen_by_hand_is_refused_and_nothing_is_written_through_it) {
    EngineRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.write_reader();
    TmpDir outside;
    outside.write("secret.txt", "outside the reader");
    // applications/testapp -> a folder outside the reader
    PB_CHECK_EQ(::symlink(outside.path().c_str(), (rig.root() + "/applications/testapp").c_str()), 0);
    SampleApp app = nested_sample_app();
    CatalogEntry& e = rig.host.add_app(app);
    (void)e;
    rig.host.publish(100);
    Snapshot outside_before = snapshot(outside.path());
    Snapshot before = snapshot(rig.root());
    Engine& engine = rig.make_engine();
    CancelToken cancel;
    auto reader = engine.open_reader(rig.root());
    PB_CHECK(reader.ok());
    Action action;
    action.type = Action::Type::Install;
    action.app_id = "testapp";
    auto plan = engine.plan(**reader.value, action, nullptr, cancel);
    PB_CHECK(!plan.ok());
    PB_CHECK(has_rule(plan.findings, "E-INS-22"));
    PB_CHECK(compare(before, snapshot(rig.root())).identical);
    PB_CHECK(compare(outside_before, snapshot(outside.path())).identical);
}

PB_TEST_MAIN()
