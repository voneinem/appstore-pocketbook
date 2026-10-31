#pragma once
// Wi-Fi as the store sees it (contracts/device-interfaces.md). The reader's implementation is in
// platform/pocketbook/radio.cpp; RadioSession is the only caller. Qt-free and SDK-free.

#include <optional>

namespace pbstore {
namespace store {

class Radio {
public:
    virtual ~Radio() = default;
    virtual std::optional<bool> is_on() = 0;   // the radio is powered (nullopt: cannot be read)
    virtual bool is_connected() = 0;           // associated and online
    virtual bool connect() = 0;                // start a connection without waiting; false if refused at once
    virtual bool disconnect() = 0;             // switch off what connect() switched on; idempotent
    virtual int last_error() = 0;              // 0, or a platform code; never shown raw
};

}  // namespace store
}  // namespace pbstore
