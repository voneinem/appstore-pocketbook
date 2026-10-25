// T046 (spec SC-002, SC-005, SC-006): the fault matrix. For every operation kind added so far and for every
// mutating call n of the operation, power is lost at call n (once with the data that was written staying, once
// with only flushed data surviving), the reader reboots, the next run repairs, and the survivor must hold
// exactly the old or exactly the new state of the app files, a parseable desktop configuration equal to the old
// or the new text, no changed unrecorded file and an untouched data folder. For a sample of n the power is lost
// again inside the repair itself, at every step.
#include "install_fixtures.h"
#include "test_support.h"

#include <cstdio>

using namespace pbstore::install;
using namespace pbtest;

namespace {

struct Scenario {
    const char* name;
    FakeReader reader;
    std::function<Built(ReaderFs&)> make;
};

struct Counters {
    int fault_points = 0, repairs = 0, nested = 0;
};

void check_survivor(const char* kind, int n, bool pessimistic, int m, const Snapshot& old_live, const Snapshot& new_live, Volume& v) {
    const std::string where = std::string(kind) + ": power lost at call " + std::to_string(n) + (pessimistic ? " (only flushed data survives)" : "") +
                              (m ? ", and again at repair step " + std::to_string(m) : "");
    Snapshot live = without_installer_area(snapshot(v.fat));
    Diff to_old = compare(old_live, live);
    Diff to_new = compare(new_live, live);
    if (!to_old.identical && !to_new.identical)
        PB_FAIL_AT(where + ": the survivor is neither the old nor the new version.\n  against old: " + to_old.describe() + "  against new: " + to_new.describe());
    if (journal_exists(v.fat)) PB_FAIL_AT(where + ": a journal is left after the repair");
    std::string view = reader_view_text(v.fat);
    if (!parse_desktop_config(view).config) PB_FAIL_AT(where + ": view.json does not parse");
}

Counters run_kind(Scenario& k) {
    Counters c;
    // the reference runs: the old live area, the new live area, the number of mutating calls
    Snapshot old_live, new_live;
    int total = 0;
    {
        Volume v(k.reader);
        old_live = without_installer_area(snapshot(v.fat));
        Built b = k.make(v.fat);
        v.mem.reboot();
        PB_CHECK(run_execute(v.fat, b).ok());
        total = v.mem.mutating_calls();
        new_live = without_installer_area(snapshot(v.fat));
    }
    PB_CHECK(total > 20);
    for (int n = 1; n <= total; ++n) {
        for (int pessimistic = 0; pessimistic < 2; ++pessimistic) {
            Volume v(k.reader);
            Built b = k.make(v.fat);
            v.mem.lose_power_at(n, pessimistic == 1);
            bool lost = false;
            try { run_execute(v.fat, b); } catch (const PowerLoss&) { lost = true; }
            PB_CHECK(lost);
            ++c.fault_points;
            v.mem.reboot();
            // the next run: find the interrupted operation and repair it
            RepairCheck rc = plan_repair(v.fat);
            const Snapshot after_loss = without_installer_area(snapshot(v.fat));
            if (rc.plan) {
                RepairResult rr = repair(v.fat, *rc.plan);
                if (!rr.done) PB_FAIL_AT(std::string(k.name) + ": the repair did not finish after a loss at call " + std::to_string(n));
                ++c.repairs;
            } else if (!compare(old_live, after_loss).identical && !compare(new_live, after_loss).identical) {
                PB_FAIL_AT(std::string(k.name) + ": no journal and yet the reader is in a mixed state after a loss at call " + std::to_string(n));
            }
            check_survivor(k.name, n, pessimistic == 1, 0, old_live, new_live, v);
            // a second repair changes nothing
            const Snapshot settled = snapshot(v.fat);
            PB_CHECK(!plan_repair(v.fat).plan.has_value());
            PB_CHECK(compare(settled, snapshot(v.fat)).identical);

            // a sample: the power is lost again inside the repair, at every step
            if (rc.plan && (n % 4 == 1 || n == total)) {
                int repair_calls = 0;
                {
                    Volume w(k.reader);
                    Built bb = k.make(w.fat);
                    w.mem.lose_power_at(n, pessimistic == 1);
                    try { run_execute(w.fat, bb); } catch (const PowerLoss&) {}
                    w.mem.reboot();
                    RepairCheck r2 = plan_repair(w.fat);
                    repair(w.fat, *r2.plan);
                    repair_calls = w.mem.mutating_calls();
                }
                for (int m = 1; m <= repair_calls; ++m) {
                    Volume w(k.reader);
                    Built bb = k.make(w.fat);
                    w.mem.lose_power_at(n, pessimistic == 1);
                    try { run_execute(w.fat, bb); } catch (const PowerLoss&) {}
                    w.mem.reboot();
                    RepairCheck r2 = plan_repair(w.fat);
                    w.mem.lose_power_at(m, pessimistic == 1);
                    try { repair(w.fat, *r2.plan); } catch (const PowerLoss&) {}
                    w.mem.reboot();
                    RepairCheck r3 = plan_repair(w.fat);
                    if (r3.plan) {
                        RepairResult done = repair(w.fat, *r3.plan);
                        if (!done.done) PB_FAIL_AT(std::string(k.name) + ": the second repair did not finish (loss at " + std::to_string(n) + ", repair step " + std::to_string(m) + ")");
                    }
                    check_survivor(k.name, n, pessimistic == 1, m, old_live, new_live, w);
                    ++c.nested;
                    ++c.fault_points;
                }
            }
        }
    }
    return c;
}

}  // namespace

