// check-pr-paths (docs/contracts/cli.md): entry files changed together with code or workflow files.
#include <iostream>
#include <sstream>

#include "common.h"
#include "pbstore/pr_paths.h"

namespace {

int cmd_check_pr_paths(const pbcli::Args& a) {
    std::string text, error;
    if (a.has("changed-files")) {
        if (!pbcli::read_file(a.value("changed-files"), &text, &error)) return pbcli::usage_error(error);
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        text = ss.str();
    }
    std::vector<std::string> paths;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) paths.push_back(line);
    }
    pbcli::report(pbstore::check_pr_paths(paths), a);
    return 0;   // notices never change the exit code
}

pbcli::CommandRegistrar reg("check-pr-paths",
    "tell the reviewer when entries change together with code: check-pr-paths [--changed-files FILE] [--github] (paths on stdin otherwise)",
    cmd_check_pr_paths);

}  // namespace
