#pragma once
// The named consents a plan can require. Each is given separately; there is no "yes to all".

#include <set>
#include <string>

namespace pbstore {
namespace install {

enum class Confirmation {
    Plan,                // always: the plan as shown
    EditedFiles,         // E-INS-32: recorded files were changed since the install
    UnknownFirmware,     // E-INS-11
    UnknownModel,        // E-INS-09
    ExistingDataFolder,  // E-INS-17
    ReplaceOutside,      // E-INS-38: an app installed outside the installer would be replaced
    Repair,              // E-INS-23: finish or undo an interrupted operation
    DeleteData1,         // the data folder as shown
    DeleteData2,         // the number of files shown
};

using Confirmations = std::set<Confirmation>;

// Stable names used by the assistant's interface and in messages: "plan", "edited-files", ...
const char* confirmation_name(Confirmation c);
bool confirmation_from_name(const std::string& name, Confirmation* out);

}  // namespace install
}  // namespace pbstore