PB_TEST(install_survives_a_power_loss_at_every_call) {
    std::vector<Scenario> kinds;
    {
        Scenario k;
        k.name = "install";
        k.reader = FakeReader();
        k.reader.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
        k.make = [](ReaderFs& fs) { return make_install_plan(fs, nested_sample_app()); };
        kinds.push_back(std::move(k));
    }
    {   // the same with the plan of the real planner (spec: the matrix is re-run with the planner and the package pipeline)
        Scenario k;
        k.name = "install (real planner)";
        k.reader = FakeReader();
        k.reader.add_data_dir("testapp", {{"notes.db", "precious"}});
        k.make = [](ReaderFs& fs) {
            Built b = make_planned(fs, nested_sample_app());
            // the data folder exists and no installed app owns it: the plan asks for that confirmation, given here
            return b;
        };
        kinds.push_back(std::move(k));
    }
    {   // remove: the entry, the recorded files and the record; the data folder must stay
        Scenario k;
        k.name = "remove";
        FakeReader base;
        base.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
        k.reader = with_installed(base, nested_sample_app());
        k.make = [](ReaderFs& fs) { return make_planned(fs, nested_sample_app(), PlanKind::Remove); };
        kinds.push_back(std::move(k));
    }
    {   // update: 1.2.0 -> 1.3.0 with replaced, added and dropped files; the entry stays, the record is replaced
        Scenario k;
        k.name = "update";
        FakeReader base;
        base.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
        k.reader = with_installed(base, nested_sample_app());
        k.make = [](ReaderFs& fs) { return make_planned(fs, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"), PlanKind::Update); };
        kinds.push_back(std::move(k));
    }
    {   // update with a changed menu entry, so that the desktop configuration changes too
        Scenario k;
        k.name = "update (entry changes)";
        FakeReader base;
        base.add_data_dir("testapp", {{"notes.db", "precious"}});
        k.reader = with_installed(base, nested_sample_app());
        k.make = [](ReaderFs& fs) {
            SampleApp v2 = nested_sample_app_v2("testapp", "U_TestApp", "1.3.0");
            for (auto& kv : v2.manifest.object) if (kv.first == "menu_entry") kv.second.set("title", pbstore::json::Value::from_string("A New Title"));
            return make_planned(fs, v2, PlanKind::Update);
        };
        kinds.push_back(std::move(k));
    }
    {   // adopt: the record only
        Scenario k;
        k.name = "adopt";
        k.reader = FakeReader();
        k.reader.add_hand_installed(nested_sample_app());
        k.reader.add_data_dir("testapp", {{"notes.db", "precious"}});
        k.make = [](ReaderFs& fs) { return make_planned(fs, nested_sample_app(), PlanKind::Adopt); };
        kinds.push_back(std::move(k));
    }
    {   // replace an app installed outside the installer: one file differs, one is missing
        Scenario k;
        k.name = "replace";
        k.reader = FakeReader();
        k.reader.add_hand_installed(nested_sample_app("testapp", "1.0.0", "U_TestApp", 2));
        k.reader.add_data_dir("testapp", {{"notes.db", "precious"}});
        k.reader.files.erase("applications/icons/testapp_f.bmp");
        k.make = [](ReaderFs& fs) { return make_planned(fs, nested_sample_app(), PlanKind::Replace); };
        kinds.push_back(std::move(k));
    }
    for (Scenario& k : kinds) {
        Counters c = run_kind(k);
        std::printf("fault matrix, kind %s: %d fault points (%d repairs, %d of them inside a repair)\n", k.name, c.fault_points, c.repairs, c.nested);
        PB_CHECK(c.fault_points > 100);
    }
}

PB_TEST_MAIN()
