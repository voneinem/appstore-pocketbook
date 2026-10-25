// T013: PosixReaderFs on a temporary directory (the Linux implementation of ReaderFs).
#include "pbstore/install/reader_fs.h"
#include "pbstore/install/posix_reader_fs.h"
#include "test_support.h"
#include "tmp_dir.h"

#include <sys/stat.h>
#include <unistd.h>

using namespace pbstore::install;

namespace {
struct Env {
    pbtest::TmpDir dir;
    PosixReaderFs fs{dir.path()};
};
bool has_name(const std::vector<DirEntry>& v, const std::string& n) {
    for (const auto& e : v) if (e.name == n) return true;
    return false;
}
}  // namespace

PB_TEST(create_exclusive_fails_if_the_name_exists_and_keeps_the_old_content) {
    Env e;
    PB_CHECK(e.fs.make_dir("applications"));
    PB_CHECK(e.fs.create_exclusive("applications/a.txt", "first", true));
    PB_CHECK(!e.fs.create_exclusive("applications/a.txt", "second", true));
    std::string out;
    PB_CHECK(e.fs.read_file("applications/a.txt", 100, &out));
    PB_CHECK_EQ(out, std::string("first"));
    PB_CHECK(!e.fs.create_exclusive("missing-dir/b.txt", "x", false));   // the parent must exist
}

PB_TEST(rename_free_moves_into_a_free_name_and_never_replaces) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(e.fs.create_exclusive("d/a", "A", true));
    PB_CHECK(e.fs.create_exclusive("d/b", "B", true));
    PB_CHECK(!e.fs.rename_free("d/a", "d/b"));          // target exists: refused
    std::string out;
    PB_CHECK(e.fs.read_file("d/b", 10, &out));
    PB_CHECK_EQ(out, std::string("B"));
    PB_CHECK(e.fs.stat("d/a").kind == Kind::File);       // the source is still there
    PB_CHECK(e.fs.rename_free("d/a", "d/c"));
    PB_CHECK(!e.fs.stat("d/a").exists());
    PB_CHECK(e.fs.read_file("d/c", 10, &out));
    PB_CHECK_EQ(out, std::string("A"));
    PB_CHECK(!e.fs.rename_free("d/nothing", "d/other"));
}

PB_TEST(a_symbolic_link_anywhere_on_the_path_is_link_like_and_refused_for_writes) {
    Env e;
    PB_CHECK(e.fs.make_dir("real"));
    PB_CHECK(e.fs.create_exclusive("real/f", "x", true));
    PB_CHECK_EQ(::symlink("real", e.dir.file("lnk").c_str()), 0);
    PB_CHECK_EQ(::symlink("real/f", e.dir.file("flnk").c_str()), 0);
    PB_CHECK(e.fs.stat("lnk").link_like());
    PB_CHECK(e.fs.stat("flnk").link_like());
    PB_CHECK(e.fs.stat("lnk/f").link_like());            // a link earlier on the path
    PB_CHECK(!e.fs.create_exclusive("lnk/new", "x", true));
    PB_CHECK(!e.dir.exists("real/new"));                 // nothing was written through the link
    PB_CHECK(!e.fs.make_dir("lnk/sub"));
    PB_CHECK(!e.fs.rename_free("lnk/f", "real/g"));
    PB_CHECK(!e.fs.rename_free("real/f", "lnk/g"));
    PB_CHECK(!e.fs.remove_file("lnk/f"));
    PB_CHECK(e.dir.exists("real/f"));
    std::string out;
    PB_CHECK(!e.fs.read_file("flnk", 10, &out));         // a link is not read as a file
    PB_CHECK(!e.fs.read_file("lnk/f", 10, &out));
}

PB_TEST(list_returns_names_and_kinds_and_stat_the_size) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(e.fs.make_dir("d/sub"));
    PB_CHECK(e.fs.create_exclusive("d/f.bin", std::string(1234, 'z'), false));
    PB_CHECK_EQ(::symlink("f.bin", e.dir.file("d/ln").c_str()), 0);
    auto l = e.fs.list("d");
    PB_CHECK_EQ(l.size(), size_t(3));
    for (const auto& x : l) {
        if (x.name == "sub") PB_CHECK(x.kind == Kind::Dir);
        else if (x.name == "f.bin") { PB_CHECK(x.kind == Kind::File); PB_CHECK_EQ(x.size, uint64_t(1234)); }
        else if (x.name == "ln") PB_CHECK(x.kind == Kind::Link);
        else PB_FAIL_AT("unexpected name " + x.name);
    }
    PB_CHECK(e.fs.list("nothing").empty());
    PB_CHECK_EQ(e.fs.stat("d/f.bin").size, uint64_t(1234));
    PB_CHECK(e.fs.stat("d/sub").kind == Kind::Dir);
    PB_CHECK(e.fs.stat("").kind == Kind::Dir);           // the root itself
    PB_CHECK(has_name(e.fs.list(""), "d"));
}

