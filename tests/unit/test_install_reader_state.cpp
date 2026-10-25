// T060: scan() is the only look at a reader: records, the desktop configuration, the state of the paths a
// plan touches, and data folders (counted, never read).
#include "install_fixtures.h"
#include "pbstore/install/reader_state.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
Identity known() { return Identity{std::string("1030"), std::string("6.11.1437")}; }
}  // namespace

PB_TEST(records_view_and_identity_are_read) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app());
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    PB_CHECK(s.findings.empty());
    PB_CHECK(s.desktop.has_value());
    PB_CHECK(s.desktop->has_entry("U_TestApp"));
    PB_CHECK_EQ(s.installed.size(), size_t(1));
    PB_CHECK(s.installed.count("testapp") == 1);
    PB_CHECK_EQ(s.installed["testapp"].record.version, std::string("1.2.0"));
    PB_CHECK_EQ(s.view_sha256, s.desktop->sha256);
    PB_CHECK_EQ(s.identity.model.value_or(""), std::string("1030"));
    PB_CHECK(s.space.writable);
    PB_CHECK(s.space.free_bytes > 0);
    PB_CHECK(!s.journal_present);
    PB_CHECK(!s.backups.empty());                      // the backup the installation made
}

PB_TEST(a_record_that_is_not_understood_is_a_finding_and_a_newer_major_touches_nothing) {
    FakeReader r;
    r.add_record("brokenapp", "{ not json");
    r.add_record("newapp", "{\"schema_version\":\"2.0\",\"app_id\":\"newapp\"}");
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    PB_CHECK(s.installed.empty());
    int e35 = 0;
    for (const auto& f : s.findings) if (f.rule_id == "E-INS-35") ++e35;
    PB_CHECK_EQ(e35, 2);
}

PB_TEST(a_record_whose_name_differs_from_its_app_id_is_not_taken) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app());
    std::string text = r.files["system/pbstore/installed/testapp.json"];
    r.files.erase("system/pbstore/installed/testapp.json");
    r.files["system/pbstore/installed/other.json"] = text;
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    PB_CHECK(s.installed.empty());
    PB_CHECK(!s.findings.empty());
}

PB_TEST(a_view_json_that_is_not_acceptable_is_reported) {
    FakeReader r;
    r.files["system/config/desktop/view.json"] = "{ \"applications\": {}";
    Volume v(r);
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    PB_CHECK(!s.desktop.has_value());
    PB_CHECK(!s.findings.empty());
    PB_CHECK_EQ(s.findings[0].rule_id, std::string("E-INS-12"));
    FakeReader r2;
    r2.files["system/config/desktop/view.json"] = "{ \"applications\": {} }";
    Volume v2(r2);
    ReaderSnapshot s2 = scan(v2.fat, known(), ScanRequest{});
    PB_CHECK_EQ(s2.findings[0].rule_id, std::string("E-INS-13"));
}

PB_TEST(only_target_and_recorded_paths_are_hashed_and_folders_are_listed) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app());
    Volume v(r);
    TraceFs fs(v.fat);
    SampleApp other = nested_sample_app("otherapp", "1.0.0", "U_OtherApp", 5);
    pbstore::Manifest m = manifest_of(other);
    ScanRequest req;
    req.app_id = "otherapp";
    req.manifest = &m;
    ReaderSnapshot s = scan(fs, known(), req);
    for (const auto& f : m.files) PB_CHECK(s.facts.count(f.path) == 1);
    PB_CHECK(s.facts["applications/otherapp.app"].kind == Kind::None);
    PB_CHECK(s.facts["applications"].kind == Kind::Dir);
    PB_CHECK(s.listings.count("applications") == 1);
    // the files of the installed testapp were not read: they are not targets of this request
    for (const auto& op : fs.ops)
        if (op.name == "read" && op.a.compare(0, 24, "applications/testapp.app") == 0) PB_FAIL_AT("a file that is no target was read: " + op.a);
    // with all_recorded the recorded files are hashed
    ScanRequest all;
    all.all_recorded = true;
    ReaderSnapshot t = scan(v.fat, known(), all);
    PB_CHECK(t.facts.count("applications/testapp.app") == 1);
    PB_CHECK(t.installed["testapp"].changed_files.empty());
}

PB_TEST(a_recorded_file_that_was_changed_or_removed_is_listed) {
    FakeReader base;
    FakeReader r = with_installed(base, nested_sample_app());
    r.files["applications/testapp.bin"] = "edited by hand";
    r.files.erase("applications/icons/testapp_f.bmp");
    Volume v(r);
    ScanRequest all;
    all.all_recorded = true;
    ReaderSnapshot s = scan(v.fat, known(), all);
    std::vector<std::string> changed = s.installed["testapp"].changed_files;
    std::sort(changed.begin(), changed.end());
    PB_CHECK_EQ(changed, (std::vector<std::string>{"applications/icons/testapp_f.bmp", "applications/testapp.bin"}));
}

