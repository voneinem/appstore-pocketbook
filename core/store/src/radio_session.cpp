#include "pbstore/store/radio_session.h"

#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

std::optional<Finding> RadioSession::begin(const Cancelled& cancelled) {
    ended_ = false;
    const std::optional<bool> was_on = radio_.is_on();
    if (!was_on || *was_on) {   // already on, or unreadable: counted as on, left alone (and a marker of an earlier run is not ours to act on)
        if (mark_) mark_(false);
        return std::nullopt;
    }
    if (mark_) mark_(true);     // before the radio is asked: a program cut off from here on leaves the marker
    if (!radio_.connect()) {
        if (mark_) mark_(false);
        return make_store_finding("E-STO-02", "wifi", "No connection could be made: the reader refused to switch Wi-Fi on.");
    }
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

bool RadioSession::switch_off(Radio& radio, const Sleep& sleep, const Now& now) {
    const int64_t start = now();
    int64_t last_ask = start - kDisconnectRetryMs;   // the first look asks at once
    for (;;) {
        const std::optional<bool> on = radio.is_on();   // what counts is the radio's state, not what disconnect() says
        if (!on) return false;
        if (!*on) return true;
        if (now() - last_ask >= kDisconnectRetryMs) {   // a request that meets the bring-up of the connection is lost on the reader
            radio.disconnect();
            last_ask = now();
        }
        if (now() - start >= static_cast<int64_t>(kOffWaitSeconds) * 1000) return false;
        sleep(kPollMs);
    }
}

std::optional<Finding> RadioSession::end() {
    if (ended_ || !switched_on_) {
        ended_ = true;
        return std::nullopt;
    }
    ended_ = true;
    if (switch_off(radio_, sleep_, now_)) {
        if (mark_) mark_(false);
        return std::nullopt;   // switched off again: nothing to say (the owner found the notice too chatty, 2026-10-29)
    }
    return make_store_finding("W-STO-02", "wifi");   // the marker stays: the launcher tries once more
}

}  // namespace store
}  // namespace pbstore
