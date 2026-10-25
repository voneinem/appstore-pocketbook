#pragma once
// Text from a catalog or a package (names, titles, descriptions, file names) is shown as plain text. Control
// characters, terminal escape sequences, invisible direction overrides, NUL and bytes that are not valid
// UTF-8 are replaced by "?" in every output and in the log (spec FR-035).

#include <string>

namespace pbstore {
namespace install {

// With `keep_line_breaks` a line feed and a tab are kept (for text that is laid out in lines).
std::string neutralize(const std::string& text, bool keep_line_breaks = false);

}  // namespace install
}  // namespace pbstore
