#include "pbstore/store/store_text.h"

#include <cstdint>
#include <vector>

namespace pbstore {
namespace store {

namespace {

// Decodes one UTF-8 character at s[i]; returns its length in bytes, or 0 if the bytes are not valid.
size_t decode(const std::string& s, size_t i, uint32_t* cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = 0;
    uint32_t v = 0;
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { len = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; v = c & 0x07; }
    else return 0;
    if (i + len > s.size()) return 0;
    for (size_t k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;
        v = (v << 6) | (cc & 0x3F);
    }
    // no overlong forms, no surrogates, nothing above U+10FFFF
    if ((len == 2 && v < 0x80) || (len == 3 && v < 0x800) || (len == 4 && v < 0x10000) || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF) return 0;
    *cp = v;
    return len;
}

bool invisible_or_direction(uint32_t cp) {
    return (cp >= 0x80 && cp <= 0x9F) ||                  // the C1 controls
           (cp >= 0x202A && cp <= 0x202E) ||              // embeddings and overrides
           (cp >= 0x2066 && cp <= 0x2069) ||              // isolates
           cp == 0x200E || cp == 0x200F || cp == 0x061C || // direction marks
           cp == 0x2028 || cp == 0x2029 || cp == 0xFEFF;  // line and paragraph separators, byte order mark
}

}  // namespace

std::string store_text(const std::string& text, size_t max_chars) {
    // 1. the characters that may stay, as UTF-8 strings
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1B) {   // a terminal escape sequence is removed whole
            ++i;
            if (i < text.size() && text[i] == '[') {            // CSI: up to the final byte 0x40..0x7E
                ++i;
                while (i < text.size() && !(static_cast<unsigned char>(text[i]) >= 0x40 && static_cast<unsigned char>(text[i]) <= 0x7E)) ++i;
                if (i < text.size()) ++i;
            } else if (i < text.size() && text[i] == ']') {     // OSC: up to BEL or ESC \ .
                ++i;
                while (i < text.size() && text[i] != '\x07' && !(text[i] == '\x1B' && i + 1 < text.size() && text[i + 1] == '\\')) ++i;
                if (i < text.size()) i += (text[i] == '\x07') ? 1 : 2;
            }
            continue;
        }
        if (c < 0x20 || c == 0x7F) { ++i; continue; }
        uint32_t cp = 0;
        const size_t len = decode(text, i, &cp);
        if (len == 0) { chars.push_back("?"); ++i; continue; }   // not valid UTF-8
        if (!invisible_or_direction(cp)) chars.push_back(text.substr(i, len));
        i += len;
    }
    // 2. the limit
    if (max_chars == 0) return "";
    std::string out;
    if (chars.size() <= max_chars) {
        for (const std::string& ch : chars) out += ch;
        return out;
    }
    for (size_t k = 0; k + 1 < max_chars; ++k) out += chars[k];
    return out + "\xE2\x80\xA6";
}

}  // namespace store
}  // namespace pbstore
