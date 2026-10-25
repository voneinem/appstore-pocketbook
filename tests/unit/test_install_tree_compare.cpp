// T017: tests/support/tree_compare.h: "byte-identical" in the installer tests means the same relative
// paths, kinds, sizes and SHA-256; timestamps and permissions are ignored.
#include "crash_fs.h"
#include "test_support.h"
#include "tmp_dir.h"
#include "tree_compare.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace pbtest;

PB_TEST(a_snapshot_is_a_sorted_list_of_path_kind_size_and_hash) {
    TmpDir d;
    d.write_p("b/two.txt", "22");
    d.write_p("a.txt", "1");
    d.write_p("b/sub/Three.txt", "333");
    Snapshot s = snapshot(d.path());
    PB_CHECK_EQ(s.size(), size_t(5));   // a.txt, b, b/sub, b/sub/Three.txt, b/two.txt
    for (size_t i = 1; i < s.size(); ++i) PB_CHECK(s[i - 1].path < s[i].path);
    PB_CHECK_EQ(s[0].path, std::string("a.txt"));
    PB_CHECK(s[0].kind == 'f' && s[0].size == 1);
    PB_CHECK_EQ(s[0].sha256, pbstore::sha256_hex("1"));
    bool dir_seen = false;
    for (const auto& i : s) if (i.path == "b/sub") { dir_seen = true; PB_CHECK(i.kind == 'd'); }
    PB_CHECK(dir_seen);
    bool case_kept = false;
    for (const auto& i : s) if (i.path == "b/sub/Three.txt") case_kept = true;
    PB_CHECK(case_kept);
}

PB_TEST(timestamps_and_permissions_are_ignored) {
    TmpDir d;
    d.write_p("f", "same");
    Snapshot before = snapshot(d.path());
    struct timeval tv[2] = {{1000000000, 0}, {1000000000, 0}};
    PB_CHECK_EQ(::utimes(d.file("f").c_str(), tv), 0);
    PB_CHECK_EQ(::chmod(d.file("f").c_str(), 0600), 0);
    Diff x = compare(before, snapshot(d.path()));
    PB_CHECK(x.identical);
}

PB_TEST(compare_names_added_removed_and_changed_paths) {
    TmpDir a, b;
    a.write_p("keep", "k");
    a.write_p("gone", "g");
    a.write_p("edit", "old");
    b.write_p("keep", "k");
    b.write_p("new/file", "n");
    b.write_p("edit", "new");
    Diff x = compare(snapshot(a.path()), snapshot(b.path()));
    PB_CHECK(!x.identical);
    PB_CHECK_EQ(x.removed, (std::vector<std::string>{"gone"}));
    PB_CHECK_EQ(x.added, (std::vector<std::string>{"new", "new/file"}));
    PB_CHECK_EQ(x.changed, (std::vector<std::string>{"edit"}));
    PB_CHECK(x.describe().find("gone") != std::string::npos);
    PB_CHECK(x.describe().find("new/file") != std::string::npos);
    PB_CHECK(x.describe().find("edit") != std::string::npos);
    PB_CHECK(compare(snapshot(a.path()), snapshot(a.path())).identical);
}

PB_TEST(a_change_of_letter_case_in_a_name_is_a_difference) {
    TmpDir a, b;
    a.write_p("Readme.txt", "x");
    b.write_p("README.txt", "x");
    PB_CHECK(!compare(snapshot(a.path()), snapshot(b.path())).identical);
}

PB_TEST(the_same_works_for_an_in_memory_reader_volume) {
    CrashFs fs;
    fs.make_dir("d");
    fs.create_exclusive("d/f", "abc", true);
    Snapshot s = snapshot(static_cast<pbstore::install::ReaderFs&>(fs));
    PB_CHECK_EQ(s.size(), size_t(2));
    PB_CHECK_EQ(s[1].sha256, pbstore::sha256_hex("abc"));
    fs.create_exclusive("d/g", "x", true);
    PB_CHECK(!compare(s, snapshot(static_cast<pbstore::install::ReaderFs&>(fs))).identical);
}

PB_TEST_MAIN()
