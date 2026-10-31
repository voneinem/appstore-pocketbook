// T102 (spec FR-025, SC-013): the user leaves the store - home key, sleep, cable, the reader ended the app. Every reason has the same effect,
// and the tests run every case for every reason. For every step of a download, an install, an update, a removal, an adoption and a repair
// (the leave comes at the k-th progress report of the operation, for every k): before the commit point the operation is undone and the
// reader is as before, after it the operation finishes first; there is never a mixed state; the note of an undone operation stays so that the
// next start says what happened (W-STO-01); Wi-Fi is back in its earlier state; and after the leave request the store starts nothing.
#include <fstream>

#include "store_rig.h"
#include "test_support.h"
#include "throwing_fs.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}

// the reader's files without the store's own state and the engine's folders that differ between runs
Snapshot apps_only(const Snapshot& s) {
    Snapshot out;
    for (const auto& i : s) {
        if (i.path.compare(0, 18, "system/pbstore/app") == 0) continue;
        if (i.path == "system/pbstore/lock" || i.path == "system/pbstore/journal" || i.path == "system/pbstore/installed") continue;
        if (i.path.compare(0, 22, "system/pbstore/backups") == 0) continue;
        if (i.path.compare(0, 25, "system/pbstore/installed/") == 0) continue;
        out.push_back(i);
    }
    return out;
}

const LeaveReason kReasons[] = {LeaveReason::Home, LeaveReason::Sleep, LeaveReason::Usb, LeaveReason::ReaderEnded};
const char* reason_name(LeaveReason r) {
    switch (r) {
        case LeaveReason::Home: return "home";
        case LeaveReason::Sleep: return "sleep";
        case LeaveReason::Usb: return "usb";
        case LeaveReason::ReaderEnded: return "reader ended";
    }
    return "?";
}

struct LeaveCase {
    const char* name;
    FakeReader reader;
    RowAction action;
    bool offer_v2;
};

