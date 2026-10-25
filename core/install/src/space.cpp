#include "pbstore/install/space.h"

namespace pbstore {
namespace install {

uint64_t round_to_clusters(uint64_t bytes, uint64_t cluster) {
    if (bytes == 0) return 0;
    if (cluster == 0) return bytes;
    return (bytes + cluster - 1) / cluster * cluster;
}

SpaceParts space_needed(const SpaceInput& in) {
    SpaceParts p;
    const uint64_t c = in.cluster;
    for (uint64_t s : in.new_file_sizes) p.files += round_to_clusters(s, c);
    p.staging = p.files;   // every new file is staged in the journal area before it is placed
    for (uint64_t s : in.old_file_sizes) p.old_version += round_to_clusters(s, c);
    p.backup = round_to_clusters(in.desktop_config_size, c);
    p.record = 2 * round_to_clusters(in.record_size, c);   // the record and its staged copy
    p.journal = round_to_clusters(in.intent_size, c) + 2 * c + round_to_clusters(in.new_desktop_config_size, c);
    p.directories = in.directories_to_create * c;
    p.margin = kSpaceMarginBytes;
    return p;
}

}  // namespace install
}  // namespace pbstore
