#pragma once
// How much free space an operation needs (spec FR-002). The sum is shown in the plan with its parts.

#include <cstdint>
#include <vector>

namespace pbstore {
namespace install {

constexpr uint64_t kSpaceMarginBytes = 10ull * 1024 * 1024;   // the fixed margin

// `bytes` rounded up to whole clusters (a file takes at least one cluster, an empty file none).
uint64_t round_to_clusters(uint64_t bytes, uint64_t cluster);

struct SpaceParts {
    uint64_t files = 0;        // the new files, rounded up to whole clusters
    uint64_t staging = 0;      // their staged copies in the journal area
    uint64_t backup = 0;       // the copy of the desktop configuration
    uint64_t record = 0;       // the install record, and its staged copy
    uint64_t journal = 0;      // the intent, the markers and the prepared desktop configuration
    uint64_t directories = 0;  // one cluster for each directory that is created
    uint64_t old_version = 0;  // update and replace: the old files, which stay until the commit point
    uint64_t margin = kSpaceMarginBytes;
    uint64_t total() const { return files + staging + backup + record + journal + directories + old_version + margin; }
};

struct SpaceInput {
    uint64_t cluster = 4096;
    std::vector<uint64_t> new_file_sizes;       // files that will be placed
    std::vector<uint64_t> old_file_sizes;       // files of the old version (update, replace); empty otherwise
    uint64_t desktop_config_size = 0;           // bytes of view.json now (the backup) when it changes, else 0
    uint64_t new_desktop_config_size = 0;       // bytes of the prepared view.json when it changes, else 0
    uint64_t record_size = 0;                   // bytes of the install record that will be written, 0 if none
    uint64_t intent_size = 0;                   // bytes of the journal intent (an estimate is fine)
    uint64_t directories_to_create = 0;
};

SpaceParts space_needed(const SpaceInput& in);

}  // namespace install
}  // namespace pbstore
