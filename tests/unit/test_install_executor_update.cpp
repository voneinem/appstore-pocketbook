// T086: the executor, kind update.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
FakeReader installed_v1() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
    return with_installed(r, nested_sample_app());
}
SampleApp v2() { return nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"); }
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
}  // namespace

PB_TEST(the_update_leaves_exactly_the_new_version_and_the_data_folder_untouched) {
    FakeReader r = installed_v1();
    Volume v(r);
    Snapshot before = snapshot(v.fat);
    Built b = make_planned(v.fat, v2(), PlanKind::Update);
    PB_CHECK(run_execute(v.fat, b).ok());
    for (const auto& f : v2().files) {
        std::string got;
        PB_CHECK(v.fat.read_file(f.name, 1u << 20, &got));
        PB_CHECK_EQ(got, f.data);
    }
    PB_CHECK(!v.fat.stat("applications/testapp/lib/extra/data.txt").exists());            // only in the old version: gone
    PB_CHECK(!v.fat.stat("applications/testapp/lib/extra").exists());                     // and its folder, which the install made
    std::string rec;
    PB_CHECK(v.fat.read_file("system/pbstore/installed/testapp.json", 1u << 20, &rec));
    RecordParse rp = parse_install_record(rec, "testapp");
    PB_CHECK(rp.record.has_value());
    PB_CHECK_EQ(rp.record->version, std::string("1.3.0"));
    PB_CHECK_EQ(rp.record->files.size(), v2().files.size());
    PB_CHECK(!journal_exists(v.fat));
    // data folder and every unrelated file are byte-identical
    Diff d = compare(before, snapshot(v.fat));
    for (const std::string& p : d.added) PB_CHECK(starts_with(p, "applications/testapp") || in_installer_area(p));
    for (const std::string& p : d.removed) PB_CHECK(starts_with(p, "applications/testapp") || in_installer_area(p));
    for (const std::string& p : d.changed) PB_CHECK(starts_with(p, "applications/") || starts_with(p, "applications/icons/testapp") || in_installer_area(p));
    std::string notes;
    PB_CHECK(v.fat.read_file("system/testapp/notes.db", 100, &notes));
    PB_CHECK_EQ(notes, std::string("precious"));
}

PB_TEST(each_file_is_vacated_then_placed_and_old_only_files_are_vacated_and_the_record_is_replaced) {
    FakeReader r = installed_v1();
    Volume v(r);
    Built b = make_planned(v.fat, v2(), PlanKind::Update);
    TraceFs fs(v.fat);
    PB_CHECK(run_execute(fs, b).ok());
    std::vector<std::string> live;
    for (const auto& op : fs.ops) {
        if (!op.mutating || op.name != "rename") continue;
        if (starts_with(op.a, "applications/") && starts_with(op.b, "system/pbstore/journal/current/old/")) live.push_back("vacate " + op.a);
        else if (starts_with(op.b, "applications/")) live.push_back("place " + op.b);
        else if (op.a == "system/pbstore/installed/testapp.json") live.push_back("vacate record");
        else if (op.b == "system/pbstore/installed/testapp.json") live.push_back("place record");
    }
    // a replaced file: vacate directly followed by place
    for (size_t i = 0; i + 1 < live.size(); ++i) {
        if (starts_with(live[i], "vacate applications/") && live[i].find("data.txt") == std::string::npos) {
            const std::string path = live[i].substr(7);
            PB_CHECK_EQ(live[i + 1], "place " + path);
        }
    }
    bool dropped = false;
    for (size_t i = 0; i < live.size(); ++i) if (live[i] == "vacate applications/testapp/lib/extra/data.txt") { dropped = true; PB_CHECK(i + 1 >= live.size() || !starts_with(live[i + 1], "place applications/testapp/lib/extra/data.txt")); }
    PB_CHECK(dropped);
    PB_CHECK_EQ(live[live.size() - 2], std::string("vacate record"));
    PB_CHECK_EQ(live.back(), std::string("place record"));
    // the entry does not change, so the desktop configuration is not touched at all
    for (const auto& op : fs.ops) PB_CHECK(!(op.mutating && (op.a == "system/config/desktop/view.json" || op.b == "system/config/desktop/view.json")));
}

PB_TEST(a_failure_at_any_point_before_the_commit_leaves_the_previous_version_complete) {
    FakeReader r = installed_v1();
    int total = 0;
    Snapshot old_live;
    {
        Volume v(r);
        old_live = without_installer_area(snapshot(v.fat));
        Built b = make_planned(v.fat, v2(), PlanKind::Update);
        v.mem.reboot();
        run_execute(v.fat, b);
        total = v.mem.mutating_calls();
    }
    int undone = 0;
    for (int k = 1; k <= total; ++k) {
        Volume v(r);
        Built b = make_planned(v.fat, v2(), PlanKind::Update);
        v.mem.fail_mutating_call(k);
        ExecResult res = run_execute(v.fat, b);
        if (res.ok()) continue;
        if (res.outcome == ExecResult::Outcome::RepairNeeded) continue;     // a failure in the clean-up after the commit
        ++undone;
        const Diff d = compare(old_live, without_installer_area(snapshot(v.fat)));
        if (!d.identical) PB_FAIL_AT("call " + std::to_string(k) + " failed and the old version is not complete: " + d.describe());
        PB_CHECK(!journal_exists(v.fat));
    }
    PB_CHECK(undone > total / 2);
}

PB_TEST(edited_files_are_replaced_only_with_the_confirmation_and_the_data_folder_stays_either_way) {
    FakeReader r = installed_v1();
    r.files["applications/testapp.bin"] = "the user edited this";
    Volume v(r);
    Built b = make_planned(v.fat, v2(), PlanKind::Update);
    PB_CHECK(b.plan.confirmations.count(Confirmation::EditedFiles) == 1);
    CancelToken cancel;
    ExecResult none = execute(v.fat, b.plan, b.content, Confirmations{Confirmation::Plan}, nullptr, cancel);
    PB_CHECK(!none.ok());
    PB_CHECK(none.outcome == ExecResult::Outcome::NotStarted);
    std::string c;
    PB_CHECK(v.fat.read_file("applications/testapp.bin", 100, &c));
    PB_CHECK_EQ(c, std::string("the user edited this"));
    PB_CHECK(run_execute(v.fat, b).ok());                                   // with all confirmations
    PB_CHECK(v.fat.read_file("system/testapp/notes.db", 100, &c));
    PB_CHECK_EQ(c, std::string("precious"));
}

PB_TEST_MAIN()
