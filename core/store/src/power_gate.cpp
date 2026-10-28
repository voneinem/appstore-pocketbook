#include "pbstore/store/power_gate.h"

#include "pbstore/store/store_rules.h"

namespace pbstore {
namespace store {

PowerDecision decide_power(PowerOperation op, std::optional<int> level, std::optional<bool> charging) {
    PowerDecision d;
    d.level = level;
    if (op == PowerOperation::Repair) d.minimum = kMinBatteryRepair;
    else d.minimum = charging.value_or(false) ? kMinBatteryCharging : kMinBatteryPercent;
    if (!level) d.result = PowerResult::NeedsRiskConfirmation;
    else d.result = *level < d.minimum ? PowerResult::Refused : PowerResult::Allowed;
    return d;
}

PowerFinding power_finding(const PowerDecision& d) {
    PowerFinding f;
    if (d.result == PowerResult::Refused)
        f.finding = make_store_finding("E-STO-01", "battery",
                                       "The battery is at " + std::to_string(*d.level) + " %; PB Store needs at least " + std::to_string(d.minimum) + " % to do this.");
    else if (d.result == PowerResult::NeedsRiskConfirmation)
        f.finding = make_store_finding("E-STO-05", "battery", "The battery level cannot be read, so PB Store cannot tell whether there is enough charge.");
    return f;
}

PowerDecision PowerGate::allow(PowerOperation op) { return decide_power(op, battery_.percent(), battery_.charging()); }

}  // namespace store
}  // namespace pbstore
