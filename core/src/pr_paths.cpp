#include "pbstore/pr_paths.h"

#include <algorithm>

namespace pbstore {

namespace {

// Lexical normalization: removes "." and resolves ".." ("" if the path climbs out of the repository).
std::string normalize(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string seg = path.substr(start, end - start);
        if (seg == "..") {
            if (parts.empty()) return "";          // escapes the repository
            parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        start = end + 1;
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool is_entry_file(const std::string& p) {
    static const std::string prefix = "catalog/entries/";
    if (!starts_with(p, prefix) || p.size() <= prefix.size() + 5) return false;
    const std::string rest = p.substr(prefix.size());
    return rest.find('/') == std::string::npos && rest.compare(rest.size() - 5, 5, ".json") == 0;
}

bool is_sensitive(const std::string& p) {
    for (const char* dir : {"core/", "tools/", "scripts/", "tests/", ".github/", "keys/", "third_party/"})
        if (starts_with(p, dir)) return true;
    return p == "CMakeLists.txt" || p == "catalog/accepted-licenses.txt" || p == "catalog/reserved-data-dirs.txt";
}

}  // namespace

std::vector<Finding> check_pr_paths(const std::vector<std::string>& changed_paths) {
    bool entries = false;
    std::vector<std::string> sensitive;
    for (const std::string& raw : changed_paths) {
        const std::string p = normalize(raw);
        if (p.empty() && !raw.empty() && raw != "." && raw != "/") {
            sensitive.push_back(raw);              // a path that climbs out of the repository is never harmless
            continue;
        }
        if (p.empty()) continue;
        if (is_entry_file(p)) entries = true;
        else if (is_sensitive(p)) sensitive.push_back(p);
    }
    std::vector<Finding> out;
    if (!entries || sensitive.empty()) return out;
    std::string list;
    for (size_t i = 0; i < sensitive.size() && i < 10; ++i) list += (i ? ", " : "") + sensitive[i];
    out.push_back(make_notice("W-PR-01", "pull request",
        "this pull request changes entry files together with " + std::to_string(sensitive.size()) + " code, workflow, key or list file(s) (" + list +
        (sensitive.size() > 10 ? ", ..." : "") + "); the check ran with the pull request's own code, so the reviewer must read those changes"));
    return out;
}

}  // namespace pbstore
