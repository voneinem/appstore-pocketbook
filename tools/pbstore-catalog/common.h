#pragma once
// Shared pieces of the command line: argument parsing, finding output, exit codes.
//   exit 0  success
//   exit 1  validation or verification failure (the content is wrong)
//   exit 2  usage or I/O error (the command could not run)

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbcli {

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;  // --name value / --name=value
    std::set<std::string> flags;                // --github, --first-publish, --dry-run, ...
    std::string error;                          // non-empty if the arguments could not be parsed

    bool flag(const std::string& name) const { return flags.count(name) > 0; }
    bool has(const std::string& name) const { return values.count(name) > 0; }
    std::string value(const std::string& name, const std::string& fallback = "") const {
        auto it = values.find(name);
        return it == values.end() ? fallback : it->second;
    }
};

// Options that never take a value; every other --name takes the next argument.
Args parse_args(const std::vector<std::string>& argv);

// Prints the findings to stderr (one per line); with --github also the workflow annotations to
// stdout. `annotation_file` is the file the annotation points at (may be empty).
void report(const std::vector<pbstore::Finding>& findings, const Args& args, const std::string& annotation_file = "");
// 1 if any finding is an error, else 0.
int exit_code(const std::vector<pbstore::Finding>& findings);
// Prints "error: <message>" to stderr and returns 2.
int usage_error(const std::string& message);

using CommandFn = std::function<int(const Args&)>;
struct CommandRegistrar {
    CommandRegistrar(const std::string& name, const std::string& summary, CommandFn fn);
};
const std::map<std::string, std::pair<std::string, CommandFn>>& commands();

// Reads a whole file; returns false and sets *error if it cannot be read.
bool read_file(const std::string& path, std::string* out, std::string* error);
bool write_file(const std::string& path, const std::string& data, std::string* error);

}  // namespace pbcli
