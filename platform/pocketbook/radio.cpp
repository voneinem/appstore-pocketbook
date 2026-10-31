#include <atomic>
#include <climits>
#include <cstdio>

#include "inkview.h"
#include "platform/pocketbook/inkview_devices.h"

namespace {

// The system calls this when NetConnectAsync has an answer (a NET_* status). Until then there is none.
constexpr int kNoAnswer = INT_MIN;
std::atomic<int> g_connect_status{kNoAnswer};

int connect_done(int status) {
    g_connect_status.store(status);
    return 0;
}

}  // namespace

std::optional<bool> InkViewRadio::is_on() {
    const int power = GetWiFiPowerStatus();
    std::fprintf(stderr, "[pbstore] radio power status %d\n", power);   // device task DS-13: what the radio says at each look
    if (power < 0) return std::nullopt;   // an error value: unreadable
    return power > 0;
}

bool InkViewRadio::is_connected() { return (QueryNetwork() & NET_CONNECTED) != 0; }

bool InkViewRadio::connect() {
    g_connect_status.store(kNoAnswer);
    std::fprintf(stderr, "[pbstore] radio connect asked\n");
    return NetConnectAsync(&connect_done) == 0;   // the SDK says it always returns 0; the answer comes through the callback
}

bool InkViewRadio::disconnect() {
    const int answer = NetDisconnect();
    std::fprintf(stderr, "[pbstore] radio disconnect asked, answer %d\n", answer);
    return answer == NET_OK;
}

int InkViewRadio::last_error() {
    const int status = g_connect_status.load();
    return (status == kNoAnswer || status == NET_OK) ? 0 : status;
}
