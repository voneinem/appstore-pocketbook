#pragma once
// The host shim of the PocketBook SDK's inkview.h: declares and stubs only the InkView calls the store's reader wrappers use
// (platform/pocketbook/{radio,battery,screen,identity}.cpp), with scriptable return values and a record of every call, so that the
// wrappers are tested on the computer without the SDK or a reader (constitution Principle VI). The signatures and constants are the
// SDK's (inkview.h of the 6.11 SDK). Only tests include this file (scripts/check-constitution.sh R18 allows tests/).

#include <string>
#include <vector>

#define NET_WIFI 0x0002
#define NET_WIFIREADY 0x0200
#define NET_CONNECTED 0x0f00
#define NET_OK 0

namespace pbtest_inkview {

struct State {
    int query_network = 0;                 // QueryNetwork(): NET_* flags
    int wifi_power = 0;                    // GetWiFiPowerStatus()
    int connect_async_result = 0;          // what NetConnectAsync returns (the SDK says: always 0)
    int disconnect_result = 0;             // NetDisconnect()
    int last_net_error = 0;                // GetLastNetConnectionError()
    int battery = 80;                      // GetBatteryPower()
    int charging = 0;                      // IsCharging()
    std::string model = "1030";            // GetDeviceModel()
    std::string firmware = "6.11.1437";    // GetSoftwareVersion()
    bool model_null = false, firmware_null = false;
    int (*connect_callback)(int status) = nullptr;   // what NetConnectAsync was given
    std::vector<std::string> calls;        // every call, in order
    int count(const std::string& name) const { int n = 0; for (const auto& c : calls) if (c == name) ++n; return n; }
    void reset() { *this = State(); }
};

inline State& state() {
    static State s;
    return s;
}

// What the system does some time after NetConnectAsync: it calls the callback with the result.
inline int finish_connect(int status) { return state().connect_callback ? state().connect_callback(status) : -1; }

}  // namespace pbtest_inkview

inline int QueryNetwork() { pbtest_inkview::state().calls.push_back("QueryNetwork"); return pbtest_inkview::state().query_network; }
inline int GetWiFiPowerStatus(void) { pbtest_inkview::state().calls.push_back("GetWiFiPowerStatus"); return pbtest_inkview::state().wifi_power; }
inline int NetConnectAsync(int (*cb)(int status)) {
    pbtest_inkview::state().calls.push_back("NetConnectAsync");
    pbtest_inkview::state().connect_callback = cb;
    return pbtest_inkview::state().connect_async_result;
}
inline int NetConnectSilent(const char*) { pbtest_inkview::state().calls.push_back("NetConnectSilent"); return NET_OK; }
inline int NetDisconnect() { pbtest_inkview::state().calls.push_back("NetDisconnect"); return pbtest_inkview::state().disconnect_result; }
inline int GetLastNetConnectionError(void) { pbtest_inkview::state().calls.push_back("GetLastNetConnectionError"); return pbtest_inkview::state().last_net_error; }
inline int GetBatteryPower() { pbtest_inkview::state().calls.push_back("GetBatteryPower"); return pbtest_inkview::state().battery; }
inline int IsCharging() { pbtest_inkview::state().calls.push_back("IsCharging"); return pbtest_inkview::state().charging; }
inline void FullUpdate() { pbtest_inkview::state().calls.push_back("FullUpdate"); }
inline char* GetDeviceModel() {
    pbtest_inkview::state().calls.push_back("GetDeviceModel");
    return pbtest_inkview::state().model_null ? nullptr : const_cast<char*>(pbtest_inkview::state().model.c_str());
}
inline char* GetSoftwareVersion() {
    pbtest_inkview::state().calls.push_back("GetSoftwareVersion");
    return pbtest_inkview::state().firmware_null ? nullptr : const_cast<char*>(pbtest_inkview::state().firmware.c_str());
}
