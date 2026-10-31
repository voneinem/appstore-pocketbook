// T101, T113 (spec SC-002, SC-005, SC-006): the fault matrix through the store's own service. For install, update, remove and adopt, and for
// every mutating call n of StoreService::execute_plan (power lost once with the written data staying, once with only flushed data
// surviving), the reader reboots, the next start of the store finds the operation, repair() finishes or undoes it, and the survivor holds
// exactly the old or exactly the new state of the live area, a view.json that parses, no journal and an untouched data folder. For a
// sample of n the power is lost again inside the repair at every step. The engine's own matrix is tests/integration/test_fault_matrix.cpp;
// this one proves that the store's front (gates, note, Wi-Fi session, start check) does not change what survives.
#include <cstdio>
#include <functional>

#include "install_fixtures.h"
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

struct Scenario {
    const char* name;
    FakeReader reader;
    RowAction action;
    bool offer_v2;
};

// One reader volume in memory behind FAT rules, opened by the store through its own storage configuration.
struct Session {
    StoreRig rig;
    Scenario scenario;
    std::unique_ptr<Volume> volume;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;

    explicit Session(const Scenario& s) : rig(s.reader), scenario(s) {}

    bool prepare() {
        if (!rig.ready()) return false;
        CatalogEntry& e = scenario.offer_v2 ? rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0")) : rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        if (scenario.offer_v2) e.listed_versions = {"1.2.0", "1.3.0"};
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        CancelToken c;
        if (!service->refresh(c, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        // from here on the reader is the in-memory volume
        rig.storage.open_fs = [this](const std::string&) -> std::unique_ptr<ReaderFs> { return std::make_unique<ForwardingFs>(volume->fat); };
        return true;
    }

    // A new start of the store on the current volume.
    StartResult start() {
        rig.make_engine();
        service = rig.make_service();
        return service->start_check();
    }
};

struct Counters {
    int fault_points = 0, repairs = 0, nested = 0;
};

// The operation of the scenario, up to the end of execute_plan; the power is lost at the cut-th write of execute_plan (0: never).
// Returns false if the power was lost. `writes` receives the number of mutating calls of execute_plan.
bool run_operation(Session& s, int cut = 0, bool pessimistic = false, int* writes = nullptr) {
    s.start();
    PlanView plan = s.service->plan_action(s.scenario.action, "testapp", s.cancel, nullptr);
    if (!plan.ok) throw std::runtime_error(std::string("fixture: no plan for ") + s.scenario.name);
    std::vector<std::string> given;   // the user taps every consent the plan names
    for (const ConfirmationItem& item : plan.confirmations) given.push_back(item.name);
    const int base = s.volume->mem.mutating_calls();
    s.volume->mem.lose_power_at(cut, pessimistic);
    if (writes) *writes = 0;
    try {
        ExecView out = s.service->execute_plan(given, false, s.cancel, nullptr);
        if (!out.ok) throw std::runtime_error(std::string("fixture: the operation failed: ") + (out.findings.empty() ? "" : out.findings[0].message));
        if (writes) *writes = s.volume->mem.mutating_calls() - base;
    } catch (const PowerLoss&) {
        return false;
    }
    return true;
}

void check_survivor(const std::string& where, const Snapshot& old_live, const Snapshot& new_live, Volume& v) {
    Snapshot live = without_installer_area(snapshot(v.fat));
    Diff to_old = compare(old_live, live);
    Diff to_new = compare(new_live, live);
    if (!to_old.identical && !to_new.identical)
        PB_FAIL_AT(where + ": the survivor is neither the old nor the new version.\n  against old: " + to_old.describe() + "  against new: " + to_new.describe());
    if (journal_exists(v.fat)) PB_FAIL_AT(where + ": a journal is left after the repair");
    if (!parse_desktop_config(reader_view_text(v.fat)).config) PB_FAIL_AT(where + ": view.json does not parse");
}

// The next start after the loss: the store finds the interrupted operation and repairs it. A loss inside the repair is followed by another start.
// Returns the number of mutating calls the repair made when the power did not fail.
int recover(Session& s, int repair_cut, bool pessimistic, bool* repaired) {
    s.volume->mem.reboot();
    StartResult start = s.start();
    int calls = 0;
    for (int round = 0; round < 3 && start.repair_pending; ++round) {
        *repaired = true;
        const int base = s.volume->mem.mutating_calls();
        s.volume->mem.lose_power_at(round == 0 ? repair_cut : 0, pessimistic);
        try {
            ExecView fixed = s.service->repair(false, s.cancel, nullptr);
            if (round == 0) calls = s.volume->mem.mutating_calls() - base;
            if (!fixed.ok) PB_FAIL_AT(std::string(s.scenario.name) + ": the repair did not finish: " + (fixed.findings.empty() ? "" : fixed.findings[0].message));
        } catch (const PowerLoss&) {
            s.volume->mem.reboot();
            start = s.start();
            continue;
        }
        start = s.start();   // nothing may be left
        break;
    }
    if (start.repair_pending) PB_FAIL_AT(std::string(s.scenario.name) + ": a repair is still pending after the repair");
    return calls;
}

Counters run_scenario(const Scenario& scenario) {
    Counters c;
    // the reference run: the old live area, the new live area, the number of mutating calls
    Snapshot old_live, new_live;
    int total = 0;
    {
        Session s(scenario);
        if (!s.prepare()) return c;
        s.volume = std::make_unique<Volume>(s.scenario.reader);
        old_live = without_installer_area(snapshot(s.volume->fat));
        PB_CHECK(run_operation(s, 0, false, &total));
        new_live = without_installer_area(snapshot(s.volume->fat));
    }
    PB_CHECK(total > 5);
    Session s(scenario);
    if (!s.prepare()) return c;
    for (int n = 1; n <= total; ++n) {
        for (int pessimistic = 0; pessimistic < 2; ++pessimistic) {
            const std::string where = std::string(scenario.name) + ": power lost at call " + std::to_string(n) + (pessimistic ? " (only flushed data survives)" : "");
            s.volume = std::make_unique<Volume>(s.scenario.reader);
            PB_CHECK(!run_operation(s, n, pessimistic == 1));
            ++c.fault_points;
            bool repaired = false;
            const Snapshot after_loss = (s.volume->mem.reboot(), without_installer_area(snapshot(s.volume->fat)));
            int repair_calls = recover(s, 0, pessimistic == 1, &repaired);
            if (repaired) ++c.repairs;
            else if (!compare(old_live, after_loss).identical && !compare(new_live, after_loss).identical)
                PB_FAIL_AT(where + ": no journal and yet the reader is in a mixed state.\n  against old: " + compare(old_live, after_loss).describe() + "  against new: " + compare(new_live, after_loss).describe());
            check_survivor(where, old_live, new_live, *s.volume);
            // a second start changes nothing
            const Snapshot settled = snapshot(s.volume->fat);
            PB_CHECK(!s.start().repair_pending);
            PB_CHECK(compare(settled, snapshot(s.volume->fat)).identical);

            // a sample: the power is lost again inside the repair, at every step
            if (repaired && (n % 4 == 1 || n == total)) {
                for (int m = 1; m <= repair_calls; ++m) {
                    s.volume = std::make_unique<Volume>(s.scenario.reader);
                    PB_CHECK(!run_operation(s, n, pessimistic == 1));
                    bool again = false;
                    recover(s, m, pessimistic == 1, &again);
                    check_survivor(where + ", and again at repair step " + std::to_string(m), old_live, new_live, *s.volume);
                    ++c.nested;
                    ++c.fault_points;
                }
            }
        }
    }
    return c;
}

}  // namespace

PB_TEST(a_power_loss_at_every_write_of_a_store_operation_leaves_the_old_or_the_new_complete_version) {
    std::vector<Scenario> kinds;
    {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
        kinds.push_back({"install", r, RowAction::Install, false});
    }
    {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}, {"sub/more.db", "also precious"}});
        kinds.push_back({"update", with_installed(r, nested_sample_app("testapp", "1.2.0")), RowAction::Update, true});
    }
    {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}});
        kinds.push_back({"remove", with_installed(r, nested_sample_app("testapp", "1.2.0")), RowAction::Remove, false});
    }
    {
        FakeReader r;
        r.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
        r.add_data_dir("testapp", {{"notes.db", "precious"}});
        kinds.push_back({"adopt", r, RowAction::Check, false});
    }
    int total = 0;
    for (const Scenario& k : kinds) {
        Counters c = run_scenario(k);
        if (c.fault_points == 0) PB_SKIP("openssl CLI not available");
        std::printf("store fault matrix, kind %s: %d fault points (%d repairs, %d of them inside a repair)\n", k.name, c.fault_points, c.repairs, c.nested);
        PB_CHECK(c.fault_points > 20);
        total += c.fault_points;
    }
    std::printf("store fault matrix: %d fault points in all\n", total);
}

PB_TEST_MAIN()
