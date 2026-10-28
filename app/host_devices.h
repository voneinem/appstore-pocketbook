#pragma once
// Stand-ins for the reader's devices in the host test build (pbstore-app-test), which runs on a computer for the scripted walks and the
// screenshots. The released program never has them: it uses the InkView wrappers of platform/pocketbook.

#include "pbstore/store/battery.h"
#include "pbstore/store/radio.h"
#include "pbstore/store/screen.h"

class HostRadio : public pbstore::store::Radio {
public:
    std::optional<bool> is_on() override { return true; }       // the computer is online: nothing is switched on or off
    bool is_connected() override { return true; }
    bool connect() override { return true; }
    bool disconnect() override { return true; }
    int last_error() override { return 0; }
};

class HostBattery : public pbstore::store::Battery {
public:
    std::optional<int> percent() override { return 80; }
    std::optional<bool> charging() override { return false; }
};

class HostScreen : public pbstore::store::Screen {
public:
    void full_update() override {}
};
