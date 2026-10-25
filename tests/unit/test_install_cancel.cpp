// T048 (spec FR-037): cancelling before the commit point leaves the reader as it was, with E-INS-33; a cancel
// after it is held until the clean-up is done; progress is reported for every phase.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
bool has_rule(const ExecResult& r, const char* id) {
    for (const auto& f : r.findings)
        if (f.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(a_cancel_before_any_write_writes_nothing) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const Snapshot original = snapshot(v.fat);
    CancelToken cancel;
    cancel.cancel();
    ExecResult res = run_execute(v.fat, b, &cancel);
    PB_CHECK(!res.ok());
    PB_CHECK(res.cancelled);
    PB_CHECK(has_rule(res, "E-INS-33"));
    PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
    PB_CHECK(compare(original, snapshot(v.fat)).identical);     // not even the installer area
}

PB_TEST(a_cancel_at_any_progress_event_before_the_commit_point_undoes_everything) {
    FakeReader reader;
    int events = 0;
    {
        Volume v(reader);
        Built b = make_install_plan(v.fat, nested_sample_app());
        CancelToken c;
        PB_CHECK(run_execute(v.fat, b, &c, [&](const Progress&) { ++events; }).ok());
    }
    PB_CHECK(events > 6);
    Volume ref(reader);
    const Snapshot original = without_installer_area(snapshot(ref.fat));
    int cancelled_runs = 0;
    for (int k = 1; k <= events; ++k) {
        Volume v(reader);
        Built b = make_install_plan(v.fat, nested_sample_app());
        CancelToken cancel;
        int seen = 0;
        ExecResult res = run_execute(v.fat, b, &cancel, [&](const Progress&) {
            if (++seen == k) cancel.cancel();
        });
        if (res.ok()) {
            PB_CHECK(res.cancel_too_late);       // the cancel came after the commit point: held, finished
            continue;
        }
        ++cancelled_runs;
        PB_CHECK(res.cancelled);
        PB_CHECK(has_rule(res, "E-INS-33"));
        PB_CHECK(res.outcome == ExecResult::Outcome::Undone || res.outcome == ExecResult::Outcome::NotStarted);
        PB_CHECK(res.effect == ReaderEffect::Restored || res.effect == ReaderEffect::Unchanged);
        const Diff d = compare(original, without_installer_area(snapshot(v.fat)));
        if (!d.identical) PB_FAIL_AT("cancelled at event " + std::to_string(k) + " but the reader changed: " + d.describe());
        PB_CHECK(!journal_exists(v.fat));
    }
    PB_CHECK(cancelled_runs > 3);
}

PB_TEST(a_cancel_after_the_commit_point_is_held_until_the_clean_up_is_done) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    CancelToken cancel;
    ExecResult res = run_execute(v.fat, b, &cancel, [&](const Progress& p) {
        if (p.phase == "finishing") cancel.cancel();
    });
    PB_CHECK(res.ok());
    PB_CHECK(res.cancel_too_late);
    PB_CHECK(!res.cancelled);
    PB_CHECK(!journal_exists(v.fat));
    PB_CHECK_EQ(reader_view_text(v.fat), b.plan.view_new_text);
}

PB_TEST(progress_is_reported_for_preparing_applying_and_finishing) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    std::vector<Progress> seen;
    CancelToken c;
    PB_CHECK(run_execute(v.fat, b, &c, [&](const Progress& p) { seen.push_back(p); }).ok());
    std::vector<std::string> phases;
    for (const auto& p : seen) if (phases.empty() || phases.back() != p.phase) phases.push_back(p.phase);
    PB_CHECK_EQ(phases, (std::vector<std::string>{"preparing", "applying", "finishing"}));
    for (const auto& p : seen) {
        PB_CHECK(!p.text.empty());                                  // a phase without measurable progress says so in words
        if (p.total > 0) PB_CHECK(p.done <= p.total);
        if (p.total > 0) PB_CHECK(!p.unit.empty());
    }
    bool preparing_counts_files = false;
    for (const auto& p : seen) if (p.phase == "preparing" && p.unit == "files" && p.total >= b.plan.items.size()) preparing_counts_files = true;
    PB_CHECK(preparing_counts_files);
}

PB_TEST_MAIN()
