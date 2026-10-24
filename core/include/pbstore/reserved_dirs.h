#pragma once
// Names under system/ that an app may not claim as its data folder. The list is the file
// catalog/reserved-data-dirs.txt, compiled into the library at configure time.

#include <string>
#include <vector>

namespace pbstore {

const std::vector<std::string>& reserved_data_dirs();
// `name` is the part after "system/".
bool is_reserved_data_dir(const std::string& name);

}  // namespace pbstore
