#pragma once
// Wi-Fi for one user action (spec FR-001, SC-012; research R-06). begin() records whether the radio was on; if it was off it
// asks for a connection and waits up to 30 seconds, polling every 500 ms. end() puts the radio back in its earlier state,
// however the action ended (it asks again for up to 10 seconds if the radio is still on: a disconnect that meets the bring-up of the connection is lost on the reader); it is legal in every state, idempotent, and also runs from the destructor (so an exception
// thrown by the caller cannot leave Wi-Fi on) and from request_leave(). If the earlier state cannot be read the session
// assumes it was on and never switches off: switching off a connection the user needs is worse than leaving one on.
// The clock is injected: tests never wait.

#include <cstdint>
#include <functional>
#include <optional>

#include "pbstore/rules.h"
#include "pbstore/store/radio.h"

namespace pbstore {
namespace store {

constexpr int kConnectSeconds = 30;
constexpr int kPollMs = 500;
constexpr int kOffWaitSeconds = 10;     // end() waits this long for the radio to be off, asking again every kDisconnectRetryMs (a disconnect during the bring-up is lost)
constexpr int kDisconnectRetryMs = 2000;

class RadioSession {
public:
    using Sleep = std::function<void(int ms)>;
    using Now = std::function<int64_t()>;           // milliseconds, monotonic
    using Cancelled = std::function<bool()>;
    // The marker run/wifi-on: set(true) before the store asks for Wi-Fi, set(false) once the radio is off again or was never the store's to switch off.
    // The reader ends the app at the home key without running its code, so the launcher takes Wi-Fi back when it finds the marker.
    using Mark = std::function<void(bool)>;

    RadioSession(Radio& radio, Sleep sleep, Now now, Mark mark = nullptr) : radio_(radio), sleep_(std::move(sleep)), now_(std::move(now)), mark_(std::move(mark)) {}
    ~RadioSession() { end(); }
    RadioSession(const RadioSession&) = delete;
    RadioSession& operator=(const RadioSession&) = delete;

    // Nothing: the connection is ready. E-STO-02 if no connection could be made within 30 seconds, if it was refused at once,
    // or if the user cancelled. After a failure end() must still be called (it also runs from the destructor).
    std::optional<Finding> begin(const Cancelled& cancelled = nullptr);
    // W-STO-02 when the session switched Wi-Fi on and could not switch it off again; nothing otherwise (switching it off again is not announced).
    std::optional<Finding> end();

    bool switched_on_by_store() const { return switched_on_; }

    // For the launcher (pbstore.bin --radio-off): ask for the radio to be off and wait up to kOffWaitSeconds, asking again. True when it was seen off;
    // false when it stayed on or its state cannot be read (a success is never claimed that cannot be seen).
    static bool switch_off(Radio& radio, const Sleep& sleep, const Now& now);

private:
    Radio& radio_;
    Sleep sleep_;
    Now now_;
    Mark mark_;
    bool switched_on_ = false;
    bool ended_ = false;
};

}  // namespace store
}  // namespace pbstore