struct Rig {
    StoreRig rig;
    LeaveCase c;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    explicit Rig(const LeaveCase& k) : rig(k.reader), c(k) {}
    bool prepare() {
        if (!rig.ready()) return false;
        if (c.offer_v2) {
            CatalogEntry& e = rig.host.add_app(nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else {
            rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        }
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        CancelToken first;
        if (!service->refresh(first, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        rig.radio = FakeRadio();
        return true;
    }
    void fresh_start() {
        rig.make_engine();
        service = rig.make_service();
    }
    static std::vector<std::string> names(const PlanView& p) {
        std::vector<std::string> v;
        for (const ConfirmationItem& i : p.confirmations) v.push_back(i.name);
        return v;
    }
};

// Counts the progress reports of a function, or leaves at the k-th.
struct Events {
    int count = 0;
    int leave_at = 0;                 // 0: never
    StoreService* service = nullptr;
    LeaveReason reason = LeaveReason::Home;
    ProgressFn fn() {
        return [this](const Progress&) {
            ++count;
            if (leave_at > 0 && count == leave_at && service) service->request_leave(reason);
        };
    }
};

void wifi_is_back(Rig& r, const std::string& where) {
    if (r.rig.radio.is_on() != std::optional<bool>(false)) PB_FAIL_AT(where + ": Wi-Fi was not switched off again");
}

void nothing_starts_after_a_leave(Rig& r, const std::string& where) {
    PB_CHECK(r.service->leave_requested());
    const Snapshot before = snapshot(r.rig.root());
    const size_t radio_calls = r.rig.radio.calls().size();
    CancelToken c;
    PlanView plan = r.service->plan_action(RowAction::Install, "testapp", c, nullptr);
    if (plan.ok || !plan.findings.empty()) PB_FAIL_AT(where + ": a plan was started after the leave request");
    ExecView exec = r.service->execute_plan({}, false, c, nullptr);
    if (exec.ok || !exec.findings.empty()) PB_FAIL_AT(where + ": an execution was started after the leave request");
    StoreOutcome refreshed = r.service->refresh(c, nullptr);
    if (refreshed.ok || !refreshed.findings.empty()) PB_FAIL_AT(where + ": a refresh was started after the leave request");
    ExecView repaired = r.service->repair(false, c, nullptr);
    if (repaired.ok || !repaired.findings.empty()) PB_FAIL_AT(where + ": a repair was started after the leave request");
    if (r.rig.radio.calls().size() != radio_calls) PB_FAIL_AT(where + ": the Wi-Fi was touched after the leave request");
    if (!compare(before, snapshot(r.rig.root())).identical) PB_FAIL_AT(where + ": the reader changed after the leave request");
}

}  // namespace

PB_TEST(a_leave_during_the_download_stops_it_writes_nothing_and_restores_wifi_for_every_reason) {
    std::vector<LeaveCase> cases;
    cases.push_back({"install", FakeReader(), RowAction::Install, false});
    cases.push_back({"update", with_installed(FakeReader(), nested_sample_app("testapp", "1.2.0")), RowAction::Update, true});
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    cases.push_back({"adopt", hand, RowAction::Check, false});
    int steps = 0;
    for (const LeaveCase& k : cases) {
        int total = 0;
        {
            Rig ref(k);
            if (!ref.prepare()) PB_SKIP("openssl CLI not available");
            Events e;
            PB_CHECK(ref.service->plan_action(k.action, "testapp", ref.cancel, e.fn()).ok);
            total = e.count;
        }
        PB_CHECK(total > 0);
        for (LeaveReason reason : kReasons)
            for (int at = 1; at <= total; ++at) {
                const std::string where = std::string(k.name) + ", " + reason_name(reason) + ", at report " + std::to_string(at) + " of the plan";
                Rig r(k);
                r.prepare();
                const Snapshot before = snapshot(r.rig.root());
                Events e;
                e.leave_at = at;
                e.service = r.service.get();
                e.reason = reason;
                r.service->plan_action(k.action, "testapp", r.cancel, e.fn());
                if (e.count < at) continue;   // the download had fewer reports this time (the chunks differ): the leave did not come
                PB_CHECK(r.service->leave_requested());
                PB_CHECK(compare(apps_only(before), apps_only(snapshot(r.rig.root()))).identical);   // nothing written
                wifi_is_back(r, where);
                nothing_starts_after_a_leave(r, where);
                ++steps;
            }
    }
    PB_CHECK(steps > 6);
    std::printf("leave during a download: %d cases\n", steps);
}

PB_TEST(a_leave_during_the_write_undoes_it_before_the_commit_point_and_finishes_it_after_for_every_reason) {
    std::vector<LeaveCase> cases;
    {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}});
        cases.push_back({"install", r, RowAction::Install, false});
    }
    {
        FakeReader r;
        r.add_data_dir("testapp", {{"notes.db", "precious"}});
        cases.push_back({"update", with_installed(r, nested_sample_app("testapp", "1.2.0")), RowAction::Update, true});
        cases.push_back({"remove", with_installed(r, nested_sample_app("testapp", "1.2.0")), RowAction::Remove, false});
    }
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    cases.push_back({"adopt", hand, RowAction::Check, false});
    int steps = 0, undone = 0, finished = 0;
    for (const LeaveCase& k : cases) {
        Snapshot old_state, new_state;
        int total = 0;
        {
            Rig ref(k);
            if (!ref.prepare()) PB_SKIP("openssl CLI not available");
            old_state = apps_only(snapshot(ref.rig.root()));
            PlanView p = ref.service->plan_action(k.action, "testapp", ref.cancel, nullptr);
            PB_CHECK(p.ok);
            Events e;
            PB_CHECK(ref.service->execute_plan(Rig::names(p), false, ref.cancel, e.fn()).ok);
            total = e.count;
            new_state = apps_only(snapshot(ref.rig.root()));
        }
        PB_CHECK(total > 1);
        for (LeaveReason reason : kReasons)
            for (int at = 1; at <= total; ++at) {
                const std::string where = std::string(k.name) + ", " + reason_name(reason) + ", at report " + std::to_string(at) + " of the write";
                Rig r(k);
                r.prepare();
                PlanView p = r.service->plan_action(k.action, "testapp", r.cancel, nullptr);
                PB_CHECK(p.ok);
                Events e;
                e.leave_at = at;
                e.service = r.service.get();
                e.reason = reason;
                ExecView out = r.service->execute_plan(Rig::names(p), false, r.cancel, e.fn());
                PB_CHECK(r.service->leave_requested());
                const Snapshot now = apps_only(snapshot(r.rig.root()));
                const bool is_old = compare(old_state, now).identical, is_new = compare(new_state, now).identical;
                if (out.ok) {
                    if (!is_new) PB_FAIL_AT(where + ": the write finished but the reader does not hold the new complete state");
                    PB_CHECK(r.rig.storage.state->size("pending-note.txt") == UINT64_MAX);   // a finished action leaves no note behind
                    ++finished;
                } else {
                    if (!out.cancelled) PB_FAIL_AT(where + ": neither finished nor cancelled: " + (out.findings.empty() ? "" : out.findings[0].message));
                    if (!is_old) PB_FAIL_AT(where + ": cancelled but the reader is not as before");
                    PB_CHECK(r.rig.storage.state->size("pending-note.txt") != UINT64_MAX);   // kept, so that the next start can say what happened
                    ++undone;
                }
                wifi_is_back(r, where);
                nothing_starts_after_a_leave(r, where);
                // the next start
                r.fresh_start();
                StartResult next = r.service->start_check();
                if (!out.ok) {
                    PB_CHECK(!next.repair_pending);
                    PB_CHECK(has_rule(next.notices, "W-STO-01"));
                } else {
                    PB_CHECK(!has_rule(next.notices, "W-STO-01"));   // it was not cancelled: it finished
                }
                ++steps;
            }
    }
    PB_CHECK(undone > 0 && finished > 0);   // the cases really cover both sides of the commit point
    std::printf("leave during the write: %d cases (%d undone, %d finished)\n", steps, undone, finished);
}

