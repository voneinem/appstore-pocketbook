#include "pbstore/install/clock.h"

#include <cstdio>
#include <ctime>

namespace pbstore {
namespace install {

std::string Utc::iso() const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour, minute, second);
    return buf;
}

std::string LocalTime::day_key() const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d", year, month, day);
    return buf;
}

std::string LocalTime::stamp() const {
    char buf[40];
    const int off = offset_minutes < 0 ? -offset_minutes : offset_minutes;
    std::snprintf(buf, sizeof buf, "%04d%02d%02dT%02d%02d%02d%c%02d%02d", year, month, day, hour, minute, second,
                  offset_minutes < 0 ? '-' : '+', off / 60, off % 60);
    return buf;
}

Utc SystemClock::now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    Utc u;
    u.year = tm.tm_year + 1900;
    u.month = tm.tm_mon + 1;
    u.day = tm.tm_mday;
    u.hour = tm.tm_hour;
    u.minute = tm.tm_min;
    u.second = tm.tm_sec;
    return u;
}

LocalTime SystemClock::local_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    LocalTime l;
    l.year = tm.tm_year + 1900;
    l.month = tm.tm_mon + 1;
    l.day = tm.tm_mday;
    l.hour = tm.tm_hour;
    l.minute = tm.tm_min;
    l.second = tm.tm_sec;
    l.offset_minutes = static_cast<int>(tm.tm_gmtoff / 60);
    return l;
}

}  // namespace install
}  // namespace pbstore
