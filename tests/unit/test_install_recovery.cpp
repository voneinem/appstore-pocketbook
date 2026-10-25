// T045: recovery decisions on journal states (contracts/journal.md). The states are made by cutting a real
// installation off at a chosen mutating call, so they are exactly what a pulled cable leaves.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {

FakeReader make_reader() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}});
    return r;
}

struct Outcome {
    Snapshot original, installed;
    int total = 0;
};

Outcome reference_run(const FakeReader& reader) {
    Outcome o;
    Volume v(reader);
    o.original = snapshot(v.fat);
    Built b = make_install_plan(v.fat, nested_sample_app());
    v.mem.reboot();
    PB_CHECK(run_execute(v.fat, b).ok());
    o.total = v.mem.mutating_calls();
    o.installed = snapshot(v.fat);
    return o;
}

// A volume where the installation lost power at call n (clean mode), after the reboot.
std::unique_ptr<Volume> cut_at(const FakeReader& reader, int n) {
    auto v = std::make_unique<Volume>(reader);
    Built b = make_install_plan(v->fat, nested_sample_app());
    v->mem.lose_power_at(n, false);
    try { run_execute(v->fat, b); } catch (const PowerLoss&) {}
    v->mem.reboot();
    return v;
}

struct Found {
    int discard = 0, undo = 0, finish = 0;
};
// the first call number that leaves each kind of state
Found first_of_each_kind(const FakeReader& reader, int total) {
    Found f;
    for (int n = 1; n <= total; ++n) {
        auto v = cut_at(reader, n);
        RepairCheck rc = plan_repair(v->fat);
        if (!rc.plan) continue;
        if (rc.plan->action == RepairPlan::Action::Discard && !f.discard) f.discard = n;
        if (rc.plan->action == RepairPlan::Action::Undo && !f.undo) f.undo = n;
        if (rc.plan->action == RepairPlan::Action::Finish && !f.finish) f.finish = n;
    }
    return f;
}
}  // namespace

PB_TEST(no_journal_means_nothing_to_repair) {
    FakeReader reader = make_reader();
    Volume v(reader);
    RepairCheck rc = plan_repair(v.fat);
    PB_CHECK(!rc.plan.has_value());
    PB_CHECK(rc.findings.empty());
}

PB_TEST(a_journal_without_a_valid_intent_is_discarded_as_undone) {
    FakeReader reader = make_reader();
    Volume v(reader);
    v.mem.make_dir("system/pbstore");
    v.mem.make_dir("system/pbstore/journal");
    v.mem.make_dir("system/pbstore/journal/current");
    v.mem.create_exclusive(std::string(kJournalDir) + "/10-intent.json", "{\"journal_format\": 1, \"kind\": \"inst", true);   // cut off
    Snapshot live_before = without_installer_area(snapshot(v.fat));
    RepairCheck rc = plan_repair(v.fat);
    PB_CHECK(rc.plan.has_value());
    PB_CHECK(rc.plan->action == RepairPlan::Action::Discard);
    RepairResult rr = repair(v.fat, *rc.plan);
    PB_CHECK(rr.done);
    PB_CHECK_EQ(rr.outcome, std::string("undone"));
    PB_CHECK(!journal_exists(v.fat));
    PB_CHECK(compare(live_before, without_installer_area(snapshot(v.fat))).identical);
}

PB_TEST(every_state_of_a_cut_off_installation_ends_in_the_old_or_the_new_complete_version) {
    FakeReader reader = make_reader();
    Outcome ref = reference_run(reader);
    Found f = first_of_each_kind(reader, ref.total);
    PB_CHECK(f.discard > 0);
    PB_CHECK(f.undo > 0);
    PB_CHECK(f.finish > 0);

    {   // intent without a prepared marker: discarded, live area untouched
        auto v = cut_at(reader, f.discard);
        RepairCheck rc = plan_repair(v->fat);
        RepairResult rr = repair(v->fat, *rc.plan);
        PB_CHECK(rr.done);
        PB_CHECK_EQ(rr.outcome, std::string("undone"));
        PB_CHECK(compare(without_installer_area(ref.original), without_installer_area(snapshot(v->fat))).identical);
        PB_CHECK(!journal_exists(v->fat));
    }
    {   // prepared, not committed: undone, the old version is complete
        auto v = cut_at(reader, f.undo);
        RepairCheck rc = plan_repair(v->fat);
        PB_CHECK(rc.plan.has_value());
        const std::string text = rc.plan->text();
        PB_CHECK(text.find("testapp") != std::string::npos);
        PB_CHECK(text.find("undo") != std::string::npos || text.find("Undo") != std::string::npos);
        PB_CHECK(!rc.plan->reason.empty());
        RepairResult rr = repair(v->fat, *rc.plan);
        PB_CHECK(rr.done);
        PB_CHECK_EQ(rr.outcome, std::string("undone"));
        Diff d = compare(without_installer_area(ref.original), without_installer_area(snapshot(v->fat)));
        PB_CHECK(d.identical);
        PB_CHECK(!journal_exists(v->fat));
    }
    {   // committed: finished, the new version is complete
        auto v = cut_at(reader, f.finish);
        RepairCheck rc = plan_repair(v->fat);
        PB_CHECK(rc.plan.has_value());
        PB_CHECK(rc.plan->text().find("finish") != std::string::npos);
        RepairResult rr = repair(v->fat, *rc.plan);
        PB_CHECK(rr.done);
        PB_CHECK_EQ(rr.outcome, std::string("finished"));
        Diff d = compare(without_installer_area(ref.installed), without_installer_area(snapshot(v->fat)));
        PB_CHECK(d.identical);
        PB_CHECK(!journal_exists(v->fat));
        std::string rec;
        PB_CHECK(v->fat.read_file("system/pbstore/installed/testapp.json", 1u << 20, &rec));
    }
}

