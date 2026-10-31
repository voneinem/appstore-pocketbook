#pragma once
// The one address the store may hand to the reader's browser, and only after a tap and a confirmation (spec FR-011, FR-043; PB Notes does the
// same with `is_followable_url`). Qt-free and SDK-free.

#include <string_view>

namespace pbstore {
namespace store {

constexpr size_t kMaxLinkBytes = 2048;

// https only, one plain line (no blank, no control character), a host, no credentials in the address, at most kMaxLinkBytes.
bool is_followable_url(std::string_view url);

}  // namespace store
}  // namespace pbstore
