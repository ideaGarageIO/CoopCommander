/*
 * Coop node configuration - coop-1 (real hardware). Copy this file per coop.
 *
 * Each coop gets its own copy with a unique COOP_ID. Either:
 *   - keep one file per coop in config/coops/ and build with
 *       scripts/build.sh --coop config/coops/<id>.h
 *     (it copies that file here as coop_config.h), or
 *   - copy this file to coop_config.h in this folder and edit it.
 *
 * coop_config.h is gitignored. Without it the sketch builds with these
 * defaults (mock door + mock sensor), which is handy on the bench.
 */

#ifndef COOP_CONFIG_H
#define COOP_CONFIG_H

// ---- Identity -------------------------------------------------------------
// COOP_ID becomes the MQTT topic level (coop/<id>/state) and the Home
// Assistant entity id, so it must match [a-z0-9][a-z0-9_-]{0,31} and be
// unique on the farm. COOP_NAME is what the dashboard shows (max 32 chars).
#define COOP_ID             "coop-1"
#define COOP_NAME           "Coop 1"

// ---- Location and schedule ------------------------------------------------
// Used for sunrise/sunset. Decimal degrees, west longitude negative.
#define COOP_LATITUDE       40.0
#define COOP_LONGITUDE      -105.0
// POSIX time zone for the display and EARLIEST_OPEN_MINUTE, e.g.
//   US Central  "CST6CDT,M3.2.0,M11.1.0"
//   US Mountain "MST7MDT,M3.2.0,M11.1.0"
//   US Eastern  "EST5EDT,M3.2.0,M11.1.0"
#define COOP_TZ             "MST7MDT,M3.2.0,M11.1.0"
// Minutes after sunrise to open / after sunset to close (negative = before).
// Chickens head in to roost around sunset, so close a little after it.
#define OPEN_OFFSET_MIN     0
#define CLOSE_OFFSET_MIN    30
// Never open before this local time (minutes after midnight); 0 = no limit.
#define EARLIEST_OPEN_MINUTE (6 * 60)

// ---- Door hardware --------------------------------------------------------
// Give up and report JAMMED if a limit switch isn't reached in this time.
// Set to about 1.5x the real travel time of your door.
#define DOOR_TRAVEL_TIMEOUT_MS  45000
// Swap motor direction in software instead of re-wiring the motor.
#define MOTOR_REVERSED      false

// ---- Battery sense --------------------------------------------------------
// Carrier board R1/R2 = 100k/22k -> Vbatt = Vadc * 5.545
#define BATTERY_DIVIDER_RATIO   5.545

// ---- Bench testing --------------------------------------------------------
// Mock door: no motor or limit switches needed, the door "travels" in 8 s.
// Mock DHT: fixed 18.0 C / 60 %. Set both false on real hardware.
#define MOCK_DOOR_ENABLED   false
#define MOCK_DHT_ENABLED    MOCK_DOOR_ENABLED

#endif // COOP_CONFIG_H