PB_TEST(data_folders_are_counted_without_reading_their_files) {
    FakeReader r;
    r.add_data_dir("testapp", {{"a.db", "12345"}, {"sub/b.db", "123"}});
    Volume v(r);
    TraceFs fs(v.fat);
    SampleApp app = nested_sample_app();
    pbstore::Manifest m = manifest_of(app);
    ScanRequest req;
    req.app_id = "testapp";
    req.manifest = &m;
    ReaderSnapshot s = scan(fs, known(), req);
    PB_CHECK(s.data["system/testapp"].exists);
    PB_CHECK_EQ(s.data["system/testapp"].files, uint64_t(2));
    PB_CHECK_EQ(s.data["system/testapp"].bytes, uint64_t(8));
    for (const auto& op : fs.ops)
        if (op.name == "read" && op.a.compare(0, 14, "system/testapp") == 0) PB_FAIL_AT("a file of the data folder was read: " + op.a);
}

PB_TEST(a_link_like_object_is_reported) {
    FakeReader base;
    CrashFs mem;
    base.load_into(mem);
    mem.plant_link("applications/testapp.app");
    FatRulesFs fat(mem);
    SampleApp app = nested_sample_app();
    pbstore::Manifest m = manifest_of(app);
    ScanRequest req;
    req.app_id = "testapp";
    req.manifest = &m;
    ReaderSnapshot s = scan(fat, known(), req);
    PB_CHECK(s.facts["applications/testapp.app"].kind == Kind::Link);
}

PB_TEST(the_fingerprint_changes_when_view_json_a_target_path_or_the_free_space_changes) {
    FakeReader r;
    Volume v(r);
    SampleApp app = nested_sample_app();
    pbstore::Manifest m = manifest_of(app);
    ScanRequest req;
    req.app_id = "testapp";
    req.manifest = &m;
    std::vector<std::string> paths = {"applications/testapp.app", "applications/icons/testapp.bmp"};
    ReaderSnapshot s0 = scan(v.fat, known(), req);
    const std::string f0 = fingerprint_of(snapshot_basis(s0, paths));
    PB_CHECK_EQ(fingerprint_of(snapshot_basis(scan(v.fat, known(), req), paths)), f0);   // stable
    v.mem.create_exclusive("applications/testapp.app", "appeared", true);
    PB_CHECK(fingerprint_of(snapshot_basis(scan(v.fat, known(), req), paths)) != f0);
    v.mem.remove_file("applications/testapp.app");
    PB_CHECK_EQ(fingerprint_of(snapshot_basis(scan(v.fat, known(), req), paths)), f0);
    v.mem.set_space(12345, 4096, true, false);
    PB_CHECK(fingerprint_of(snapshot_basis(scan(v.fat, known(), req), paths)) != f0);
    v.mem.set_space(1ull << 30, 4096, true, false);
    std::string view = reader_view_text(v.fat);
    v.mem.remove_file("system/config/desktop/view.json");
    v.mem.create_exclusive("system/config/desktop/view.json", view + " ", true);
    PB_CHECK(fingerprint_of(snapshot_basis(scan(v.fat, known(), req), paths)) != f0);
}

PB_TEST(a_free_space_change_of_less_than_one_mib_is_no_change_wherever_it_falls_between_two_mib) {
    const uint64_t mib = 1024 * 1024;
    PB_CHECK(!free_space_changed(6842 * mib - 1, 6842 * mib));       // across a whole-MiB boundary
    PB_CHECK(!free_space_changed(6842 * mib + 100, 6842 * mib - 100));
    PB_CHECK(!free_space_changed(5 * mib, 5 * mib));
    PB_CHECK(free_space_changed(6842 * mib, 6841 * mib));
    PB_CHECK(free_space_changed(6841 * mib, 6842 * mib));
    FingerprintBasis a, b;
    a.view_sha256 = b.view_sha256 = "x";
    a.free_bytes = 6842 * mib - 1;
    b.free_bytes = 6842 * mib;
    PB_CHECK(basis_unchanged(a, b));
    b.free_bytes = 6840 * mib;
    PB_CHECK(!basis_unchanged(a, b));
    b.free_bytes = a.free_bytes;
    b.facts.push_back("applications/x\tabsent");
    PB_CHECK(!basis_unchanged(a, b));
}

PB_TEST(the_fingerprint_equals_the_one_the_executor_recomputes) {
    FakeReader r;
    Volume v(r);
    SampleApp app = nested_sample_app();
    Built b = make_install_plan(v.fat, app);
    pbstore::Manifest m = manifest_of(app);
    ScanRequest req;
    req.app_id = "testapp";
    req.manifest = &m;
    ReaderSnapshot s = scan(v.fat, known(), req);
    PB_CHECK_EQ(fingerprint_of(snapshot_basis(s, plan_target_paths(b.plan))), b.plan.fingerprint);
}

PB_TEST(an_interrupted_operation_is_noticed) {
    FakeReader r;
    Volume v(r);
    Built b = make_install_plan(v.fat, nested_sample_app());
    v.mem.lose_power_at(30, false);
    try { run_execute(v.fat, b); } catch (const PowerLoss&) {}
    v.mem.reboot();
    ReaderSnapshot s = scan(v.fat, known(), ScanRequest{});
    PB_CHECK(s.journal_present);
}

PB_TEST_MAIN()
