#pragma once
// Test double for pbstore::store::Battery. The default level is 80 %. A level outside 0..100 is reported as unreadable,
// as the reader's wrapper does (contracts/device-interfaces.md). Every read is recorded.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/store/battery.h"

namespace pbtest {

class FakeBattery : public pbstore::store::Battery {
public:
    void set_percent(int p) { percent_ = p; unreadable_percent_ = false; }
    void set_percent_unreadable() { unreadable_percent_ = true; }
    void set_charging(bool c) { charging_ = c; unreadable_charging_ = false; }
    void set_charging_unreadable() { unreadable_charging_ = true; }
    const std::vector<std::string>& calls() const { return calls_; }

    std::optional<int> percent() override {
        calls_.push_back("percent");
        if (unreadable_percent_ || percent_ < 0 || percent_ > 100) return std::nullopt;
        return percent_;
    }
    std::optional<bool> charging() override {
        calls_.push_back("charging");
        if (unreadable_charging_) return std::nullopt;
        return charging_;
    }

private:
    int percent_ = 80;
    bool charging_ = false, unreadable_percent_ = false, unreadable_charging_ = false;
    std::vector<std::string> calls_;
};

}  // namespace pbtest
