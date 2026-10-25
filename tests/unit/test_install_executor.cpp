// T043: the executor, kind install, with a hand-built plan on the in-memory volume behind FAT rules.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
struct Rig {
    FakeReader reader;
    CrashFs mem;
    TraceFs fs{mem};
    SampleApp app = nested_sample_app();
    Rig() { reader.load_into(mem); }
};
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
}  // namespace

PB_TEST(a_successful_install_leaves_the_files_one_entry_and_a_record) {
    Rig r;
    FatRulesFs fat(r.mem);
    Snapshot before = snapshot(fat);
    Built b = make_install_plan(fat, r.app);
    TraceFs fs(fat);
    ExecResult res = run_execute(fs, b);
    PB_CHECK(res.ok());
    PB_CHECK(res.findings.empty());
    PB_CHECK(res.outcome == ExecResult::Outcome::Done);
    // every file of the package is there with the right content
    for (const auto& kv : b.content.files) {
        std::string got;
        PB_CHECK(fat.read_file(kv.first, 1u << 20, &got));
        PB_CHECK_EQ(got, kv.second);
    }
    // the desktop configuration is the prepared text
    PB_CHECK_EQ(reader_view_text(fat), b.plan.view_new_text);
    // the record has the documented fields
    std::string rec;
    PB_CHECK(fat.read_file("system/pbstore/installed/testapp.json", 1u << 20, &rec));
    RecordParse rp = parse_install_record(rec, "testapp");
    PB_CHECK(rp.record.has_value());
    PB_CHECK_EQ(rp.record->version, std::string("1.2.0"));
    PB_CHECK_EQ(rp.record->source, std::string("catalog"));
    PB_CHECK_EQ(rp.record->files.size(), b.plan.items.size());
    PB_CHECK_EQ(rec, b.plan.record_text);
    // no journal is left
    PB_CHECK(!journal_exists(fat));
    // only the paths of the contract changed
    Diff d = compare(before, snapshot(fat));
    std::vector<std::string> touched = d.added;
    touched.insert(touched.end(), d.changed.begin(), d.changed.end());
    touched.insert(touched.end(), d.removed.begin(), d.removed.end());
    for (const std::string& p : touched) {
        const bool ok = starts_with(p, "applications/") || in_installer_area(p) || p == "system/config/desktop/view.json";
        if (!ok) PB_FAIL_AT("changed outside the allowed paths: " + p);
    }
    PB_CHECK(std::find(d.changed.begin(), d.changed.end(), "system/config/desktop/view.json") != d.changed.end());
}

PB_TEST(prepare_writes_only_inside_the_installer_area_and_the_live_area_is_unchanged_until_prepared_exists) {
    FakeReader reader;
    Snapshot original;
    int total = 0;
    {
        Volume v(reader);
        original = snapshot(v.fat);
        Built b = make_install_plan(v.fat, nested_sample_app());
        v.mem.reboot();
        run_execute(v.fat, b);
        total = v.mem.mutating_calls();
    }
    PB_CHECK(total > 20);
    bool saw_unprepared = false, saw_prepared = false;
    for (int n = 1; n <= total; ++n) {
        Volume v(reader);
        Built b = make_install_plan(v.fat, nested_sample_app());
        v.mem.lose_power_at(n, false);
        try { run_execute(v.fat, b); } catch (const PowerLoss&) {}
        v.mem.reboot();
        const bool prepared = v.mem.stat(std::string(kJournalDir) + "/" + kPreparedName).exists();
        const bool committed = v.mem.stat(std::string(kJournalDir) + "/" + kCommitName).exists();
        const bool has_intent = v.mem.stat(std::string(kJournalDir) + "/" + kIntentName).exists();
        if (!prepared && !committed && has_intent) {
            saw_unprepared = true;
            Diff d = compare(without_installer_area(original), without_installer_area(snapshot(v.fat)));
            if (!d.identical) PB_FAIL_AT("the live area changed before the journal was prepared (power lost at call " + std::to_string(n) + "): " + d.describe());
        }
        if (prepared) saw_prepared = true;
    }
    PB_CHECK(saw_unprepared);
    PB_CHECK(saw_prepared);
}

PB_TEST(apply_creates_directories_then_places_files_sorted_with_the_launcher_last_then_the_entry_then_the_record) {
    Rig r;
    FatRulesFs fat(r.mem);
    Built b = make_install_plan(fat, r.app);
    TraceFs fs(fat);
    PB_CHECK(run_execute(fs, b).ok());
    std::vector<std::string> live;   // changes of the live area in order
    for (const auto& op : fs.ops) {
        if (!op.mutating) continue;
        if (op.name == "make_dir" && !in_installer_area(op.a)) live.push_back("mkdir " + op.a);
        if (op.name == "rename" && !in_installer_area(op.b)) live.push_back("place " + op.b);
    }
    std::vector<std::string> expected;
    for (const auto& d : b.plan.directories_to_create) expected.push_back("mkdir " + d);
    for (const auto& it : b.plan.items) expected.push_back("place " + it.path);
    expected.push_back("place system/config/desktop/view.json");
    expected.push_back("place system/pbstore/installed/testapp.json");
    // record and view are inside/outside: the record lives in the installer area
    expected.erase(std::remove(expected.begin(), expected.end(), std::string("place system/pbstore/installed/testapp.json")), expected.end());
    PB_CHECK_EQ(live, expected);
    // the launcher is the last of the files
    size_t last_file = 0;
    for (size_t i = 0; i < live.size(); ++i) if (starts_with(live[i], "place applications/")) last_file = i;
    PB_CHECK_EQ(live[last_file], "place " + b.plan.launcher);
    // sorted: the files before the launcher are in path order
    std::vector<std::string> before_launcher;
    for (size_t i = 0; i < last_file; ++i) if (starts_with(live[i], "place applications/")) before_launcher.push_back(live[i]);
    PB_CHECK(std::is_sorted(before_launcher.begin(), before_launcher.end()));
}

