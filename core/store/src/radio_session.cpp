#include "pbstore/store/radio_session.h"

#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

std::optional<Finding> RadioSession::begin(const Cancelled& cancelled) {
    ended_ = false;
    const std::optional<bool> was_on = radio_.is_on();
    if (!was_on || *was_on) return std::nullopt;   // already on, or unreadable: counted as on, left alone
    if (!radio_.connect())
        return make_store_finding("E-STO-02", "wifi", "No connection could be made: the reader refused to switch Wi-Fi on.");
    switched_on_ = true;
    const int64_t start = now_();
    while (!radio_.is_connected()) {
        if (cancelled && cancelled())
            return make_store_finding("E-STO-02", "wifi", "No connection was made: the action was cancelled while waiting for Wi-Fi.");
        if (now_() - start >= static_cast<int64_t>(kConnectSeconds) * 1000)
            return make_store_finding("E-STO-02", "wifi", "No connection could be made within " + std::to_string(kConnectSeconds) + " seconds.");
        sleep_(kPollMs);
    }
    return std::nullopt;
}

std::optional<Finding> RadioSession::end() {
    if (ended_ || !switched_on_) {
        ended_ = true;
        return std::nullopt;
    }
    ended_ = true;
    radio_.disconnect();
    const std::optional<bool> now = radio_.is_on();   // what counts is the radio's state, not what disconnect() says
    if (now && !*now) return make_store_finding("W-STO-03", "wifi");
    return make_store_finding("W-STO-02", "wifi");
}

}  // namespace store
}  // namespace pbstore
