#include "pbstore/reserved_dirs.h"

#include <algorithm>

namespace pbstore {

const std::vector<std::string>& reserved_data_dirs() {
    static const std::vector<std::string> names = {
#include "reserved_dirs_data.inc"
    };
    return names;
}

bool is_reserved_data_dir(const std::string& name) {
    const auto& names = reserved_data_dirs();
    return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace pbstore
