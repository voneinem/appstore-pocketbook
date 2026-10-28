// T034: the battery gate (research R-07). 25 % on battery, 10 % while charging, 10 % for any repair; an unreadable
// level needs an explicit tap; the message of a refusal names the level and the minimum.
#include "fake_battery.h"
#include "pbstore/store/power_gate.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
const PowerOperation kWrites[] = {PowerOperation::Install, PowerOperation::Update, PowerOperation::Remove, PowerOperation::Adopt, PowerOperation::SelfUpdate};
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
}  // namespace

PB_TEST(the_numbers_are_named_constants) {
    PB_CHECK_EQ(kMinBatteryPercent, 25);
    PB_CHECK_EQ(kMinBatteryCharging, 10);
    PB_CHECK_EQ(kMinBatteryRepair, 10);
}

PB_TEST(on_battery_every_write_needs_25_percent) {
    for (PowerOperation op : kWrites) {
        PowerDecision low = decide_power(op, 24, false);
        PB_CHECK(low.result == PowerResult::Refused);
        PB_CHECK_EQ(low.minimum, 25);
        PB_CHECK(low.level == std::optional<int>(24));
        PB_CHECK(decide_power(op, 25, false).result == PowerResult::Allowed);
        PB_CHECK(decide_power(op, 100, false).result == PowerResult::Allowed);
        PB_CHECK(decide_power(op, 0, false).result == PowerResult::Refused);
    }
}

PB_TEST(while_charging_10_percent_is_enough) {
    for (PowerOperation op : kWrites) {
        PowerDecision low = decide_power(op, 9, true);
        PB_CHECK(low.result == PowerResult::Refused);
        PB_CHECK_EQ(low.minimum, 10);
        PB_CHECK(decide_power(op, 10, true).result == PowerResult::Allowed);
        PB_CHECK(decide_power(op, 24, true).result == PowerResult::Allowed);
    }
}

PB_TEST(a_repair_needs_only_10_percent_whether_or_not_it_is_charging) {
    for (bool charging : {false, true}) {
        PowerDecision low = decide_power(PowerOperation::Repair, 9, charging);
        PB_CHECK(low.result == PowerResult::Refused);
        PB_CHECK_EQ(low.minimum, 10);
        PB_CHECK(decide_power(PowerOperation::Repair, 10, charging).result == PowerResult::Allowed);
    }
    PB_CHECK(decide_power(PowerOperation::Repair, 12, std::nullopt).result == PowerResult::Allowed);
}

PB_TEST(an_unreadable_charging_state_counts_as_not_charging) {
    PB_CHECK(decide_power(PowerOperation::Install, 24, std::nullopt).result == PowerResult::Refused);
    PB_CHECK_EQ(decide_power(PowerOperation::Install, 24, std::nullopt).minimum, 25);
    PB_CHECK(decide_power(PowerOperation::Install, 25, std::nullopt).result == PowerResult::Allowed);
}

PB_TEST(an_unreadable_level_needs_an_explicit_confirmation_for_every_operation) {
    for (PowerOperation op : {PowerOperation::Install, PowerOperation::Update, PowerOperation::Remove, PowerOperation::Adopt, PowerOperation::SelfUpdate, PowerOperation::Repair}) {
        PowerDecision d = decide_power(op, std::nullopt, false);
        PB_CHECK(d.result == PowerResult::NeedsRiskConfirmation);
        PB_CHECK(!d.level.has_value());
        PowerFinding f = power_finding(d);
        PB_CHECK_EQ(f.finding.rule_id, std::string("E-STO-05"));
    }
}

PB_TEST(a_refusal_is_e_sto_01_and_names_the_level_and_the_minimum) {
    PowerFinding f = power_finding(decide_power(PowerOperation::Install, 17, false));
    PB_CHECK_EQ(f.finding.rule_id, std::string("E-STO-01"));
    PB_CHECK(has(f.finding.message, "17"));
    PB_CHECK(has(f.finding.message, "25"));
    PB_CHECK(f.finding.severity == pbstore::Severity::Error);
    PowerFinding repair = power_finding(decide_power(PowerOperation::Repair, 4, true));
    PB_CHECK(has(repair.finding.message, "4"));
    PB_CHECK(has(repair.finding.message, "10"));
    PB_CHECK(power_finding(decide_power(PowerOperation::Install, 80, false)).finding.rule_id.empty());   // allowed: no finding
}

PB_TEST(the_gate_reads_the_battery_through_the_interface) {
    pbtest::FakeBattery battery;
    PowerGate gate(battery);
    battery.set_percent(80);
    PB_CHECK(gate.allow(PowerOperation::Install).result == PowerResult::Allowed);
    battery.set_percent(20);
    PB_CHECK(gate.allow(PowerOperation::Install).result == PowerResult::Refused);
    battery.set_charging(true);
    PB_CHECK(gate.allow(PowerOperation::Install).result == PowerResult::Allowed);
    battery.set_percent_unreadable();
    PB_CHECK(gate.allow(PowerOperation::Repair).result == PowerResult::NeedsRiskConfirmation);
    PB_CHECK(!battery.calls().empty());
}

PB_TEST_MAIN()
