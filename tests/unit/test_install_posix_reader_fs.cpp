// T013: PosixReaderFs on a temporary directory (the Linux implementation of ReaderFs).
// The cases every ReaderFs must meet live in tests/support/reader_fs_contract.h (T021) and run here for
// PosixReaderFs; the cases below need the real operating system (links, permissions, a second instance).
#include "pbstore/install/reader_fs.h"
#include "pbstore/install/posix_reader_fs.h"
#include "reader_fs_contract.h"
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

PB_TEST(posix_reader_fs_meets_the_reader_fs_contract) {
    pbtest::run_reader_fs_contract([](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<PosixReaderFs>(root); }, "PosixReaderFs");
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

PB_TEST(list_reports_a_link_as_a_link) {
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

PB_TEST(a_directory_the_user_cannot_write_is_reported_as_not_writable_but_not_read_only) {
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

PB_TEST(a_second_instance_on_the_same_reader_cannot_take_the_lock) {
    Env e;
    auto a = e.fs.lock();
    PB_CHECK(a != nullptr);
    PB_CHECK(e.fs.stat("system/pbstore/lock").kind == Kind::File);   // the lock is a file on the volume
    PB_CHECK(e.fs.stat("system/pbstore/lock").kind == Kind::File);
    PosixReaderFs other(e.dir.path());                    // another instance on the same reader
    PB_CHECK(other.lock() == nullptr);
    PB_CHECK(e.fs.lock() == nullptr);                     // also not twice from the same process
    a.reset();
    auto b = other.lock();
    PB_CHECK(b != nullptr);
}

PB_TEST_MAIN()
