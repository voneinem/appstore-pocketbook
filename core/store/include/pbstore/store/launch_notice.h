#pragma once
// The two notices the launcher asks the program to show when the system refuses the GPU after a restart (contracts/launcher.md S7).
// The store's own screen cannot start then, so the program shows the text with InkView alone (platform/pocketbook/notice.cpp) and ends.

#include <optional>
#include <string>
#include <string_view>

namespace pbstore {
namespace store {

struct LaunchNotice {
    std::string title;
    std::string text;
};

// kind is "waiting" (the first refusal: the launcher keeps trying) or "gave-up" (the final timeout: the user acts). Any other kind: none.
std::optional<LaunchNotice> launch_notice(std::string_view kind);

}  // namespace store
}  // namespace pbstore
