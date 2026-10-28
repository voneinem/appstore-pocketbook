#pragma once
// The reader's implementations of the store's device interfaces over InkView (contracts/device-interfaces.md). Each function is one
// InkView call plus the conversions the contract names; the .cpp files are the only code in the store besides the probe that includes
// inkview.h (scripts/check-constitution.sh R18), and they contain no install logic (R19). They are compiled for the reader with the SDK
// and for the tests against the host shim tests/support/inkview_shim/inkview.h.

#include <memory>
#include <optional>

#include "pbstore/store/battery.h"
#include "pbstore/store/identity_parse.h"
#include "pbstore/store/radio.h"
#include "pbstore/store/screen.h"

// Wi-Fi: QueryNetwork, GetWiFiPowerStatus, NetConnectAsync, NetDisconnect. SetFlightMode is not used (it changes a setting the user owns).
class InkViewRadio : public pbstore::store::Radio {
public:
    std::optional<bool> is_on() override;
    bool is_connected() override;
    bool connect() override;
    bool disconnect() override;
    int last_error() override;
};

// GetBatteryPower (0..100) and IsCharging.
class InkViewBattery : public pbstore::store::Battery {
public:
    std::optional<int> percent() override;
    std::optional<bool> charging() override;
};

// FullUpdate.
class InkViewScreen : public pbstore::store::Screen {
public:
    void full_update() override;
};

// GetDeviceModel and GetSoftwareVersion through the parsers of core/store. The serial number is never read.
std::unique_ptr<pbstore::store::StoreIdentitySource> make_inkview_identity();
