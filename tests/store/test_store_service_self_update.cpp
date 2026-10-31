// T127 (story 7): what the store says about its own version when the launcher left a message. Staging and the "takes effect when you open PB Store
// again" text are in test_store_service_actions, the view's own cases in test_self_update_view; here the service is shown not to offer a version
// again that has just failed to start, to show a refused swap once, and to say when the running program is newer than its record.
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;

namespace {

SampleApp store_app(const std::string& version, unsigned seed = 1) { return make_sample_app("pbstore", version, "U_PbStore", "", seed); }

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

bool has_rule(const std::vector<Finding>& f, const char* id) {
    for (const auto& x : f) if (x.rule_id == id) return true;
    return false;
}

const AppRow* find(const std::vector<AppRow>& rows, const std::string& id) {
    for (const AppRow& r : rows) if (r.id == id) return &r;
    return nullptr;
}

bool offers(const AppRow& row, RowAction a) { return std::find(row.actions.begin(), row.actions.end(), a) != row.actions.end(); }

bool has_note(const AppRow& row, const std::string& part) {
    for (const auto& n : row.notes) if (has(n, part)) return true;
    return false;
}

// The reader holds `recorded` as PB Store's version, the catalog offers `offered`; the store itself runs as 0.1.0 (the rig's default).
struct Fixture {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    Fixture(const std::string& recorded, const std::string& offered) : rig(with_installed(FakeReader(), store_app(recorded))) {
        if (!rig.ready()) return;
        CatalogEntry& e = rig.host.add_app(store_app(offered, 77));
        if (recorded != offered) e.listed_versions = {recorded, offered};
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
    }
    std::vector<AppRow> refreshed() {
        CancelToken c;
        if (!service->refresh(c, nullptr).ok) throw std::runtime_error("fixture: refresh failed");
        return service->rows("");
    }
};

}  // namespace

PB_TEST(a_version_that_failed_to_start_is_not_offered_again_but_a_different_one_is) {
    Fixture same("0.2.0", "0.2.0");   // the record already says the failed version, the catalog offers just that one
    if (!same.rig.ready()) PB_SKIP("openssl CLI not available");
    same.rig.run_dirs->write_atomic("message", "update-failed 0.2.0\n");
    same.rig.run_dirs->write_atomic("pbstore.bin.failed", "ELF");
    StartResult start = same.service->start_check();
    PB_CHECK(has_rule(start.notices, "E-STO-06"));
    std::vector<AppRow> rows = same.refreshed();
    const AppRow* row = find(rows, "pbstore");
    PB_CHECK(row != nullptr);
    PB_CHECK(row->state != RowState::UpdateAvailable);
    PB_CHECK(!offers(*row, RowAction::Update));
    PB_CHECK(has_note(*row, "version 0.2.0 failed to start; version 0.1.0 is running"));
    PB_CHECK(same.service->update_all_queue().empty());                        // Update all does not try it either

    Fixture other("0.2.0", "0.3.0");  // the catalog has moved on: that version has not failed
    other.rig.run_dirs->write_atomic("message", "update-failed 0.2.0\n");
    other.rig.run_dirs->write_atomic("pbstore.bin.failed", "ELF");
    other.service->start_check();
    rows = other.refreshed();
    row = find(rows, "pbstore");
    PB_CHECK(row != nullptr);
    PB_CHECK(row->state == RowState::UpdateAvailable);
    PB_CHECK(offers(*row, RowAction::Update));
    PB_CHECK(has_note(*row, "version 0.2.0 failed to start"));
}

PB_TEST(a_refused_swap_is_shown_once_with_its_rule_and_the_old_version_keeps_running) {
    Fixture f("0.0.1", "0.2.0");
    if (!f.rig.ready()) PB_SKIP("openssl CLI not available");
    f.rig.run_dirs->write_atomic("message", "swap-skipped no space left\n");
    StartResult first = f.service->start_check();
    PB_CHECK(has_rule(first.notices, "E-STO-07"));
    StartResult second = f.service->start_check();                              // the message was taken: nothing again
    PB_CHECK(!has_rule(second.notices, "E-STO-07"));
    PB_CHECK(!has_rule(second.notices, "E-STO-06"));
}

PB_TEST(a_running_version_newer_than_the_record_is_said_in_the_list) {
    Fixture f("0.0.1", "0.0.1");      // the store runs as 0.1.0: someone replaced the package by hand
    if (!f.rig.ready()) PB_SKIP("openssl CLI not available");
    f.service->start_check();
    std::vector<AppRow> rows = f.refreshed();
    const AppRow* row = find(rows, "pbstore");
    PB_CHECK(row != nullptr);
    PB_CHECK(has_note(*row, "newer than its install record"));
    PB_CHECK(!has_note(*row, "failed to start"));
}

PB_TEST(the_first_screen_marks_the_start_and_the_next_start_says_the_version_was_updated_once) {
    Fixture f("0.1.0", "0.1.0");
    if (!f.rig.ready()) PB_SKIP("openssl CLI not available");
    f.rig.run_dirs->write_atomic("trying", "1\n");
    f.rig.storage.state->write_atomic("last-run-version", "0.0.9\n");
    PB_CHECK(f.rig.run_state->trying());
    f.service->start_check();
    const auto before = f.rig.run_state->last_run_version();
    PB_CHECK(before && *before == "0.0.9");
    f.service->mark_started("0.1.0");                                           // the first screen is up
    PB_CHECK(!f.rig.run_state->trying());
    const auto after = f.rig.run_state->last_run_version();
    PB_CHECK(after && *after == "0.1.0");
    PB_CHECK(!f.rig.run_state->failed_present());                               // nothing else was touched
}

PB_TEST_MAIN()
