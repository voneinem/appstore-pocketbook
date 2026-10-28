// The start of the store (spec FR-025, FR-028, FR-034, story 7): what the start check says about a cancelled operation and about what the
// launcher left, and the moment the start counts as successful (the marker is removed and the version remembered).
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {
bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(a_note_without_a_journal_is_reported_once_as_cancelled_nothing_changed) {
    StoreRig rig;
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    rig.pending_note->write({"install", "testapp", "", "1.2.0"});
    StartResult r = service->start_check();
    PB_CHECK(has_rule(r.notices, "W-STO-01"));
    PB_CHECK(!r.repair_pending);
    PB_CHECK(!has_rule(service->start_check().notices, "W-STO-01"));
}

PB_TEST(what_the_launcher_left_is_shown_once_as_e_sto_06_or_e_sto_07) {
    StoreRig rig;
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    rig.run_dirs->write_atomic("message", "update-failed 0.3.0\n");
    StartResult failed = service->start_check();
    PB_CHECK(has_rule(failed.notices, "E-STO-06"));
    PB_CHECK(rig.run_dirs->size("message") == UINT64_MAX);
    rig.run_dirs->write_atomic("message", "swap-skipped no-space\n");
    StartResult skipped = service->start_check();
    PB_CHECK(has_rule(skipped.notices, "E-STO-07"));
    PB_CHECK(!has_rule(service->start_check().notices, "E-STO-07"));
}

PB_TEST(mark_started_removes_the_marker_and_remembers_the_version_and_nothing_else) {
    StoreRig rig;
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    rig.run_dirs->write_atomic("trying", "swap\n");
    rig.run_dirs->write_atomic("last-exit", "0\n");
    service->mark_started("0.1.0");
    PB_CHECK(rig.run_dirs->size("trying") == UINT64_MAX);
    PB_CHECK(rig.run_dirs->size("last-exit") != UINT64_MAX);
    PB_CHECK(rig.run_state->last_run_version() == std::optional<std::string>("0.1.0"));
}

PB_TEST_MAIN()
