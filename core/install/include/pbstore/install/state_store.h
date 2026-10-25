#pragma once
// The serial of the last accepted catalog, kept per user on the computer (rollback protection). A first
// run, an unreadable file or a malformed file all mean "no earlier serial": any verified catalog is then
// accepted and the user is told. The file holds nothing that identifies the user.

#include <cstdint>
#include <optional>

#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace install {

class StateStore {
public:
    explicit StateStore(StateDirs& dirs) : dirs_(dirs) {}
    std::optional<int64_t> last_serial() const;
    bool fresh_start() const { return !last_serial().has_value(); }
    // Remembers the serial of an accepted catalog; never lowers it. False if it could not be written or the
    // serial is not positive.
    bool record(int64_t serial);

private:
    StateDirs& dirs_;
};

}  // namespace install
}  // namespace pbstore
