#include "pbstore/store/store_redact.h"

#include <cctype>
#include <regex>

namespace pbstore {
namespace store {

namespace {

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

std::string replace_all(std::string text, const std::string& from, const std::string& with) {
    if (from.empty()) return text;
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), with);
        at += with.size();
    }
    return text;
}

std::string replace_word(std::string text, const std::string& word, const std::string& with) {
    if (word.empty()) return text;
    size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        const size_t end = at + word.size();
        const bool left = at == 0 || !word_char(text[at - 1]);
        const bool right = end >= text.size() || !word_char(text[end]);
        if (left && right) {
            text.replace(at, word.size(), with);
            at += with.size();
        } else {
            at = end;
        }
    }
    return text;
}

bool alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// An address is not part of a longer word or number: before it no letter, digit, colon or dot; after it no letter or digit, and a colon or
// dot only as punctuation (the sentence goes on) or, for an IPv4 address, as the colon of a port.
bool bounded(const std::string& t, size_t at, size_t end, bool port_ok) {
    if (at > 0 && (alnum(t[at - 1]) || t[at - 1] == ':' || t[at - 1] == '.')) return false;
    if (end >= t.size()) return true;
    const char n = t[end];
    if (alnum(n)) return false;
    if (n == ':' || n == '.') {
        if (end + 1 >= t.size() || !alnum(t[end + 1])) return true;
        return port_ok && n == ':' && std::isdigit(static_cast<unsigned char>(t[end + 1]));
    }
    return true;
}

// Replaces the matches of `re` that `accept` lets through.
template <class Accept>
std::string replace_matches(const std::string& text, const std::regex& re, const std::string& with, Accept accept) {
    std::string out;
    size_t last = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
        const size_t at = static_cast<size_t>(it->position(0));
        const size_t end = at + static_cast<size_t>(it->length(0));
        if (!accept(at, end, it->str(0))) continue;
        out.append(text, last, at - last);
        out += with;
        last = end;
    }
    out.append(text, last, std::string::npos);
    return out;
}

}  // namespace

std::string store_redact(const std::string& text) {
    static const std::regex mail("[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+");
    static const std::regex mac("([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}");
    static const std::regex ipv4("(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}");
    // eight groups, or fewer with one "::" somewhere; a time such as 13:09:00 has neither
    static const std::regex ipv6_full("([0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}");
    static const std::regex ipv6_short("(([0-9A-Fa-f]{1,4}:){1,7}|:)(:[0-9A-Fa-f]{1,4}){1,7}|([0-9A-Fa-f]{1,4}:){1,7}:");
    std::string out = replace_matches(text, mail, "<e-mail address>", [](size_t, size_t, const std::string&) { return true; });
    auto strict = [&out](size_t at, size_t end, const std::string&) { return bounded(out, at, end, false); };
    out = replace_matches(out, mac, "<hardware address>", strict);
    out = replace_matches(out, ipv6_full, "<address>", strict);
    out = replace_matches(out, ipv6_short, "<address>", [&out](size_t at, size_t end, const std::string& m) {
        return m.find("::") != std::string::npos && m.size() >= 3 && bounded(out, at, end, false);
    });
    out = replace_matches(out, ipv4, "<address>", [&out](size_t at, size_t end, const std::string&) { return bounded(out, at, end, true); });
    return out;
}

void StoreRedactor::add_secret(const std::string& secret) {
    if (secret.size() >= 3) secrets_.push_back(secret);
}

std::string StoreRedactor::redact(const std::string& text) const {
    std::string out = install::Redactor::redact(text);
    out = store_redact(out);
    for (const std::string& s : secrets_) out = replace_all(out, s, "<hidden>");
    return replace_word(out, user_, "<user>");
}

}  // namespace store
}  // namespace pbstore