PB_TEST(between_the_first_and_the_last_change_of_the_live_area_no_file_data_is_written) {
    Rig r;
    FatRulesFs fat(r.mem);
    Built b = make_install_plan(fat, r.app);
    TraceFs fs(fat);
    PB_CHECK(run_execute(fs, b).ok());
    long first = -1, last = -1;
    for (size_t i = 0; i < fs.ops.size(); ++i) {
        const auto& op = fs.ops[i];
        if (!op.mutating) continue;
        const bool live = (op.name == "make_dir" && !in_installer_area(op.a)) ||
                          (op.name == "rename" && (!in_installer_area(op.a) || !in_installer_area(op.b)));
        if (live) {
            if (first < 0) first = static_cast<long>(i);
            last = static_cast<long>(i);
        }
    }
    PB_CHECK(first >= 0 && last > first);
    for (long i = first; i <= last; ++i)
        if (fs.ops[static_cast<size_t>(i)].name == "create") PB_FAIL_AT("file data written inside the interval of renames: " + fs.ops[static_cast<size_t>(i)].a);
}

PB_TEST(success_is_reported_only_after_the_volume_was_flushed_and_the_journal_removed) {
    Rig r;
    FatRulesFs fat(r.mem);
    Built b = make_install_plan(fat, r.app);
    TraceFs fs(fat);
    PB_CHECK(run_execute(fs, b).ok());
    const TraceFs::Op* last_mutation = nullptr;
    for (const auto& op : fs.ops) if (op.mutating) last_mutation = &op;
    PB_CHECK(last_mutation != nullptr);
    PB_CHECK_EQ(last_mutation->name, std::string("flush_volume"));
    PB_CHECK(!journal_exists(fat));
}

PB_TEST(the_data_folder_of_the_app_is_not_touched) {
    Rig r;
    r.reader = FakeReader();
    r.reader.add_data_dir("testapp", {{"notes.db", "precious"}});
    CrashFs mem;
    r.reader.load_into(mem);
    FatRulesFs fat(mem);
    Snapshot before = snapshot(fat);
    Built b = make_install_plan(fat, r.app);
    TraceFs fs(fat);
    PB_CHECK(run_execute(fs, b).ok());
    for (const auto& op : fs.ops) {
        // looking whether the folder exists is allowed; reading, listing, writing or deleting in it is not
        if ((starts_with(op.a, "system/testapp") || starts_with(op.b, "system/testapp")) && op.name != "stat")
            PB_FAIL_AT("the data folder was accessed: " + op.name + " " + op.a);
    }
    std::string notes;
    PB_CHECK(fat.read_file("system/testapp/notes.db", 100, &notes));
    PB_CHECK_EQ(notes, std::string("precious"));
}

PB_TEST(the_plan_needs_its_confirmation_or_nothing_is_written) {
    Rig r;
    FatRulesFs fat(r.mem);
    Snapshot before = snapshot(fat);
    Built b = make_install_plan(fat, r.app);
    b.plan.confirmations.insert(Confirmation::UnknownFirmware);
    CancelToken cancel;
    ExecResult res = execute(fat, b.plan, b.content, Confirmations{Confirmation::Plan}, [](const Progress&) {}, cancel);
    PB_CHECK(!res.ok());
    PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
    PB_CHECK_EQ(res.missing_confirmations.size(), size_t(1));
    PB_CHECK(res.missing_confirmations[0] == Confirmation::UnknownFirmware);
    PB_CHECK(compare(before, snapshot(fat)).identical);
    ExecResult none = execute(fat, b.plan, b.content, Confirmations{}, [](const Progress&) {}, cancel);
    PB_CHECK(!none.ok());
    PB_CHECK(compare(before, snapshot(fat)).identical);
}

PB_TEST(content_that_does_not_match_the_plan_is_refused_and_nothing_is_changed) {
    Rig r;
    FatRulesFs fat(r.mem);
    Built b = make_install_plan(fat, r.app);
    Snapshot before = without_installer_area(snapshot(fat));
    b.content.files[b.plan.items[0].path] += "tampered";
    ExecResult res = run_execute(fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(!res.findings.empty());
    PB_CHECK(compare(before, without_installer_area(snapshot(fat))).identical);
    PB_CHECK(!journal_exists(fat));
}

PB_TEST_MAIN()
