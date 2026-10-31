// T040: what the store says about its own version (research R-12): the version that runs (compiled in) against the one in
// the install record and what the launcher left. This is the only place where the list differs from the engine's states;
// it adds a note and never changes the state.
#include "pbstore/store/self_update_view.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
SelfUpdateInputs inputs(const char* running, const char* record) {
    SelfUpdateInputs i;
    i.running_version = running;
    i.record_version = record;
    return i;
}
}  // namespace

PB_TEST(running_and_recorded_version_equal_is_current_with_no_note) {
    SelfUpdateView v = self_update_view(inputs("0.2.0", "0.2.0"));
    PB_CHECK(v.kind == SelfUpdateKind::Current);
    PB_CHECK(v.list_note.empty());
}

PB_TEST(no_record_at_all_is_current) {
    SelfUpdateInputs i = inputs("0.2.0", "");
    PB_CHECK(self_update_view(i).kind == SelfUpdateKind::Current);
}

PB_TEST(a_newer_record_is_a_staged_update_that_takes_effect_at_the_next_opening) {
    SelfUpdateView v = self_update_view(inputs("0.2.0", "0.3.0"));
    PB_CHECK(v.kind == SelfUpdateKind::UpdateStaged);
    PB_CHECK(has(v.list_note, "update staged"));
    PB_CHECK(has(v.list_note, "open PB Store again"));
    PB_CHECK(has(v.about_text, "0.2.0"));                            // About says what runs now
    PB_CHECK(has(v.about_text, "0.3.0"));
}

PB_TEST(a_newer_record_with_the_failed_program_kept_by_the_launcher_is_a_rollback) {
    SelfUpdateInputs i = inputs("0.2.0", "0.3.0");
    i.failed_present = true;
    SelfUpdateView v = self_update_view(i);
    PB_CHECK(v.kind == SelfUpdateKind::RolledBack);
    PB_CHECK_EQ(v.failed_version, std::string("0.3.0"));
    PB_CHECK(has(v.list_note, "version 0.3.0 failed to start"));
    PB_CHECK(has(v.list_note, "version 0.2.0 is running"));
}

PB_TEST(the_launchers_message_also_makes_it_a_rollback_and_names_the_failed_version) {
    SelfUpdateInputs i = inputs("0.2.0", "0.3.0");
    i.message = RunMessage{RunMessage::Kind::UpdateFailed, "0.3.0"};
    SelfUpdateView v = self_update_view(i);
    PB_CHECK(v.kind == SelfUpdateKind::RolledBack);
    PB_CHECK_EQ(v.failed_version, std::string("0.3.0"));
}

PB_TEST(a_running_version_newer_than_the_record_is_ahead_because_someone_replaced_the_package) {
    SelfUpdateView v = self_update_view(inputs("0.3.0", "0.2.0"));
    PB_CHECK(v.kind == SelfUpdateKind::Ahead);
    PB_CHECK(!v.list_note.empty());
}

PB_TEST(a_pre_release_is_older_than_the_release_it_precedes) {
    PB_CHECK(self_update_view(inputs("0.3.0-rc1", "0.3.0")).kind == SelfUpdateKind::UpdateStaged);
    PB_CHECK(self_update_view(inputs("0.3.0", "0.3.0-rc1")).kind == SelfUpdateKind::Ahead);
}

PB_TEST(an_unparsable_version_is_treated_as_current_and_never_as_an_update) {
    PB_CHECK(self_update_view(inputs("0.2.0", "garbage")).kind == SelfUpdateKind::Current);
    PB_CHECK(self_update_view(inputs("", "0.2.0")).kind == SelfUpdateKind::Current);
}

PB_TEST(the_updated_notice_appears_once_when_the_last_run_version_is_older) {
    SelfUpdateInputs i = inputs("0.3.0", "0.3.0");
    i.last_run_version = "0.2.0";
    SelfUpdateView v = self_update_view(i);
    PB_CHECK(v.updated_to.has_value());
    PB_CHECK_EQ(*v.updated_to, std::string("0.3.0"));
    PB_CHECK(has(v.updated_text, "PB Store was updated to 0.3.0."));
}

PB_TEST(no_updated_notice_on_a_first_start_or_for_an_equal_or_newer_last_run_version) {
    SelfUpdateInputs first = inputs("0.3.0", "0.3.0");                                // nothing remembered: a first start
    PB_CHECK(!self_update_view(first).updated_to.has_value());
    SelfUpdateInputs same = inputs("0.3.0", "0.3.0");
    same.last_run_version = "0.3.0";
    PB_CHECK(!self_update_view(same).updated_to.has_value());
    SelfUpdateInputs newer = inputs("0.2.0", "0.3.0");                                // fell back: E-STO-06 speaks, not this
    newer.last_run_version = "0.3.0";
    newer.failed_present = true;
    PB_CHECK(!self_update_view(newer).updated_to.has_value());
    SelfUpdateInputs garbage = inputs("0.3.0", "0.3.0");
    garbage.last_run_version = "garbage";
    PB_CHECK(!self_update_view(garbage).updated_to.has_value());
}

PB_TEST_MAIN()
