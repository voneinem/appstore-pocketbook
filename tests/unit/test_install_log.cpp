// T019: the log on the computer: one line per action, no personal data, rotated, private.
#include "mem_state_dirs.h"
#include "fixed_clock.h"
#include "pbstore/install/log.h"
#include "pbstore/install/posix_state_dirs.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>

using namespace pbstore::install;

namespace {
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
}  // namespace

PB_TEST(the_redactor_replaces_mount_label_file_and_home) {
    Redactor r;
    r.set_mount_path("/run/media/alice/PB1030");
    r.set_volume_label("PB1030");
    r.set_local_file("/home/alice/Downloads/testapp-1.2.0.zip");
    r.set_home("/home/alice");
    const std::string in = "wrote /run/media/alice/PB1030/applications/a.app (label PB1030) from /home/alice/Downloads/testapp-1.2.0.zip, home /home/alice/x";
    const std::string out = r.redact(in);
    PB_CHECK(has(out, "<reader>/applications/a.app"));
    PB_CHECK(has(out, "<file>"));
    PB_CHECK(has(out, "<home>/x"));
    PB_CHECK(!has(out, "alice"));
    PB_CHECK(!has(out, "PB1030"));
    PB_CHECK(!has(out, "Downloads"));
}

PB_TEST(the_label_is_replaced_where_it_stands_alone_but_not_inside_other_words) {
    Redactor r;
    r.set_volume_label("MyReader");
    PB_CHECK_EQ(r.redact("drive MyReader is read-only"), std::string("drive <label> is read-only"));
    r.set_mount_path("/mnt/MyReader");
    PB_CHECK_EQ(r.redact("/mnt/MyReader/x"), std::string("<reader>/x"));
}

PB_TEST(urls_lose_credentials_and_query_strings) {
    PB_CHECK_EQ(Redactor::clean_url("https://user:secret@example.org/a/b.zip?token=abc#frag"), std::string("https://example.org/a/b.zip"));
    PB_CHECK_EQ(Redactor::clean_url("https://example.org/a/b.zip"), std::string("https://example.org/a/b.zip"));
    PB_CHECK_EQ(Redactor::clean_url("https://tok@example.org/"), std::string("https://example.org/"));
    Redactor r;
    const std::string out = r.redact("fetch https://user:secret@example.org/x?k=v failed");
    PB_CHECK(!has(out, "secret"));
    PB_CHECK(!has(out, "k=v"));
    PB_CHECK(has(out, "https://example.org/x"));
}

PB_TEST(the_log_line_has_time_action_app_version_result_and_rule_ids) {
    pbtest::MemStateDirs dirs;
    pbtest::FixedClock clock;
    clock.set_utc(2026, 10, 25, 12, 0, 7);
    Redactor red;
    Log log(dirs, clock, red);
    log.event("install", "testapp", "1.2.0", "ok", {});
    log.event("install", "testapp", "1.2.0", "failed", {"E-INS-07", "E-INS-37"}, "not enough space");
    log.event("repair", "", "", "ok", {});
    std::string text;
    PB_CHECK(dirs.read("installer.log", &text));
    PB_CHECK(has(text, "2026-10-25T12:00:07Z install testapp 1.2.0 ok\n"));
    PB_CHECK(has(text, "2026-10-25T12:00:07Z install testapp 1.2.0 failed E-INS-07 E-INS-37 not enough space\n"));
    PB_CHECK(has(text, "2026-10-25T12:00:07Z repair - - ok\n"));
}

PB_TEST(the_text_of_an_event_goes_through_the_redactor) {
    pbtest::MemStateDirs dirs;
    pbtest::FixedClock clock;
    Redactor red;
    red.set_mount_path("/run/media/alice/PB1030");
    red.set_volume_label("PB1030");
    red.set_home("/home/alice");
    Log log(dirs, clock, red);
    log.event("install", "testapp", "1.2.0", "failed", {"E-INS-06"}, "cannot write /run/media/alice/PB1030/applications/x as /home/alice");
    std::string text;
    PB_CHECK(dirs.read("installer.log", &text));
    PB_CHECK(!has(text, "alice"));
    PB_CHECK(!has(text, "PB1030"));
    PB_CHECK(has(text, "<reader>/applications/x"));
}

