#pragma once
// Model and firmware of the reader from the strings InkView gives (GetDeviceModel, GetSoftwareVersion). The Qt-free
// part parses them; platform/pocketbook/identity.cpp passes the InkView calls in. Text that does not parse gives
// nothing, so the installer's confirmations for an unknown model or firmware apply (E-INS-09, E-INS-11).
// What the real strings look like is the device task DS-04; until then only the plain forms are accepted.

#include <functional>
#include <optional>
#include <string>

#include "pbstore/install/identity.h"

namespace pbstore {
namespace store {

std::optional<std::string> parse_model(const std::string& text);      // for example "1030"
std::optional<std::string> parse_firmware(const std::string& text);   // for example "6.11.1437"

class StoreIdentitySource : public install::IdentitySource {
public:
    using Getter = std::function<std::string()>;
    StoreIdentitySource(Getter model, Getter firmware) : model_(std::move(model)), firmware_(std::move(firmware)) {}
    std::optional<std::string> model(install::ReaderFs&) override { return model_ ? parse_model(model_()) : std::nullopt; }
    std::optional<std::string> firmware(install::ReaderFs&) override { return firmware_ ? parse_firmware(firmware_()) : std::nullopt; }

private:
    Getter model_, firmware_;
};

}  // namespace store
}  // namespace pbstore
