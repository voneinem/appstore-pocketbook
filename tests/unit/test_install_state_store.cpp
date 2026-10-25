// T020: the serial memory (rollback protection) kept per user on the computer.
#include "mem_state_dirs.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore/install/state_store.h"
#include "pbstore/json.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore::install;

PB_TEST(a_first_run_has_no_earlier_serial) {
    pbtest::MemStateDirs dirs;
    StateStore s(dirs);
    PB_CHECK(!s.last_serial().has_value());
    PB_CHECK(s.fresh_start());
}

PB_TEST(a_recorded_serial_is_read_back_in_the_documented_format) {
    pbtest::MemStateDirs dirs;
    {
        StateStore s(dirs);
        PB_CHECK(s.record(42));
    }
    std::string text;
    PB_CHECK(dirs.read("state.json", &text));
    auto v = pbstore::json::parse(text);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(v->get_string("schema_version").value_or(""), std::string("1.0"));
    PB_CHECK_EQ(v->get_int("last_serial").value_or(0), int64_t(42));
    PB_CHECK_EQ(v->object.size(), size_t(2));                // nothing that identifies the user
    StateStore again(dirs);
    PB_CHECK_EQ(again.last_serial().value_or(0), int64_t(42));
    PB_CHECK(!again.fresh_start());
}

PB_TEST(an_unreadable_or_malformed_file_counts_as_no_earlier_serial) {
    for (const char* bad : {"", "not json", "{}", "[1]", "{\"schema_version\":\"1.0\"}", "{\"schema_version\":\"1.0\",\"last_serial\":0}",
                            "{\"schema_version\":\"1.0\",\"last_serial\":-3}", "{\"schema_version\":\"1.0\",\"last_serial\":\"7\"}",
                            "{\"schema_version\":\"1.0\",\"last_serial\":1.5}", "{\"schema_version\":\"1.0\",\"last_serial\":5,\"last_serial\":6}"}) {
        pbtest::MemStateDirs dirs;
        dirs.write_atomic("state.json", bad);
        StateStore s(dirs);
        PB_CHECK(!s.last_serial().has_value());
        PB_CHECK(s.fresh_start());
    }
}

PB_TEST(the_serial_is_never_lowered_and_an_equal_serial_changes_nothing) {
    pbtest::MemStateDirs dirs;
    StateStore s(dirs);
    PB_CHECK(s.record(10));
    PB_CHECK(s.record(10));                                  // equal: accepted as unchanged
    PB_CHECK(s.record(7));                                   // lower: not written (the caller rejects such a catalog)
    PB_CHECK_EQ(StateStore(dirs).last_serial().value_or(0), int64_t(10));
    PB_CHECK(s.record(11));
    PB_CHECK_EQ(StateStore(dirs).last_serial().value_or(0), int64_t(11));
    PB_CHECK(!s.record(0));
    PB_CHECK(!s.record(-4));
    PB_CHECK_EQ(StateStore(dirs).last_serial().value_or(0), int64_t(11));
}

PB_TEST(the_file_is_replaced_through_a_temporary_file_and_a_rename) {
    pbtest::TmpDir tmp;
    ::setenv("XDG_STATE_HOME", tmp.path().c_str(), 1);
    PosixStateDirs dirs;
    StateStore s(dirs);
    PB_CHECK(s.record(3));
    PB_CHECK(s.record(4));
    ::unsetenv("XDG_STATE_HOME");
    PB_CHECK_EQ(tmp.read("pbstore-installer/state.json"), std::string("{\"schema_version\":\"1.0\",\"last_serial\":4}\n"));
    struct stat st;
    PB_CHECK(::stat(tmp.file("pbstore-installer/state.json.tmp").c_str(), &st) != 0);   // no leftover
}

PB_TEST(the_folder_follows_the_xdg_rules_with_a_home_fallback) {
    pbtest::TmpDir tmp;
    ::setenv("XDG_STATE_HOME", (tmp.path() + "/xdg").c_str(), 1);
    PB_CHECK_EQ(PosixStateDirs().state_dir(), tmp.path() + "/xdg/pbstore-installer");
    ::unsetenv("XDG_STATE_HOME");
    ::setenv("HOME", tmp.path().c_str(), 1);
    PB_CHECK_EQ(PosixStateDirs().state_dir(), tmp.path() + "/.local/state/pbstore-installer");
    PB_CHECK_EQ(PosixStateDirs("/explicit/dir").state_dir(), std::string("/explicit/dir"));
}

PB_TEST_MAIN()
