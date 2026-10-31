#include "pbstore/store/launch_notice.h"

namespace pbstore {
namespace store {

std::optional<LaunchNotice> launch_notice(std::string_view kind) {
    if (kind == "waiting")
        return LaunchNotice{"PB Store is getting ready",
                            "The reader is still setting itself up after the restart. Please wait: PB Store starts by itself as soon as "
                            "the reader is ready. This can take up to two minutes."};
    if (kind == "gave-up")
        return LaunchNotice{"PB Store could not start yet",
                            "The reader has not finished setting itself up. Nothing is wrong with PB Store. Wait a minute, then tap "
                            "PB Store again. If it still does not start, restart the reader."};
    return std::nullopt;
}

}  // namespace store
}  // namespace pbstore
