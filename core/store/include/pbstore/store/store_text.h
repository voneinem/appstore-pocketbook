#pragma once
// Text that comes from a catalog or a package (names, descriptions, file names) is untrusted. It is shown as plain text on every
// screen and in the log: control characters (also line breaks and tabs), terminal escape sequences and invisible direction controls
// are removed, text that is not valid UTF-8 is replaced by "?", and the text is cut at the limits of the catalog contract with an
// ellipsis (spec FR-011).

#include <cstddef>
#include <string>

namespace pbstore {
namespace store {

constexpr size_t kMaxNameChars = 40;
constexpr size_t kMaxShortDescriptionChars = 120;
constexpr size_t kMaxLongDescriptionChars = 2000;
constexpr size_t kMaxAuthorChars = 80;

// `max_chars` counts characters, not bytes; when the text is longer the last character is an ellipsis (U+2026).
std::string store_text(const std::string& text, size_t max_chars);

}  // namespace store
}  // namespace pbstore
