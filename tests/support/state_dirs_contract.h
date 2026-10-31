#pragma once
// The contract every StateDirs must meet: run_state_dirs_contract(dirs). It runs against PosixStateDirs on a
// temporary folder (the installer's per-user folder and, with another root, the store's system/pbstore/app on the
// reader), so the two configurations are tested by the same cases. The in-memory test double does not refuse odd
// names and is not meant to pass the "names" case.

#include <string>

#include "pbstore/install/state_dirs.h"
#include "test_support.h"

namespace pbtest {

inline void run_state_dirs_contract(pbstore::install::StateDirs& dirs) {
    std::string out;
    // a missing file
    PB_CHECK(!dirs.read("missing", &out));
    PB_CHECK_EQ(dirs.size("missing"), UINT64_MAX);
    PB_CHECK(!dirs.remove("missing"));
    // write_atomic creates and replaces, and leaves no temporary file behind
    PB_CHECK(dirs.write_atomic("a", "first"));
    PB_CHECK(dirs.read("a", &out));
    PB_CHECK_EQ(out, std::string("first"));
    PB_CHECK(dirs.write_atomic("a", "second, longer"));
    PB_CHECK(dirs.read("a", &out));
    PB_CHECK_EQ(out, std::string("second, longer"));
    PB_CHECK_EQ(dirs.size("a"), uint64_t(14));
    PB_CHECK(!dirs.read("a.tmp", &out));
    PB_CHECK(dirs.write_atomic("a", ""));                      // an empty file is a file
    PB_CHECK_EQ(dirs.size("a"), uint64_t(0));
    // append creates and extends
    PB_CHECK(dirs.append("log", "one\n"));
    PB_CHECK(dirs.append("log", "two\n"));
    PB_CHECK(dirs.read("log", &out));
    PB_CHECK_EQ(out, std::string("one\ntwo\n"));
    // rename replaces the target and removes the source
    PB_CHECK(dirs.write_atomic("x", "X"));
    PB_CHECK(dirs.write_atomic("y", "Y"));
    PB_CHECK(dirs.rename("x", "y"));
    PB_CHECK(dirs.read("y", &out));
    PB_CHECK_EQ(out, std::string("X"));
    PB_CHECK(!dirs.read("x", &out));
    PB_CHECK(!dirs.rename("x", "z"));                          // the source is gone
    PB_CHECK(!dirs.read("z", &out));
    // remove
    PB_CHECK(dirs.remove("y"));
    PB_CHECK(!dirs.remove("y"));
    PB_CHECK_EQ(dirs.size("y"), UINT64_MAX);
    // names with a slash, ".." or nothing at all are refused by every operation
    for (const char* bad : {"", ".", "..", "a/b", "../escape", "/abs", "sub/"}) {
        PB_CHECK(!dirs.write_atomic(bad, "x"));
        PB_CHECK(!dirs.append(bad, "x"));
        PB_CHECK(!dirs.read(bad, &out));
        PB_CHECK(!dirs.remove(bad));
        PB_CHECK(!dirs.rename(bad, "ok"));
        PB_CHECK(!dirs.rename("a", bad));
        PB_CHECK_EQ(dirs.size(bad), UINT64_MAX);
    }
    PB_CHECK(dirs.read("a", &out));                            // the refusals changed nothing
}

}  // namespace pbtest