PB_TEST(remove_file_remove_dir_and_make_dir_follow_the_usual_rules) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(!e.fs.make_dir("d"));                       // exists
    PB_CHECK(e.fs.create_exclusive("d/f", "x", true));
    PB_CHECK(!e.fs.remove_dir("d"));                     // not empty
    PB_CHECK(!e.fs.remove_file("d"));                    // a directory is not removed as a file
    PB_CHECK(e.fs.remove_file("d/f"));
    PB_CHECK(!e.fs.remove_file("d/f"));
    PB_CHECK(e.fs.remove_dir("d"));
    PB_CHECK(!e.fs.stat("d").exists());
}

PB_TEST(flush_dir_and_flush_volume_succeed) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(e.fs.create_exclusive("d/f", "x", false));
    PB_CHECK(e.fs.flush_dir("d"));
    PB_CHECK(e.fs.flush_dir(""));
    PB_CHECK(e.fs.flush_volume());
    PB_CHECK(!e.fs.flush_dir("nothing"));
}

PB_TEST(space_reports_free_cluster_and_writable) {
    Env e;
    Space s = e.fs.space();
    PB_CHECK(s.free_bytes > 0);
    PB_CHECK(s.cluster_bytes >= 512);
    PB_CHECK(s.writable);
    PB_CHECK(!s.read_only);
    PB_CHECK_EQ(::chmod(e.dir.path().c_str(), 0555), 0);
    if (::geteuid() != 0) {
        PB_CHECK(!e.fs.space().writable);                // the directory cannot be written by this user
        PB_CHECK(!e.fs.space().read_only);               // but the mount is not read-only
    }
    ::chmod(e.dir.path().c_str(), 0755);
}

PB_TEST(the_lock_is_exclusive_and_released_with_the_handle) {
    Env e;
    auto a = e.fs.lock();
    PB_CHECK(a != nullptr);
    PB_CHECK(e.fs.stat("system/pbstore/lock").kind == Kind::File);
    PosixReaderFs other(e.dir.path());                    // another instance on the same reader
    PB_CHECK(other.lock() == nullptr);
    PB_CHECK(e.fs.lock() == nullptr);                     // also not twice from the same process
    a.reset();
    auto b = other.lock();
    PB_CHECK(b != nullptr);
}

PB_TEST(paths_with_dot_dot_or_absolute_or_odd_parts_are_rejected) {
    Env e;
    PB_CHECK(e.fs.make_dir("d"));
    PB_CHECK(e.fs.create_exclusive("d/f", "x", true));
    std::string out;
    for (const char* bad : {"../x", "d/../d/f", "/etc/passwd", "d//f", "./d", "d/./f", "d/", "d/f/.."}) {
        PB_CHECK(!e.fs.stat(bad).exists());
        PB_CHECK(!e.fs.read_file(bad, 10, &out));
        PB_CHECK(!e.fs.create_exclusive(bad, "x", false));
        PB_CHECK(!e.fs.make_dir(bad));
        PB_CHECK(!e.fs.remove_file(bad));
        PB_CHECK(!e.fs.rename_free(bad, "d/z"));
        PB_CHECK(!e.fs.rename_free("d/f", bad));
    }
    PB_CHECK(!valid_relative_path("../x"));
    PB_CHECK(!valid_relative_path("/abs"));
    PB_CHECK(valid_relative_path("applications/a b.app"));
    PB_CHECK(valid_relative_path(""));
}

PB_TEST(read_file_enforces_the_maximum_size) {
    Env e;
    PB_CHECK(e.fs.create_exclusive("big", std::string(2000, 'x'), true));
    std::string out;
    PB_CHECK(!e.fs.read_file("big", 1999, &out));
    PB_CHECK(e.fs.read_file("big", 2000, &out));
    PB_CHECK_EQ(out.size(), size_t(2000));
    PB_CHECK(!e.fs.read_file("nothing", 10, &out));
}

PB_TEST_MAIN()