PB_TEST(repairing_twice_changes_nothing_the_second_time) {
    FakeReader reader = make_reader();
    Outcome ref = reference_run(reader);
    for (int n = 1; n <= ref.total; n += 3) {
        auto v = cut_at(reader, n);
        RepairCheck rc = plan_repair(v->fat);
        if (!rc.plan) continue;
        PB_CHECK(repair(v->fat, *rc.plan).done);
        Snapshot after = snapshot(v->fat);
        RepairCheck again = plan_repair(v->fat);
        PB_CHECK(!again.plan.has_value());
        PB_CHECK(compare(after, snapshot(v->fat)).identical);
    }
}

PB_TEST(a_live_item_that_matches_neither_old_nor_new_is_left_alone_and_reported) {
    FakeReader reader = make_reader();
    Outcome ref = reference_run(reader);
    Found f = first_of_each_kind(reader, ref.total);
    // an undo state in which at least one file was already placed
    int n = f.undo;
    std::unique_ptr<Volume> v;
    std::string placed;
    for (; n <= ref.total; ++n) {
        v = cut_at(reader, n);
        RepairCheck rc = plan_repair(v->fat);
        if (!rc.plan || rc.plan->action != RepairPlan::Action::Undo) continue;
        for (const auto& it : rc.plan->intent.items)
            if (v->fat.stat(it.path).exists()) { placed = it.path; break; }
        if (!placed.empty()) break;
    }
    PB_CHECK(!placed.empty());
    // the user (or the reader) replaced that file with something else
    PB_CHECK(v->fat.remove_file(placed));
    PB_CHECK(v->fat.create_exclusive(placed, "someone else's content", true));
    RepairCheck rc = plan_repair(v->fat);
    RepairResult rr = repair(v->fat, *rc.plan);
    PB_CHECK(!rr.done);
    PB_CHECK(rr.effect == ReaderEffect::RepairNeeded);
    bool named = false;
    for (const auto& finding : rr.findings)
        if (finding.rule_id == "E-INS-24" && finding.message.find(placed) != std::string::npos) named = true;
    PB_CHECK(named);
    std::string content;
    PB_CHECK(v->fat.read_file(placed, 100, &content));
    PB_CHECK_EQ(content, std::string("someone else's content"));    // a file whose hash recovery did not write is never deleted
    PB_CHECK(journal_exists(v->fat));                               // the journal is kept
    // every other item was handled: no other recorded file of the new version is left in place
    for (const auto& it : rc.plan->intent.items)
        if (it.path != placed) PB_CHECK(!v->fat.stat(it.path).exists());
    // asking again gives the same answer and still removes nothing
    RepairCheck again = plan_repair(v->fat);
    PB_CHECK(again.plan.has_value());
    RepairResult rr2 = repair(v->fat, *again.plan);
    PB_CHECK(!rr2.done);
    PB_CHECK(v->fat.read_file(placed, 100, &content));
    PB_CHECK_EQ(content, std::string("someone else's content"));
}

PB_TEST(a_committed_operation_whose_new_version_does_not_verify_is_undone_instead) {
    FakeReader reader = make_reader();
    Outcome ref = reference_run(reader);
    Found f = first_of_each_kind(reader, ref.total);
    // a committed state in which the old files are still in the journal
    std::unique_ptr<Volume> v;
    for (int n = f.finish; n <= ref.total; ++n) {
        v = cut_at(reader, n);
        RepairCheck rc = plan_repair(v->fat);
        if (rc.plan && rc.plan->action == RepairPlan::Action::Finish && v->fat.stat(std::string(kJournalDir) + "/new").exists()) break;
    }
    PB_CHECK(v != nullptr);
    RepairCheck rc = plan_repair(v->fat);
    PB_CHECK(rc.plan.has_value());
    // one of the new files is gone, so the new version does not verify
    const std::string lost = rc.plan->intent.items[0].path;
    PB_CHECK(v->fat.remove_file(lost));
    RepairResult rr = repair(v->fat, *rc.plan);
    PB_CHECK(rr.done);
    PB_CHECK(rr.outcome.find("undone") != std::string::npos);
    PB_CHECK(rr.outcome.find("did not verify") != std::string::npos);   // it says why
    PB_CHECK(compare(without_installer_area(ref.original), without_installer_area(snapshot(v->fat))).identical);
    PB_CHECK(!journal_exists(v->fat));
}

PB_TEST_MAIN()