PB_TEST(a_leave_during_a_repair_ends_with_the_old_or_the_new_complete_version_for_every_reason) {
    LeaveCase k{"repair", FakeReader(), RowAction::Install, false};
    Snapshot old_state, new_state;
    {
        Rig ref(k);
        if (!ref.prepare()) PB_SKIP("openssl CLI not available");
        old_state = apps_only(snapshot(ref.rig.root()));
        PlanView p = ref.service->plan_action(RowAction::Install, "testapp", ref.cancel, nullptr);
        PB_CHECK(ref.service->execute_plan(Rig::names(p), false, ref.cancel, nullptr).ok);
        new_state = apps_only(snapshot(ref.rig.root()));
    }
    int steps = 0;
    for (LeaveReason reason : kReasons) {
        // a repair has an unknown number of reports; leave at the 1st, 2nd, ... until the repair reports fewer
        for (int at = 0; at <= 40; ++at) {   // 0: the leave comes before the repair is called
            const std::string where = std::string("repair, ") + reason_name(reason) + ", at report " + std::to_string(at);
            Rig r(k);
            r.prepare();
            // an install cut by a power loss leaves the journal
            const ReaderFsFactory normal = r.rig.storage.open_fs;
            r.rig.storage.open_fs = [normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), 24); };
            r.fresh_start();
            r.service->start_check();
            PlanView p = r.service->plan_action(RowAction::Install, "testapp", r.cancel, nullptr);
            bool lost = false;
            try { r.service->execute_plan(Rig::names(p), false, r.cancel, nullptr); } catch (const PowerLoss&) { lost = true; }
            PB_CHECK(lost);
            r.rig.storage.open_fs = normal;
            r.fresh_start();
            PB_CHECK(r.service->start_check().repair_pending);
            const Snapshot wifi_before = snapshot(r.rig.root());
            (void)wifi_before;
            Events e;
            e.leave_at = at;
            e.service = r.service.get();
            e.reason = reason;
            CancelToken c;
            if (at == 0) r.service->request_leave(reason);
            ExecView out = r.service->repair(false, c, e.fn());
            if (at == 0) PB_CHECK(!out.ok && out.findings.empty());      // nothing is started after the leave request
            if (at > 0 && e.count < at && !r.service->leave_requested()) break;   // the repair has fewer reports than `at`: every step was tried
            // whatever happened: the next start finishes the job if one is left, and the result is the old or the new complete version
            r.rig.radio = FakeRadio();
            r.fresh_start();
            if (r.service->start_check().repair_pending) PB_CHECK(r.service->repair(false, c, nullptr).ok);
            const Snapshot now = apps_only(snapshot(r.rig.root()));
            if (!compare(old_state, now).identical && !compare(new_state, now).identical) PB_FAIL_AT(where + ": neither the old nor the new complete version");
            PB_CHECK(r.rig.radio.calls().empty());   // a repair never touches Wi-Fi
            (void)out;
            ++steps;
        }
    }
    PB_CHECK(steps > 4);
    std::printf("leave during a repair: %d cases\n", steps);
}

PB_TEST_MAIN()
