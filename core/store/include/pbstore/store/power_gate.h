#pragma once
// The battery gate (research R-07): a write refuses below 25 % on battery, 10 % while charging and 10 % for a repair (a repair
// finishes or undoes a half-done operation, and refusing it at a low level would leave the reader worse off). A level that
// cannot be read needs an explicit tap that states the risk. The check runs when the plan is shown and again immediately
// before the first write (spec FR-016). A level that falls during an operation changes nothing.

#include <optional>

#include "pbstore/rules.h"
#include "pbstore/store/battery.h"

namespace pbstore {
namespace store {

constexpr int kMinBatteryPercent = 25;    // on battery: install, update, remove, adopt, self-update
constexpr int kMinBatteryCharging = 10;   // the same while the reader is charging
constexpr int kMinBatteryRepair = 10;     // a repair, charging or not

enum class PowerOperation { Install, Update, Remove, Adopt, SelfUpdate, Repair };
enum class PowerResult { Allowed, Refused, NeedsRiskConfirmation };

struct PowerDecision {
    PowerResult result = PowerResult::Allowed;
    std::optional<int> level;   // nothing when the level cannot be read
    int minimum = kMinBatteryPercent;
};

// `charging` unreadable counts as not charging.
PowerDecision decide_power(PowerOperation op, std::optional<int> level, std::optional<bool> charging);

struct PowerFinding {
    Finding finding;   // E-STO-01 (refused) or E-STO-05 (level unreadable); rule_id empty when allowed
};
PowerFinding power_finding(const PowerDecision& decision);

class PowerGate {
public:
    explicit PowerGate(Battery& battery) : battery_(battery) {}
    PowerDecision allow(PowerOperation op);

private:
    Battery& battery_;
};

}  // namespace store
}  // namespace pbstore
