#pragma once
// Test double for pbstore::store::Radio: scriptable start state, a connect that succeeds after a number of polls, is
// refused, or never completes, a disconnect that works or does not, and a record of every call in order.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/store/radio.h"

namespace pbtest {

class FakeRadio : public pbstore::store::Radio {
public:
    enum class Connect { Succeeds, Refused, NeverConnects };

    void set_on(bool on) { on_ = on; connected_ = on; }
    void set_state_unreadable(bool u) { unreadable_ = u; }
    void set_connect(Connect mode, int polls_until_connected = 0) { mode_ = mode; polls_ = polls_until_connected; }
    void set_disconnect_works(bool works) { disconnect_works_ = works; }
    const std::vector<std::string>& calls() const { return calls_; }
    int count(const std::string& call) const { int n = 0; for (const auto& c : calls_) if (c == call) ++n; return n; }
    bool powered() const { return on_; }    // the real state, not through the interface

    std::optional<bool> is_on() override {
        calls_.push_back("is_on");
        if (unreadable_) return std::nullopt;
        return on_;
    }
    bool is_connected() override {
        calls_.push_back("is_connected");
        if (!on_) return false;
        if (connecting_) {
            if (mode_ == Connect::NeverConnects) return false;
            if (polls_left_ > 0) { --polls_left_; return false; }
            connecting_ = false;
            connected_ = true;
        }
        return connected_;
    }
    bool connect() override {
        calls_.push_back("connect");
        if (mode_ == Connect::Refused) { error_ = 7; return false; }
        error_ = 0;
        on_ = true;                          // powered up at once
        connecting_ = !connected_;
        polls_left_ = polls_;
        return true;
    }
    bool disconnect() override {
        calls_.push_back("disconnect");
        if (!on_) return true;               // idempotent
        if (!disconnect_works_) return false;
        on_ = false;
        connected_ = false;
        connecting_ = false;
        return true;
    }
    int last_error() override { return error_; }

private:
    bool on_ = false, connected_ = false, connecting_ = false, unreadable_ = false, disconnect_works_ = true;
    Connect mode_ = Connect::Succeeds;
    int polls_ = 0, polls_left_ = 0, error_ = 0;
    std::vector<std::string> calls_;
};

}  // namespace pbtest
