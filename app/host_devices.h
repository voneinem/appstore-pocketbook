#pragma once
// Stand-ins for the reader's devices in the host test build (pbstore-app-test), which runs on a computer for the scripted walks and the
// screenshots. The released program never has them: it uses the InkView wrappers of platform/pocketbook.

#include <cstdlib>
#include <string>

#include "pbstore/install/net.h"
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
    std::optional<int> percent() override {
#ifdef PBSTORE_TEST_BUILD
        // 80 % unless the walk says otherwise: the variable is a number of percent, or "unreadable" (the level cannot be read)
        const char* v = std::getenv("PBSTORE_TEST_BATTERY");
        if (v && *v) {
            if (std::string(v) == "unreadable") return std::nullopt;
            return std::atoi(v);
        }
#endif
        return 80;
    }
    std::optional<bool> charging() override { return false; }
};

#ifdef PBSTORE_TEST_BUILD
// The test build can map one host to another (the variable is host=host:port, for example github.com=127.0.0.1:4443), so that the package addresses
// of the demo catalog reach the local demo server (the same hook as the installer's test build).
inline pbstore::install::Downloader apply_test_host_map(pbstore::install::Downloader inner) {
    const char* spec = std::getenv("PBSTORE_TEST_HOST_MAP");
    if (!spec) return inner;
    const std::string s = spec;
    const size_t eq = s.find('=');
    if (eq == std::string::npos) return inner;
    const std::string from = "https://" + s.substr(0, eq), to = "https://" + s.substr(eq + 1);
    return [inner, from, to](const std::string& url, uint64_t max, long secs, const pbstore::install::DownloadProgress& p) {
        if (url.compare(0, from.size(), from) == 0) return inner(to + url.substr(from.size()), max, secs, p);
        return inner(url, max, secs, p);
    };
}
#endif

class HostScreen : public pbstore::store::Screen {
public:
    void full_update() override {}
};
