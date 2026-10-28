#include "pbstore/store/pending_note.h"

#include <cctype>
#include <vector>

#include "pbstore/install/text_safety.h"
#include "pbstore/store/state_names.h"
#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

namespace {

bool plain_token(const std::string& t) {
    if (t.empty()) return false;
    for (unsigned char c : t)
        if (std::isspace(c)) return false;
    return true;
}

}  // namespace

bool PendingNote::write(const NoteContent& n) {
    const std::string from = n.from.empty() ? "-" : n.from, to = n.to.empty() ? "-" : n.to;
    for (const std::string* t : {&n.kind, &n.app_id, &from, &to})
        if (!plain_token(*t)) return false;
    const std::string line = n.kind + " " + n.app_id + " " + from + " " + to + "\n";
    if (line.size() > limit_of(kPendingNoteName)) return false;
    return dirs_.write_atomic(kPendingNoteName, line);
}

void PendingNote::clear() { dirs_.remove(kPendingNoteName); }

std::optional<Finding> PendingNote::at_start(bool journal_present) {
    const uint64_t size = dirs_.size(kPendingNoteName);
    if (size == UINT64_MAX) return std::nullopt;
    std::string text;
    const bool readable = size <= limit_of(kPendingNoteName) && dirs_.read(kPendingNoteName, &text);
    dirs_.remove(kPendingNoteName);
    if (!readable || journal_present) return std::nullopt;
    // exactly one line, four parts separated by single spaces
    if (text.empty() || text.back() != '\n' || text.find('\n') != text.size() - 1) return std::nullopt;
    text.pop_back();
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t space = text.find(' ', start);
        parts.push_back(text.substr(start, space == std::string::npos ? std::string::npos : space - start));
        if (space == std::string::npos) break;
        start = space + 1;
    }
    if (parts.size() != 4) return std::nullopt;
    for (const std::string& p : parts)
        if (p.empty()) return std::nullopt;
    const std::string kind = install::neutralize(parts[0]), app = install::neutralize(parts[1]);
    return make_store_finding("W-STO-01", app, "The last " + kind + " of " + app + " was cancelled, nothing changed.");
}

}  // namespace store
}  // namespace pbstore
