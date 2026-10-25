#pragma once
// Time, injected so tests can fix it (backup names, the log, midnight and DST boundaries).

#include <string>

namespace pbstore {
namespace install {

struct Utc {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    std::string iso() const;   // YYYY-MM-DDTHH:MM:SSZ
};

struct LocalTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    int offset_minutes = 0;    // east of UTC
    std::string day_key() const;   // YYYYMMDD: the local calendar day
    std::string stamp() const;     // YYYYMMDDTHHMMSS+hhmm, the part of a backup name
};

class Clock {
public:
    virtual ~Clock() = default;
    virtual Utc now() = 0;
    virtual LocalTime local_now() = 0;
};

class SystemClock : public Clock {
public:
    Utc now() override;
    LocalTime local_now() override;
};

}  // namespace install
}  // namespace pbstore
