#pragma once
// Test double for pbstore::store::Screen: counts the full refreshes asked for.

#include "pbstore/store/screen.h"

namespace pbtest {

class FakeScreen : public pbstore::store::Screen {
public:
    void full_update() override { ++count_; }
    int full_updates() const { return count_; }

private:
    int count_ = 0;
};

}  // namespace pbtest
