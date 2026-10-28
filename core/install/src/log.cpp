#include "pbstore/install/log.h"

#include <cctype>

#include "pbstore/install/text_safety.h"

namespace pbstore {
namespace install {

namespace {

bool is_word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

std::string replace_all(std::string text, const std::string& what, const std::string& with) {
    if (what.empty()) return text;
    size_t at = 0;
    while ((at = text.find(what, at)) != std::string::npos) {
        text.replace(at, what.size(), with);
        at += with.size();
    }
    return text;
}

std::string replace_word(std::string text, const std::string& word, const std::string& with) {
    if (word.empty()) return text;
    size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        const bool left = at == 0 || !is_word_char(text[at - 1]);
        const size_t end = at + word.size();
        const bool right = end >= text.size() || !is_word_char(text[end]);
        if (left && right) {
            text.replace(at, word.size(), with);
            at += with.size();
        } else {
            at = end;
        }
    }
    return text;
}

}  // namespace

std::string Redactor::clean_url(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return url;
    const size_t auth_begin = scheme + 3;
    const size_t auth_end = url.find_first_of("/?#", auth_begin);
    std::string authority = url.substr(auth_begin, auth_end == std::string::npos ? std::string::npos : auth_end - auth_begin);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    std::string rest = auth_end == std::string::npos ? std::string() : url.substr(auth_end);
    const size_t cut = rest.find_first_of("?#");
    if (cut != std::string::npos) rest.erase(cut);
    return url.substr(0, auth_begin) + authority + rest;
}

std::string Redactor::redact(const std::string& text) const {
    // 1. addresses lose credentials and query strings
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const size_t http = text.find("http", i);
        if (http == std::string::npos) {
            out.append(text, i, std::string::npos);
            break;
        }
        const bool url = text.compare(http, 8, "https://") == 0 || text.compare(http, 7, "http://") == 0;
        if (!url) {
            out.append(text, i, http + 4 - i);
            i = http + 4;
            continue;
        }
        out.append(text, i, http - i);
        size_t end = http;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '"' && text[end] != '\'' && text[end] != ')' && text[end] != '>') ++end;
        out += clean_url(text.substr(http, end - http));
        i = end;
    }
    // 2. the longest personal strings first (a local file lies below the home folder)
    out = replace_all(out, file_, "<file>");
    out = replace_all(out, mount_, "<reader>");
    out = replace_all(out, home_, "<home>");
    out = replace_word(out, label_, "<label>");
    return out;
}

void Log::event(const std::string& action, const std::string& app_id, const std::string& version, const std::string& result,
                const std::vector<std::string>& rule_ids, const std::string& text) {
    std::string line = clock_.now().iso() + " " + neutralize(action) + " " + (app_id.empty() ? "-" : neutralize(app_id)) + " " + (version.empty() ? "-" : neutralize(version)) +
                       " " + neutralize(result);
    for (const std::string& id : rule_ids) line += " " + neutralize(id);
    if (!text.empty()) line += " " + neutralize(redactor_.redact(text));   // also replaces line breaks: one line per event
    line += "\n";
    const std::string& name = file_name_;
    const uint64_t size = dirs_.size(name);
    if (size != UINT64_MAX && size + line.size() > kMaxFileBytes) {
        dirs_.remove(name + ".2");
        if (dirs_.size(name + ".1") != UINT64_MAX) dirs_.rename(name + ".1", name + ".2");
        dirs_.rename(name, name + ".1");
    }
    dirs_.append(name, line);
}

}  // namespace install
}  // namespace pbstore
