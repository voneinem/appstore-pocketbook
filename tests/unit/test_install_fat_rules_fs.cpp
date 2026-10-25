// T014: the test double FatRulesFs behaves like the FAT volume of the reader: no replace, case-insensitive
// names, no links, 4 GiB limit, a restricted set of file name characters.
#include "crash_fs.h"
#include "fat_rules_fs.h"
#include "test_support.h"

using namespace pbstore::install;
using pbtest::CrashFs;
using pbtest::FatRulesFs;

namespace {
struct Env {
    CrashFs base;
    FatRulesFs fs{base};
};
}  // namespace

PB_TEST(rename_onto_an_existing_name_fails) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(e.fs.create_exclusive("d/a", "A", true));
    PB_CHECK(e.fs.create_exclusive("d/b", "B", true));
    PB_CHECK(!e.fs.rename_free("d/a", "d/b"));
    PB_CHECK(e.fs.rename_free("d/a", "d/c"));
}

PB_TEST(names_differing_only_by_ascii_case_collide) {
    Env e;
    PB_CHECK(e.fs.make_dir("Applications"));
    PB_CHECK(e.fs.create_exclusive("Applications/Readme.TXT", "x", true));
    PB_CHECK(!e.fs.create_exclusive("Applications/readme.txt", "y", true));           // create collides
    PB_CHECK(!e.fs.make_dir("applications"));                                           // directories too
    PB_CHECK(e.fs.create_exclusive("Applications/other", "z", true));
    PB_CHECK(!e.fs.rename_free("Applications/other", "Applications/README.txt"));        // rename collides
    PB_CHECK(e.fs.stat("applications/README.TXT").kind == Kind::File);                  // lookups ignore case
    PB_CHECK(e.fs.stat("APPLICATIONS/readme.txt").kind == Kind::File);
    std::string out;
    PB_CHECK(e.fs.read_file("applications/readme.txt", 10, &out));
    PB_CHECK_EQ(out, std::string("x"));
    auto l = e.fs.list("APPLICATIONS");
    bool seen = false;
    for (const auto& x : l) if (x.name == "Readme.TXT") seen = true;                    // names come back as stored
    PB_CHECK(seen);
    PB_CHECK(e.fs.remove_file("applications/README.txt"));
    PB_CHECK(!e.fs.stat("Applications/Readme.TXT").exists());
}

PB_TEST(no_links_can_be_created_and_existing_links_are_reported) {
    Env e;
    // the double has no way to create a link; one planted in the base shows up as link-like
    PB_CHECK(e.base.make_dir("real"));
    PB_CHECK(e.base.plant_link("lnk"));
    PB_CHECK(e.fs.stat("lnk").link_like());
    PB_CHECK(e.fs.stat("LNK").link_like());
    PB_CHECK(!e.fs.create_exclusive("lnk/x", "x", true));
    PB_CHECK(!e.fs.rename_free("real", "lnk"));
}

PB_TEST(a_file_of_4_gib_or_more_is_refused) {
    // the limit is 4 GiB minus one byte; the test uses a small limit so it need not allocate 4 GiB
    PB_CHECK_EQ(FatRulesFs::kMaxFileBytes, uint64_t(4294967295ull));
    CrashFs base;
    FatRulesFs fs(base, /*max_file_bytes=*/10);
    PB_CHECK(fs.make_dir("d"));
    PB_CHECK(fs.create_exclusive("d/ten", std::string(10, 'x'), true));
    PB_CHECK(!fs.create_exclusive("d/eleven", std::string(11, 'x'), true));
    PB_CHECK(!fs.stat("d/eleven").exists());
}

PB_TEST(names_with_characters_outside_the_allowed_set_are_refused) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    for (const char* bad : {"d/a:b", "d/a*b", "d/a?b", "d/a\"b", "d/a<b", "d/a>b", "d/a|b", "d/a\\b", "d/a\tb", "d/\xc3\xa4"})
        PB_CHECK(!e.fs.create_exclusive(bad, "x", true));
    PB_CHECK(!e.fs.make_dir("d/we:ird"));
    PB_CHECK(e.fs.create_exclusive("d/A-b_c.d+e f", "x", true));
    PB_CHECK(e.fs.create_exclusive("d/ok.tmp", "x", true));
    PB_CHECK(!e.fs.rename_free("d/ok.tmp", "d/bad:name"));
}

PB_TEST(a_second_name_of_a_directory_cannot_be_made_by_case) {
    Env e;
    PB_CHECK(e.fs.make_dir("system"));
    PB_CHECK(e.fs.make_dir("system/Notes"));
    PB_CHECK(!e.fs.make_dir("system/notes"));
    PB_CHECK(e.fs.stat("SYSTEM/NOTES").kind == Kind::Dir);
    PB_CHECK(e.fs.remove_dir("system/notes"));
    PB_CHECK(!e.fs.stat("system/Notes").exists());
}

PB_TEST_MAIN()
