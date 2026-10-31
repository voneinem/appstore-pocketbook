#include "pbstore/store/link_url.h"

#include <cctype>
#include <string>

namespace pbstore {
namespace store {

bool is_followable_url(std::string_view url) {
    if (url.empty() || url.size() > kMaxLinkBytes) return false;
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7f) return false;   // blanks and control characters (also NUL)
    constexpr std::string_view kScheme = "https://";
    if (url.size() <= kScheme.size()) return false;
    for (size_t i = 0; i < kScheme.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(url[i])) != kScheme[i]) return false;
    const std::string_view rest = url.substr(kScheme.size());
    const size_t end = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, end);
    if (authority.empty() || authority.find('@') != std::string_view::npos) return false;   // no host, or credentials
    return true;
}

}  // namespace store
}  // namespace pbstore
