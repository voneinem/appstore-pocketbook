#include "inkview.h"
#include "platform/pocketbook/inkview_devices.h"

std::optional<int> InkViewBattery::percent() {
    const int level = GetBatteryPower();
    if (level < 0 || level > 100) return std::nullopt;   // not a percentage: unreadable
    return level;
}

std::optional<bool> InkViewBattery::charging() {
    const int charging = IsCharging();
    if (charging < 0) return std::nullopt;
    return charging > 0;
}
