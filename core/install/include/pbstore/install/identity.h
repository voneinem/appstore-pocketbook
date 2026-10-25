#pragma once
// Model and firmware of the reader. Where they can be read on the mounted storage is not known yet (research
// §2, F12), so the list of sources is empty for now and both count as unknown; each unknown value needs its
// own named confirmation. Whether a package fits is decided by a pure function.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/confirmations.h"
#include "pbstore/install/reader_fs.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct Identity {
    std::optional<std::string> model;      // for example "1030"
    std::optional<std::string> firmware;   // for example "6.11.1437"
};

class IdentitySource {
public:
    virtual ~IdentitySource() = default;
    virtual std::optional<std::string> model(ReaderFs& fs) = 0;
    virtual std::optional<std::string> firmware(ReaderFs& fs) = 0;
};

// The sources known so far (none until the manual task D-02 finds one).
std::vector<IdentitySource*> default_identity_sources();
// For each value the first source that knows it.
Identity identify(ReaderFs& fs, const std::vector<IdentitySource*>& sources);

struct Requirement {
    Confirmation confirmation;
    Finding finding;   // E-INS-09 or E-INS-11
};

struct Compatibility {
    std::vector<Finding> refusals;           // E-INS-08, E-INS-10: no plan
    std::vector<Requirement> requirements;   // E-INS-09, E-INS-11: the plan needs the named confirmation
};

Compatibility check_compatibility(const Identity& identity, const Manifest& manifest);

}  // namespace install
}  // namespace pbstore
