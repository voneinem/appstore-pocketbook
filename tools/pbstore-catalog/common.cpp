#include "common.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace pbcli {

namespace {
const std::set<std::string>& bool_flags() {
    static const std::set<std::string> flags = {"github", "first-publish", "dry-run", "help"};
    return flags;
}
std::map<std::string, std::pair<std::string, CommandFn>>& mutable_commands() {
    static std::map<std::string, std::pair<std::string, CommandFn>> table;
    return table;
}
}  // namespace

Args parse_args(const std::vector<std::string>& argv) {
    Args a;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        if (s.rfind("--", 0) != 0 || s == "--") {
            a.positional.push_back(s);
            continue;
        }
        std::string name = s.substr(2), value;
        const size_t eq = name.find('=');
        if (eq != std::string::npos) {
            value = name.substr(eq + 1);
            name = name.substr(0, eq);
            if (bool_flags().count(name)) {
                a.error = "option --" + name + " takes no value";
                return a;
            }
            a.values[name] = value;
        } else if (bool_flags().count(name)) {
            a.flags.insert(name);
        } else {
            if (i + 1 >= argv.size()) {
                a.error = "option --" + name + " needs a value";
                return a;
            }
            a.values[name] = argv[++i];
        }
    }
    return a;
}

void report(const std::vector<pbstore::Finding>& findings, const Args& args, const std::string& annotation_file) {
    for (const auto& f : findings) {
        std::cerr << pbstore::format_finding(f) << "\n";
        if (args.flag("github")) std::cout << pbstore::github_annotation(f, annotation_file) << "\n";
    }
}

int exit_code(const std::vector<pbstore::Finding>& findings) {
    for (const auto& f : findings)
        if (f.severity == pbstore::Severity::Error) return 1;
    return 0;
}

int usage_error(const std::string& message) {
    std::cerr << "error: " << message << "\n";
    return 2;
}

CommandRegistrar::CommandRegistrar(const std::string& name, const std::string& summary, CommandFn fn) {
    mutable_commands()[name] = {summary, std::move(fn)};
}

const std::map<std::string, std::pair<std::string, CommandFn>>& commands() { return mutable_commands(); }

bool read_file(const std::string& path, std::string* out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool write_file(const std::string& path, const std::string& data, std::string* error) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f << data;
    f.flush();
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    return true;
}

}  // namespace pbcli
