#include "pbstore/install/text_safety.h"

namespace pbstore {
namespace install {

std::string neutralize(const std::string& text, bool keep_line_breaks) {
    std::string out;
    out.reserve(text.size());
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == '\n' || c == '\t') out.push_back(keep_line_breaks ? static_cast<char>(c) : '?');
            else if (c < 0x20 || c == 0x7f) out.push_back('?');
            else out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        // the length of the UTF-8 sequence and the code point
        size_t len = 0;
        unsigned cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        bool valid = len > 0 && i + len <= n;
        for (size_t k = 1; valid && k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if (valid) {
            const unsigned minimum = len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
            if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) valid = false;
        }
        const bool c1 = valid && cp >= 0x80 && cp <= 0x9F;                                   // controls of the C1 range
        const bool bidi = valid && ((cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0x200E || cp == 0x200F || cp == 0x061C);
        const bool invisible = valid && (cp == 0x2028 || cp == 0x2029 || cp == 0xFEFF);
        if (!valid || c1 || bidi || invisible) {
            out.push_back('?');
            i += valid ? len : 1;
            continue;
        }
        out.append(text, i, len);
        i += len;
    }
    return out;
}

}  // namespace install
}  // namespace pbstore
