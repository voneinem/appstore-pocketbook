#pragma once
// What the store's log and messages may not hold (research R-20, spec SC-010): the installer's Redactor already replaces the mount path,
// the volume label, a local file, the home folder and the credentials and query string of an address. The store adds anything that is shaped
// like an IPv4 or IPv6 address, a hardware address or an e-mail address, and the strings it is told about (the user name, the name of a
// Wi-Fi network, a device serial number). The store never reads the last two; they are replaced if they ever appear.

#include <string>
#include <vector>

#include "pbstore/install/log.h"

namespace pbstore {
namespace store {

// The shape-based part alone: `<address>`, `<hardware address>`, `<e-mail address>`. Text without such a shape is returned unchanged.
std::string store_redact(const std::string& text);

class StoreRedactor : public install::Redactor {
public:
    void set_user_name(const std::string& name) { user_ = name.size() >= 3 ? name : std::string(); }   // a short name ("pb", "app") would hide ordinary words
    // A string that must never appear (a Wi-Fi name, a serial number). Strings of less than 3 characters are ignored.
    void add_secret(const std::string& secret);

    std::string redact(const std::string& text) const override;

private:
    std::string user_;
    std::vector<std::string> secrets_;
};

}  // namespace store
}  // namespace pbstore