PB_TEST(the_log_rotates_at_512_kib_and_keeps_three_files) {
    pbtest::MemStateDirs dirs;
    pbtest::FixedClock clock;
    Redactor red;
    Log log(dirs, clock, red);
    const std::string filler(1000, 'x');
    for (int i = 0; i < 2000; ++i) log.event("install", "testapp", "1.0.0", "ok", {}, filler);   // about 2 MiB
    PB_CHECK(dirs.exists("installer.log"));
    PB_CHECK(dirs.exists("installer.log.1"));
    PB_CHECK(dirs.exists("installer.log.2"));
    PB_CHECK(!dirs.exists("installer.log.3"));
    for (const char* n : {"installer.log", "installer.log.1", "installer.log.2"})
        PB_CHECK(dirs.size(n) <= 512u * 1024u);
    PB_CHECK(dirs.size("installer.log.1") > 400u * 1024u);   // full files, not stubs
}

PB_TEST(the_log_cannot_be_switched_off_and_writes_only_in_the_state_folder) {
    pbtest::MemStateDirs dirs;
    pbtest::FixedClock clock;
    Redactor red;
    Log log(dirs, clock, red);
    log.event("list", "", "", "ok", {});
    for (const auto& name : dirs.names()) PB_CHECK(name.compare(0, 13, "installer.log") == 0);
    PB_CHECK(dirs.exists("installer.log"));
}

PB_TEST(the_posix_state_folder_is_private) {
    pbtest::TmpDir tmp;
    ::setenv("XDG_STATE_HOME", tmp.path().c_str(), 1);
    {
        PosixStateDirs dirs;
        PB_CHECK_EQ(dirs.state_dir(), tmp.path() + "/pbstore-installer");
        pbtest::FixedClock clock;
        Redactor red;
        Log log(dirs, clock, red);
        log.event("list", "", "", "ok", {});
    }
    ::unsetenv("XDG_STATE_HOME");
    struct stat st;
    PB_CHECK_EQ(::stat((tmp.path() + "/pbstore-installer").c_str(), &st), 0);
    PB_CHECK_EQ(int(st.st_mode & 0777), 0700);
    PB_CHECK_EQ(::stat((tmp.path() + "/pbstore-installer/installer.log").c_str(), &st), 0);
    PB_CHECK_EQ(int(st.st_mode & 0777), 0600);
}

PB_TEST(a_log_with_another_file_name_writes_and_rotates_that_name_and_the_default_is_unchanged) {
    pbtest::MemStateDirs dirs;
    pbtest::FixedClock clock;
    Redactor red;
    Log log(dirs, clock, red, "pbstore.log");               // the store app names its log pbstore.log
    log.event("install", "testapp", "1.0.0", "ok", {}, std::string(1000, 'x'));
    PB_CHECK(dirs.exists("pbstore.log"));
    PB_CHECK(!dirs.exists("installer.log"));
    const std::string filler(1000, 'x');
    for (int i = 0; i < 2000; ++i) log.event("install", "testapp", "1.0.0", "ok", {}, filler);
    PB_CHECK(dirs.exists("pbstore.log.1"));
    PB_CHECK(dirs.exists("pbstore.log.2"));
    PB_CHECK(!dirs.exists("pbstore.log.3"));
    for (const auto& name : dirs.names()) PB_CHECK(name.compare(0, 11, "pbstore.log") == 0);
    pbtest::MemStateDirs other;
    Log plain(other, clock, red);
    plain.event("list", "", "", "ok", {});
    PB_CHECK(other.exists("installer.log"));                // the default constructor still uses installer.log
}

PB_TEST_MAIN()
