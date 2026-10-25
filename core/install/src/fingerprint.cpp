#include "pbstore/install/fingerprint.h"

#include <algorithm>
#include <map>
#include <set>

#include "pbstore/sha256.h"

namespace pbstore {
namespace install {

namespace {

constexpr const char* kViewPath = "system/config/desktop/view.json";
constexpr uint64_t kMaxHashedFile = 512ull * 1024 * 1024;
constexpr uint64_t kFreeSpaceStep = 1024 * 1024;

const char* kind_word(Kind k) {
    switch (k) {
        case Kind::None: return "absent";
        case Kind::File: return "file";
        case Kind::Dir: return "dir";
        case Kind::Link: return "link";
        case Kind::Other: return "other";
    }
    return "?";
}

std::string fact_line(const std::string& path, const TargetFact& f) {
    return path + "\t" + kind_word(f.kind) + "\t" + std::to_string(f.size) + "\t" + f.sha256;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t t = line.find('\t', start);
        parts.push_back(line.substr(start, t == std::string::npos ? std::string::npos : t - start));
        if (t == std::string::npos) break;
        start = t + 1;
    }
    return parts;
}

std::string mib(uint64_t bytes) {
    const uint64_t tenths = bytes * 10 / (1024 * 1024);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MiB";
}

}  // namespace

std::string read_view_sha256(ReaderFs& fs) {
    std::string text;
    if (!fs.read_file(kViewPath, 64u << 20, &text)) return "";
    return sha256_hex(text);
}

TargetFact read_fact(ReaderFs& fs, const std::string& path) {
    TargetFact f;
    const Stat st = fs.stat(path);
    f.kind = st.kind;
    f.size = st.size;
    if (st.kind == Kind::File) {
        std::string data;
        f.sha256 = fs.read_file(path, kMaxHashedFile, &data) ? sha256_hex(data) : "unreadable";
    }
    return f;
}

FingerprintBasis make_basis(const std::string& view_sha256, const std::map<std::string, TargetFact>& facts, const std::vector<std::string>& paths,
                            uint64_t free_bytes) {
    FingerprintBasis b;
    b.view_sha256 = view_sha256;
    std::set<std::string> unique(paths.begin(), paths.end());
    for (const std::string& p : unique) {
        auto it = facts.find(p);
        b.facts.push_back(fact_line(p, it == facts.end() ? TargetFact() : it->second));
    }
    b.free_bytes = free_bytes;
    return b;
}

FingerprintBasis read_basis(ReaderFs& fs, const std::vector<std::string>& paths) {
    std::map<std::string, TargetFact> facts;
    for (const std::string& p : paths) facts[p] = read_fact(fs, p);
    return make_basis(read_view_sha256(fs), facts, paths, fs.space().free_bytes);
}

std::string fingerprint_of(const FingerprintBasis& basis) {
    std::string text = "view\t" + basis.view_sha256 + "\n";
    for (const std::string& f : basis.facts) text += f + "\n";
    // the free space counts in whole MiB: a few blocks more or less (the reader or the computer writing a log) are no change the user could see
    text += "free\t" + std::to_string(basis.free_bytes / kFreeSpaceStep) + "\n";
    return sha256_hex(text);
}

std::string current_fingerprint(ReaderFs& fs, const Plan& plan) { return fingerprint_of(read_basis(fs, plan_target_paths(plan))); }

bool free_space_changed(uint64_t planned, uint64_t now) {
    return (planned > now ? planned - now : now - planned) >= kFreeSpaceStep;
}

bool basis_unchanged(const FingerprintBasis& planned, const FingerprintBasis& now) {
    return planned.view_sha256 == now.view_sha256 && planned.facts == now.facts && !free_space_changed(planned.free_bytes, now.free_bytes);
}

std::string describe_basis_change(const FingerprintBasis& planned, const FingerprintBasis& now) {
    std::vector<std::string> changes;
    if (planned.view_sha256 != now.view_sha256) changes.push_back("the desktop configuration (system/config/desktop/view.json) was changed");
    std::map<std::string, std::vector<std::string>> before, after;
    for (const std::string& f : planned.facts) before[split_tab(f)[0]] = split_tab(f);
    for (const std::string& f : now.facts) after[split_tab(f)[0]] = split_tab(f);
    for (const auto& kv : after) {
        auto it = before.find(kv.first);
        if (it == before.end()) continue;
        if (it->second != kv.second) {
            const std::string was = it->second[1], is = kv.second[1];
            if (was == "absent") changes.push_back(kv.first + " appeared");
            else if (is == "absent") changes.push_back(kv.first + " was removed");
            else changes.push_back(kv.first + " was changed");
        }
    }
    if (free_space_changed(planned.free_bytes, now.free_bytes))
        changes.push_back("the free space changed from " + mib(planned.free_bytes) + " to " + mib(now.free_bytes));
    std::string s;
    for (const std::string& c : changes) s += (s.empty() ? "" : "; ") + c;
    return s.empty() ? "something on the reader changed" : s;
}

}  // namespace install
}  // namespace pbstore
