#pragma once
// The contract every ReaderFs must meet, as one function: run_reader_fs_contract(factory, label). The same cases run
// against the computer's implementation (PosixReaderFs over a temporary folder) and against the reader's
// configuration of it (feature 003: make_reader_storage), also wrapped in FatRulesFs, so that "the same contract
// tests on both" (constitution Principle VI) is true by construction. Cases that need the real operating system
// (links made with symlink(2), chmod, a second process holding the lock) stay with the implementation's own test.
//
// A factory builds a ReaderFs for a root folder; the root is a temporary folder, or ignored by an in-memory volume.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pbstore/install/reader_fs.h"
#include "test_support.h"
#include "tmp_dir.h"

namespace pbtest {

struct ReaderFsEnv {
    TmpDir dir;
    std::unique_ptr<pbstore::install::ReaderFs> owner;
    pbstore::install::ReaderFs& fs;
    explicit ReaderFsEnv(const pbstore::install::ReaderFsFactory& factory) : owner(factory(dir.path())), fs(*owner) {}
};

struct ReaderFsCase {
    const char* name;
    std::function<void(const pbstore::install::ReaderFsFactory&)> run;
};

inline bool contract_has_name(const std::vector<pbstore::install::DirEntry>& v, const std::string& n) {
    for (const auto& e : v) if (e.name == n) return true;
    return false;
}

inline const std::vector<ReaderFsCase>& reader_fs_cases() {
    using namespace pbstore::install;
    static const std::vector<ReaderFsCase> cases = {
        {"create_exclusive_fails_if_the_name_exists_and_keeps_the_old_content", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.make_dir("applications"));
             PB_CHECK(e.fs.create_exclusive("applications/a.txt", "first", true));
             PB_CHECK(!e.fs.create_exclusive("applications/a.txt", "second", true));
             std::string out;
             PB_CHECK(e.fs.read_file("applications/a.txt", 100, &out));
             PB_CHECK_EQ(out, std::string("first"));
             PB_CHECK(!e.fs.create_exclusive("missing-dir/b.txt", "x", false));   // the parent must exist
         }},
        {"rename_free_moves_into_a_free_name_and_never_replaces", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
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
         }},
        {"list_returns_names_and_kinds_and_stat_the_size", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.make_dir("d"));
             PB_CHECK(e.fs.make_dir("d/sub"));
             PB_CHECK(e.fs.create_exclusive("d/f.bin", std::string(1234, 'z'), false));
             auto l = e.fs.list("d");
             PB_CHECK_EQ(l.size(), size_t(2));
             for (const auto& x : l) {
                 if (x.name == "sub") PB_CHECK(x.kind == Kind::Dir);
                 else if (x.name == "f.bin") { PB_CHECK(x.kind == Kind::File); PB_CHECK_EQ(x.size, uint64_t(1234)); }
                 else PB_FAIL_AT("unexpected name " + x.name);
             }
             PB_CHECK(e.fs.list("nothing").empty());
             PB_CHECK_EQ(e.fs.stat("d/f.bin").size, uint64_t(1234));
             PB_CHECK(e.fs.stat("d/sub").kind == Kind::Dir);
             PB_CHECK(e.fs.stat("").kind == Kind::Dir);           // the root itself
             PB_CHECK(contract_has_name(e.fs.list(""), "d"));
         }},
        {"a_folder_including_the_root_can_be_listed_again_and_sees_what_changed_since", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.list("").empty());
             PB_CHECK(e.fs.make_dir("a"));
             PB_CHECK_EQ(e.fs.list("").size(), size_t(1));
             PB_CHECK(e.fs.make_dir("b"));
             PB_CHECK_EQ(e.fs.list("").size(), size_t(2));
             PB_CHECK_EQ(e.fs.list("").size(), size_t(2));         // and again: a listing never uses up the next one
             PB_CHECK(e.fs.create_exclusive("a/f", "x", false));
             PB_CHECK_EQ(e.fs.list("a").size(), size_t(1));
             PB_CHECK_EQ(e.fs.list("a").size(), size_t(1));
         }},
        {"remove_file_remove_dir_and_make_dir_follow_the_usual_rules", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.make_dir("d"));
             PB_CHECK(!e.fs.make_dir("d"));                       // exists
             PB_CHECK(e.fs.create_exclusive("d/f", "x", true));
             PB_CHECK(!e.fs.remove_dir("d"));                     // not empty
             PB_CHECK(!e.fs.remove_file("d"));                    // a directory is not removed as a file
             PB_CHECK(e.fs.remove_file("d/f"));
             PB_CHECK(!e.fs.remove_file("d/f"));
             PB_CHECK(e.fs.remove_dir("d"));
             PB_CHECK(!e.fs.stat("d").exists());
         }},
        {"flush_dir_and_flush_volume_succeed", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.make_dir("d"));
             PB_CHECK(e.fs.create_exclusive("d/f", "x", false));
             PB_CHECK(e.fs.flush_dir("d"));
             PB_CHECK(e.fs.flush_dir(""));
             PB_CHECK(e.fs.flush_volume());
             PB_CHECK(!e.fs.flush_dir("nothing"));
         }},
        {"space_reports_free_cluster_and_writable", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             Space s = e.fs.space();
             PB_CHECK(s.free_bytes > 0);
             PB_CHECK(s.cluster_bytes >= 512);
             PB_CHECK(s.writable);
             PB_CHECK(!s.read_only);
         }},
        {"the_lock_is_exclusive_and_released_with_the_handle", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             auto a = e.fs.lock();
             PB_CHECK(a != nullptr);
             PB_CHECK(e.fs.lock() == nullptr);                     // also not twice from the same process
             a.reset();
             auto b = e.fs.lock();
             PB_CHECK(b != nullptr);
         }},
        {"paths_with_dot_dot_or_absolute_or_odd_parts_are_rejected", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
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
         }},
        {"read_file_enforces_the_maximum_size", [](const ReaderFsFactory& make) {
             ReaderFsEnv e(make);
             PB_CHECK(e.fs.create_exclusive("big", std::string(2000, 'x'), true));
             std::string out;
             PB_CHECK(!e.fs.read_file("big", 1999, &out));
             PB_CHECK(e.fs.read_file("big", 2000, &out));
             PB_CHECK_EQ(out.size(), size_t(2000));
             PB_CHECK(!e.fs.read_file("nothing", 10, &out));
         }},
    };
    return cases;
}

// Runs every case; a failure names the implementation (the label) and the case.
inline void run_reader_fs_contract(const pbstore::install::ReaderFsFactory& factory, const std::string& label) {
    for (const ReaderFsCase& c : reader_fs_cases()) {
        try {
            c.run(factory);
        } catch (const Failure& f) {
            throw Failure{label + ": " + c.name + ": " + f.message};
        }
    }
}

}  // namespace pbtest
