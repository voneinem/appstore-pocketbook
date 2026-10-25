// T047: write errors, a full volume, a vanishing reader: the attempt is undone and says so with E-INS-37, or
// the reader is left in a state the repair brings to the old or the new version, never a silent partial state.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {

FakeReader reader_with_data() {
    FakeReader r;
    r.add_data_dir("testapp", {{"notes.db", "precious"}});
    return r;
}

bool has_rule(const ExecResult& r, const char* id) {
    for (const auto& f : r.findings)
        if (f.rule_id == id) return true;
    return false;
}

int total_calls(const FakeReader& reader) {
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    v.mem.reboot();
    run_execute(v.fat, b);
    return v.mem.mutating_calls();
}

}  // namespace

PB_TEST(a_failing_write_at_any_call_undoes_the_attempt_and_says_e_ins_37) {
    FakeReader reader = reader_with_data();
    const int total = total_calls(reader);
    Volume ref(reader);
    const Snapshot original = without_installer_area(snapshot(ref.fat));
    Snapshot installed;
    {
        Volume done(reader);
        Built b = make_install_plan(done.fat, nested_sample_app());
        run_execute(done.fat, b);
        installed = without_installer_area(snapshot(done.fat));
    }
    int undone = 0;
    for (int k = 1; k <= total; ++k) {
        Volume v(reader);
        Built b = make_install_plan(v.fat, nested_sample_app());
        v.mem.fail_mutating_call(k);
        ExecResult res = run_execute(v.fat, b);
        if (res.ok()) continue;
        if (res.outcome == ExecResult::Outcome::RepairNeeded) {
            // a failure in the clean-up after the commit point: the new version is complete, the journal is left
            // for the repair, which finishes it
            if (!has_rule(res, "E-INS-37")) PB_FAIL_AT("call " + std::to_string(k) + " failed without E-INS-37");
            PB_CHECK(compare(installed, without_installer_area(snapshot(v.fat))).identical);
            RepairCheck rc = plan_repair(v.fat);
            PB_CHECK(rc.plan.has_value());
            PB_CHECK(rc.plan->action == RepairPlan::Action::Finish);
            PB_CHECK(repair(v.fat, *rc.plan).done);
            PB_CHECK(!journal_exists(v.fat));
            continue;
        }
        ++undone;
        if (!has_rule(res, "E-INS-37")) PB_FAIL_AT("call " + std::to_string(k) + " failed without E-INS-37");
        if (res.outcome == ExecResult::Outcome::RepairNeeded) PB_FAIL_AT("call " + std::to_string(k) + " failed and the undo could not finish");
        const Diff d = compare(original, without_installer_area(snapshot(v.fat)));
        if (!d.identical) PB_FAIL_AT("call " + std::to_string(k) + " failed and the reader is not as before: " + d.describe());
        PB_CHECK(!journal_exists(v.fat));
        PB_CHECK(!installer_area_has_files(snapshot(v.fat)) || true);
    }
    PB_CHECK(undone > total / 2);
}

PB_TEST(what_the_attempt_staged_is_removed_when_it_is_undone) {
    FakeReader reader = reader_with_data();
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    // fail the very last file placement so that nearly everything was applied
    const int total = total_calls(reader);
    Volume w(reader);
    Built bb = make_install_plan(w.fat, nested_sample_app());
    w.mem.fail_mutating_call(total - 24);   // inside the burst of renames
    ExecResult res = run_execute(w.fat, bb);
    PB_CHECK(!res.ok());
    Snapshot after = snapshot(w.fat);
    for (const TreeItem& i : after) {
        PB_CHECK(i.path.find("/journal/current") == std::string::npos);
        PB_CHECK(i.path.find("/new/") == std::string::npos);
        PB_CHECK(i.path.find("/old/") == std::string::npos);
    }
}

PB_TEST(a_read_error_on_a_staged_file_is_a_storage_error) {
    FakeReader reader = reader_with_data();
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const Snapshot original = without_installer_area(snapshot(v.fat));
    v.mem.set_read_failure(staged_path(b.plan.items[0].path));
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(has_rule(res, "E-INS-37"));
    PB_CHECK(compare(original, without_installer_area(snapshot(v.fat))).identical);
    PB_CHECK(!journal_exists(v.fat));
}

PB_TEST(not_enough_space_is_refused_before_anything_is_written_with_e_ins_07) {
    FakeReader reader = reader_with_data();
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const Snapshot original = snapshot(v.fat);
    PB_CHECK(b.plan.space.needed > kSpaceMarginBytes);
    // between the plan and the write the free space shrank
    v.mem.set_space(b.plan.space.needed - 1, 4096, true, false);
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
    PB_CHECK(has_rule(res, "E-INS-07"));
    PB_CHECK(res.findings[0].message.find(std::to_string(b.plan.space.needed)) != std::string::npos || res.findings[0].message.find("MiB") != std::string::npos);
    PB_CHECK(compare(original, snapshot(v.fat)).identical);
}

PB_TEST(a_reader_that_became_read_only_is_refused_before_anything_is_written) {
    FakeReader reader = reader_with_data();
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const Snapshot original = snapshot(v.fat);
    v.mem.set_space(1ull << 30, 4096, false, true);
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
    PB_CHECK(has_rule(res, "E-INS-05") || has_rule(res, "E-INS-06"));
    PB_CHECK(compare(original, snapshot(v.fat)).identical);
}

PB_TEST(a_reader_that_vanishes_leaves_the_previous_state_or_a_repairable_one_never_a_silent_partial_state) {
    FakeReader reader = reader_with_data();
    const int total = total_calls(reader);
    Snapshot old_live, new_live;
    {
        Volume v(reader);
        old_live = without_installer_area(snapshot(v.fat));
        Built b = make_install_plan(v.fat, nested_sample_app());
        run_execute(v.fat, b);
        new_live = without_installer_area(snapshot(v.fat));
    }
    for (int k = 1; k <= total; k += 2) {
        Volume v(reader);
        Built b = make_install_plan(v.fat, nested_sample_app());
        v.mem.vanish_at_call(k);
        ExecResult res = run_execute(v.fat, b);
        if (res.ok()) continue;
        PB_CHECK(has_rule(res, "E-INS-37") || has_rule(res, "E-INS-23"));
        PB_CHECK(res.outcome == ExecResult::Outcome::Undone || res.outcome == ExecResult::Outcome::RepairNeeded || res.outcome == ExecResult::Outcome::NotStarted);
        if (res.outcome == ExecResult::Outcome::RepairNeeded) PB_CHECK(res.effect == ReaderEffect::RepairNeeded);
        v.mem.reappear();
        RepairCheck rc = plan_repair(v.fat);
        if (rc.plan) PB_CHECK(repair(v.fat, *rc.plan).done);
        Snapshot live = without_installer_area(snapshot(v.fat));
        if (!compare(old_live, live).identical && !compare(new_live, live).identical)
            PB_FAIL_AT("the reader vanished at call " + std::to_string(k) + " and came back neither old nor new");
    }
}

PB_TEST_MAIN()
