#include "pbstore/install/identity.h"

#include "pbstore/firmware_version.h"

namespace pbstore {
namespace install {

std::vector<IdentitySource*> default_identity_sources() { return {}; }

Identity identify(ReaderFs& fs, const std::vector<IdentitySource*>& sources) {
    Identity id;
    for (IdentitySource* s : sources) {
        if (!id.model) id.model = s->model(fs);
        if (!id.firmware) id.firmware = s->firmware(fs);
    }
    return id;
}

namespace {
bool is_ink_pad_one(const std::string& model) { return model == "1030" || model == "PB1030"; }
}  // namespace

Compatibility check_compatibility(const Identity& identity, const Manifest& manifest) {
    Compatibility c;
    const std::string subject = "reader";
    if (!identity.model) {
        c.requirements.push_back({Confirmation::UnknownModel,
                                  make_error("E-INS-09", subject, "the model of the reader cannot be read, so it cannot be checked that this is an InkPad One")});
    } else if (!is_ink_pad_one(*identity.model)) {
        c.refusals.push_back(make_error("E-INS-08", subject, "the reader reports the model " + *identity.model + ", but only the InkPad One is supported"));
    }
    const auto needed = parse_firmware(manifest.min_firmware);
    const auto have = identity.firmware ? parse_firmware(*identity.firmware) : std::nullopt;
    if (!have) {
        c.requirements.push_back({Confirmation::UnknownFirmware,
                                  make_error("E-INS-11", subject,
                                             "the firmware version of the reader cannot be read, so it cannot be checked against the version " +
                                                 manifest.min_firmware + " that the app needs")});
    } else if (needed && compare(*have, *needed) < 0) {
        c.refusals.push_back(make_error("E-INS-10", subject,
                                        "the reader has firmware " + *identity.firmware + " but the app needs " + manifest.min_firmware + " or newer"));
    }
    return c;
}

}  // namespace install
}  // namespace pbstore
