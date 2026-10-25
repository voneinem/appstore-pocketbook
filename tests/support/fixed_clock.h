#pragma once
// Test double: a clock that stands still until the test moves it.

#include "pbstore/install/clock.h"

namespace pbtest {

class FixedClock : public pbstore::install::Clock {
public:
    FixedClock() {
        utc_.year = 2026; utc_.month = 10; utc_.day = 25; utc_.hour = 12;
        local_.year = 2026; local_.month = 10; local_.day = 25; local_.hour = 14; local_.offset_minutes = 120;
    }
    pbstore::install::Utc now() override { return utc_; }
    pbstore::install::LocalTime local_now() override { return local_; }
    void set_utc(int y, int mo, int d, int h, int mi, int s) { utc_.year = y; utc_.month = mo; utc_.day = d; utc_.hour = h; utc_.minute = mi; utc_.second = s; }
    void set_local(int y, int mo, int d, int h, int mi, int s, int offset_minutes) {
        local_.year = y; local_.month = mo; local_.day = d; local_.hour = h; local_.minute = mi; local_.second = s; local_.offset_minutes = offset_minutes;
    }
    // moves the local time by whole seconds within the day (tests do not cross month ends)
    void advance_local_seconds(int n) {
        int total = local_.hour * 3600 + local_.minute * 60 + local_.second + n;
        while (total >= 86400) { total -= 86400; ++local_.day; }
        local_.hour = total / 3600; local_.minute = (total / 60) % 60; local_.second = total % 60;
    }

private:
    pbstore::install::Utc utc_;
    pbstore::install::LocalTime local_;
};

}  // namespace pbtest
