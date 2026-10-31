#include <string>

#include "inkview.h"
#include "platform/pocketbook/inkview_devices.h"

std::unique_ptr<pbstore::store::StoreIdentitySource> make_inkview_identity() {
    return std::make_unique<pbstore::store::StoreIdentitySource>(
        [] {
            const char* model = GetDeviceModel();
            return std::string(model ? model : "");
        },
        [] {
            const char* firmware = GetSoftwareVersion();
            return std::string(firmware ? firmware : "");
        });
}
