#include "pbstore/install/confirmations.h"

namespace pbstore {
namespace install {

namespace {
struct Named {
    Confirmation c;
    const char* name;
};
const Named kNames[] = {
    {Confirmation::Plan, "plan"},
    {Confirmation::EditedFiles, "edited-files"},
    {Confirmation::UnknownFirmware, "unknown-firmware"},
    {Confirmation::UnknownModel, "unknown-model"},
    {Confirmation::ExistingDataFolder, "existing-data-folder"},
    {Confirmation::ReplaceOutside, "replace"},
    {Confirmation::Repair, "repair"},
    {Confirmation::DeleteData1, "delete-data-1"},
    {Confirmation::DeleteData2, "delete-data-2"},
};
}  // namespace

const char* confirmation_name(Confirmation c) {
    for (const Named& n : kNames)
        if (n.c == c) return n.name;
    return "?";
}

bool confirmation_from_name(const std::string& name, Confirmation* out) {
    for (const Named& n : kNames)
        if (name == n.name) {
            *out = n.c;
            return true;
        }
    return false;
}

}  // namespace install
}  // namespace pbstore
