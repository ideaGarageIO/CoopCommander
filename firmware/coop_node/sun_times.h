/*
 * Sunrise / sunset for the coop door schedule.
 *
 * Same NOAA approximation as the Server's gate_monitor/sun.py (accurate to a
 * few minutes away from the poles). Pure C++, no Arduino dependencies, so it
 * can be unit-tested on a PC (see scripts/test_firmware_host.sh).
 */

#ifndef SUN_TIMES_H
#define SUN_TIMES_H

#include <math.h>
#include <stdint.h>

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Avoids timegm(), which newlib on the ESP32 lacks.
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

inline bool isLeapYear(int32_t y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

inline int dayOfYear(int32_t y, uint32_t m, uint32_t d) {
    return daysFromCivil(y, m, d) - daysFromCivil(y, 1, 1) + 1;
}

enum SunDay {
    SUN_NORMAL,        // rise and set both happen
    SUN_POLAR_NIGHT,   // sun never rises
    SUN_MIDNIGHT_SUN,  // sun never sets
};

// Sunrise and sunset for a calendar date, as minutes after 00:00 UTC on that
// date. Values can fall outside 0..1440 (e.g. a US sunset is after UTC midnight).
inline SunDay sunEventsUtcMinutes(int32_t year, uint32_t month, uint32_t day,
                                  double latitude, double longitude,
                                  double* sunriseMin, double* sunsetMin) {
    const double PI_ = 3.14159265358979323846;
    int n = dayOfYear(year, month, day);
    double daysInYear = isLeapYear(year) ? 366.0 : 365.0;
    double gamma = 2 * PI_ / daysInYear * (n - 1);
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma)
                              - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                  - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                  - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    double lat = latitude * PI_ / 180.0;
    double zenith = 90.833 * PI_ / 180.0;
    double cosHa = cos(zenith) / (cos(lat) * cos(decl)) - tan(lat) * tan(decl);

    if (cosHa >= 1) {
        *sunriseMin = *sunsetMin = 0;
        return SUN_POLAR_NIGHT;
    }
    if (cosHa <= -1) {
        *sunriseMin = 0;
        *sunsetMin = 1439;
        return SUN_MIDNIGHT_SUN;
    }
    double ha = acos(cosHa) * 180.0 / PI_;
    *sunriseMin = 720 - 4 * (longitude + ha) - eqtime;
    *sunsetMin = 720 - 4 * (longitude - ha) - eqtime;
    return SUN_NORMAL;
}

// Door open/close times (Unix seconds) for one local calendar date.
struct DoorSchedule {
    uint32_t open;
    uint32_t close;
    bool valid;     // false during polar night: the door stays closed
};

// openOffsetMin / closeOffsetMin shift the times relative to sunrise / sunset.
// earliestOpenUnix (0 = off) holds the door shut until that time, e.g. 06:00
// local, so the flock is not let out at 4:30 am in midsummer.
inline DoorSchedule doorScheduleFor(int32_t year, uint32_t month, uint32_t day,
                                    double latitude, double longitude,
                                    int openOffsetMin, int closeOffsetMin,
                                    uint32_t earliestOpenUnix) {
    DoorSchedule s = {0, 0, false};
    double rise = 0, set = 0;
    SunDay kind = sunEventsUtcMinutes(year, month, day, latitude, longitude, &rise, &set);
    if (kind == SUN_POLAR_NIGHT) return s;
    int64_t midnightUtc = (int64_t)daysFromCivil(year, month, day) * 86400;
    int64_t open = midnightUtc + (int64_t)lround((rise + openOffsetMin) * 60.0);
    int64_t close = midnightUtc + (int64_t)lround((set + closeOffsetMin) * 60.0);
    if (kind == SUN_MIDNIGHT_SUN) {
        open = midnightUtc;
        close = midnightUtc + 86399;
    }
    if (earliestOpenUnix != 0 && open < (int64_t)earliestOpenUnix) open = earliestOpenUnix;
    if (open >= close) return s;
    s.open = (uint32_t)open;
    s.close = (uint32_t)close;
    s.valid = true;
    return s;
}

#endif // SUN_TIMES_H
