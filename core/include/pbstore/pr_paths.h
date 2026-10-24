#pragma once
// check-pr-paths: a pull request that changes entry files together with code, workflows, keys or
// the license and reserved-folder lists is validated by its own code. The reviewer is told.

#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbstore {

// `changed_paths` are repository-relative paths as git prints them. Returns W-PR-01 (a notice) or nothing.
std::vector<Finding> check_pr_paths(const std::vector<std::string>& changed_paths);

}  // namespace pbstore
