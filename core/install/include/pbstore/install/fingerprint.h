#pragma once
// The fingerprint of what a plan depends on: the desktop configuration, the state of every target path and
// the free space. The planner stores it in the plan; the executor recomputes it immediately before it writes
// anything, and a difference means the reader changed since the plan was made (E-INS-25).

#include <map>
#include <string>
#include <vector>

#include "pbstore/install/plan.h"
#include "pbstore/install/reader_fs.h"

namespace pbstore {
namespace install {

// The state of one path: what it is, its size, and the SHA-256 of a regular file.
struct TargetFact {
    Kind kind = Kind::None;
    uint64_t size = 0;
    std::string sha256;   // "unreadable" if a file could not be read
};
TargetFact read_fact(ReaderFs& fs, const std::string& path);

// The basis for the given paths from facts already read (a path without a fact counts as absent).
FingerprintBasis make_basis(const std::string& view_sha256, const std::map<std::string, TargetFact>& facts, const std::vector<std::string>& paths,
                            uint64_t free_bytes);
// Reads the state of the given paths, the desktop configuration and the free space.
FingerprintBasis read_basis(ReaderFs& fs, const std::vector<std::string>& paths);
std::string fingerprint_of(const FingerprintBasis& basis);
// The fingerprint of the reader as it is now, for the paths of the plan.
std::string current_fingerprint(ReaderFs& fs, const Plan& plan);
// The free space counts as changed from a difference of 1 MiB: a few blocks more or less (the reader or the computer writing a log) are
// no change the user could see, and a threshold on the difference, unlike a rounding to whole MiB, has no boundary to fall across.
bool free_space_changed(uint64_t planned, uint64_t now);
// True if the desktop configuration, every target path and (see above) the free space are as the plan assumed.
bool basis_unchanged(const FingerprintBasis& planned, const FingerprintBasis& now);
// What differs between the state the plan assumed and the state now, in words, for the message of E-INS-25.
std::string describe_basis_change(const FingerprintBasis& planned, const FingerprintBasis& now);

// "" for a missing desktop configuration
std::string read_view_sha256(ReaderFs& fs);

}  // namespace install
}  // namespace pbstore
