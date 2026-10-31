// T043: the launcher's small files and the store's last-run version (data model RunState, contracts/launcher.md,
// contracts/store-state-folder.md). Two state folders: the app folder and its run/ folder.
#include "mem_state_dirs.h"
#include "pbstore/store/run_state.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
struct Rig {
    pbtest::MemStateDirs app, run;
    RunState state{app, run};
};
}  // namespace

PB_TEST(trying_is_present_or_absent_and_mark_started_removes_only_it) {
    Rig r;
    PB_CHECK(!r.state.trying());
    r.run.write_atomic("trying", "0.3.0\n");
    r.run.write_atomic("last-exit", "0\n");
    r.run.write_atomic("message", "restored 0.2.0\n");
    r.app.write_atomic("last-run-version", "0.2.0\n");
    PB_CHECK(r.state.trying());
    r.state.mark_started();
    PB_CHECK(!r.state.trying());
    PB_CHECK(!r.run.exists("trying"));
    PB_CHECK(r.run.exists("last-exit"));                               // nothing else is touched
    PB_CHECK(r.run.exists("message"));
    PB_CHECK(r.app.exists("last-run-version"));
    r.state.mark_started();                                            // again: harmless
}

PB_TEST(last_exit_is_an_integer_of_at_most_8_bytes_or_absent) {
    Rig r;
    PB_CHECK(!r.state.last_exit().has_value());
    r.run.write_atomic("last-exit", "0\n");
    PB_CHECK(r.state.last_exit() == std::optional<int>(0));
    r.run.write_atomic("last-exit", "134\n");
    PB_CHECK(r.state.last_exit() == std::optional<int>(134));
    for (const char* bad : {"", "abc", "12 13", "-", "123456789", "1.5"}) {
        r.run.write_atomic("last-exit", bad);
        PB_CHECK(!r.state.last_exit().has_value());
    }
}

PB_TEST(a_failed_program_file_is_reported_by_its_presence_only) {
    Rig r;
    PB_CHECK(!r.state.failed_present());
    r.run.write_atomic("pbstore.bin.failed", "ELF");
    PB_CHECK(r.state.failed_present());
}

PB_TEST(take_message_returns_the_text_once_and_deletes_the_file) {
    Rig r;
    PB_CHECK(r.state.take_message().kind == RunMessage::Kind::None);
    r.run.write_atomic("message", "update-failed 0.3.0\n");
    RunMessage m = r.state.take_message();
    PB_CHECK(m.kind == RunMessage::Kind::UpdateFailed);
    PB_CHECK_EQ(m.detail, std::string("0.3.0"));
    PB_CHECK(!r.run.exists("message"));
    PB_CHECK(r.state.take_message().kind == RunMessage::Kind::None);
}

PB_TEST(the_three_known_messages_and_a_generic_notice_for_unknown_ones) {
    Rig r;
    r.run.write_atomic("message", "restored 0.2.0\n");
    RunMessage restored = r.state.take_message();
    PB_CHECK(restored.kind == RunMessage::Kind::Restored);
    PB_CHECK_EQ(restored.detail, std::string("0.2.0"));
    r.run.write_atomic("message", "swap-skipped no-tool\n");
    RunMessage skipped = r.state.take_message();
    PB_CHECK(skipped.kind == RunMessage::Kind::SwapSkipped);
    PB_CHECK_EQ(skipped.detail, std::string("no-tool"));
    r.run.write_atomic("message", "something-new with detail\n");
    RunMessage unknown = r.state.take_message();
    PB_CHECK(unknown.kind == RunMessage::Kind::Unknown);              // shown as a generic notice
    PB_CHECK(!r.run.exists("message"));
}

PB_TEST(an_oversized_message_is_treated_as_absent_and_removed) {
    Rig r;
    r.run.write_atomic("message", std::string(129, 'x'));
    PB_CHECK(r.state.take_message().kind == RunMessage::Kind::None);
    PB_CHECK(!r.run.exists("message"));
}

PB_TEST(the_last_run_version_is_one_semantic_version_line_of_at_most_32_bytes) {
    Rig r;
    PB_CHECK(!r.state.last_run_version().has_value());
    PB_CHECK(r.state.remember_run_version("0.2.0"));
    PB_CHECK_EQ(r.app.text("last-run-version"), std::string("0.2.0\n"));
    PB_CHECK(r.state.last_run_version() == std::optional<std::string>("0.2.0"));
    for (const std::string& bad : {std::string("garbage"), std::string("1.2"), std::string(40, '1'), std::string("")}) {
        r.app.write_atomic("last-run-version", bad);
        PB_CHECK(!r.state.last_run_version().has_value());
    }
    PB_CHECK(!r.state.remember_run_version("not a version"));          // never writes something it would not read back
    PB_CHECK(!r.state.remember_run_version(std::string(40, '1')));
}

PB_TEST(the_class_writes_no_other_name_than_last_run_version_and_removes_no_other_than_trying_and_message) {
    Rig r;
    r.run.write_atomic("trying", "x");
    r.run.write_atomic("message", "restored 1.0.0\n");
    r.run.write_atomic("last-exit", "0\n");
    r.run.write_atomic("pbstore.bin", "ELF");
    r.run.write_atomic("unknown", "mine");
    const auto run_before = r.run.names();
    r.state.mark_started();
    r.state.take_message();
    r.state.remember_run_version("1.0.0");
    r.state.last_exit();
    r.state.failed_present();
    std::vector<std::string> expected;
    for (const auto& n : run_before) if (n != "trying" && n != "message") expected.push_back(n);
    PB_CHECK((r.run.names() == expected));
    PB_CHECK((r.app.names() == std::vector<std::string>{"last-run-version"}));
}

PB_TEST(the_wifi_marker_is_written_and_removed_and_nothing_else_is_touched) {
    Rig r;
    r.run.write_atomic("last-exit", "0\n");
    PB_CHECK(!r.state.radio_marker());
    r.state.set_radio_marker(true);
    PB_CHECK(r.state.radio_marker());
    PB_CHECK(r.run.exists("wifi-on"));
    r.state.set_radio_marker(true);                                    // again: harmless
    r.state.set_radio_marker(false);
    PB_CHECK(!r.state.radio_marker());
    PB_CHECK(!r.run.exists("wifi-on"));
    PB_CHECK(r.run.exists("last-exit"));
}

PB_TEST_MAIN()
