// Host-side tests for farm_protocol.h and sun_times.h.
// Run: scripts/test_firmware_host.sh
#include <cassert>
#include <cmath>
#include <cstdlib>
#include "farm_protocol.h"
#include "sun_times.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_node_ids() {
    CHECK(validNodeId("coop-1"));
    CHECK(validNodeId("a"));
    CHECK(validNodeId("main_coop"));
    CHECK(!validNodeId(""));
    CHECK(!validNodeId("-coop"));
    CHECK(!validNodeId("Coop"));
    CHECK(!validNodeId("coop/1"));
    CHECK(!validNodeId("abcdefghijklmnopqrstuvwxyz0123456"));  // 33 chars
}

static void test_parse_addressed() {
    char p1[] = "@coop-1|A|7|1763070000";
    char *id, *rest; char type;
    CHECK(parseAddressed(p1, &id, &type, &rest));
    CHECK(strcmp(id, "coop-1") == 0 && type == 'A' && strcmp(rest, "7|1763070000") == 0);

    char p2[] = "@coop-1|S";
    CHECK(parseAddressed(p2, &id, &type, &rest) && type == 'S' && rest[0] == '\0');

    char bad1[] = "S|1|12400|5";           // gate packet
    char bad2[] = "@coop-1";               // no type
    char bad3[] = "@coop-1|SX|1";          // two-char type
    char bad4[] = "@Bad|S|1";              // invalid id
    char bad5[] = "@|S|1";                 // empty id
    CHECK(!parseAddressed(bad1, &id, &type, &rest));
    CHECK(!parseAddressed(bad2, &id, &type, &rest));
    CHECK(!parseAddressed(bad3, &id, &type, &rest));
    CHECK(!parseAddressed(bad4, &id, &type, &rest));
    CHECK(!parseAddressed(bad5, &id, &type, &rest));
}

static void test_coop_state_roundtrip() {
    char buf[BUFFER_SIZE];
    // Longest realistic packet: 31-char id, max values
    const char* longId = "abcdefghijklmnopqrstuvwxyz01234";
    int n = formatCoopState(buf, longId, 'S', 65535, DOOR_JAMMED, COOP_MODE_AUTO, 65535,
                            ENV_TEMP_INVALID, ENV_HUM_INVALID, 4294967295UL, 4294967295UL);
    CHECK(n > 0 && n < BUFFER_SIZE);

    formatCoopState(buf, "coop-1", 'H', 42, DOOR_OPEN, COOP_MODE_MANUAL, 12610, -123, 550, 1763124120, 0);
    char *id, *rest; char type;
    CHECK(parseAddressed(buf, &id, &type, &rest));
    // Same format string as house_gateway.ino handleCoopPacket()
    unsigned short seq; unsigned door, mode, batt, hum; int temp; unsigned long o, c;
    int f = sscanf(rest, "%hu|%u|%u|%u|%d|%u|%lu|%lu", &seq, &door, &mode, &batt, &temp, &hum, &o, &c);
    CHECK(f == 8);
    CHECK(seq == 42 && door == DOOR_OPEN && mode == COOP_MODE_MANUAL && batt == 12610);
    CHECK(temp == -123 && hum == 550 && o == 1763124120UL && c == 0);

    // Invalid env reading survives the round trip as the sentinel values
    formatCoopState(buf, "coop-1", 'S', 1, DOOR_CLOSED, COOP_MODE_AUTO, 0, ENV_TEMP_INVALID, ENV_HUM_INVALID, 0, 0);
    CHECK(parseAddressed(buf, &id, &type, &rest));
    sscanf(rest, "%hu|%u|%u|%u|%d|%u|%lu|%lu", &seq, &door, &mode, &batt, &temp, &hum, &o, &c);
    CHECK(!envValid((int16_t)temp, (uint16_t)hum));
}

static void test_ack_and_command() {
    char buf[BUFFER_SIZE];
    formatAddressedAck(buf, "coop-1", 9, 1763070000UL);
    CHECK(strcmp(buf, "@coop-1|A|9|1763070000") == 0);
    formatAddressedCommand(buf, "coop-1", CMD_MANUAL, 300);
    CHECK(strcmp(buf, "@coop-1|C|6|300") == 0);
    formatCoopInfo(buf, "coop-1", 3, "Main Coop");
    CHECK(strcmp(buf, "@coop-1|I|3|Main Coop") == 0);
    CHECK(strcmp(coopCommandToString(CMD_AUTO), "AUTO") == 0);
    CHECK(strcmp(coopCommandToString(CMD_CLOSE), "CLOSE") == 0);
    CHECK(strcmp(coopDoorToString(DOOR_JAMMED), "JAMMED") == 0);
}

static void test_gate_packets_unchanged() {
    char buf[BUFFER_SIZE];
    formatStatusMessage(buf, STATE_OPEN, 12400, 7, 215, 480);
    CHECK(strcmp(buf, "S|2|12400|7|215|480") == 0);
    formatHeartbeatMessage(buf, STATE_CLOSED, 12000, 8);
    CHECK(strcmp(buf, "H|1|12000|8") == 0);
}

// Reference values from the Server's gate_monitor/sun.py (same algorithm).
static void test_sun_times() {
    struct { int y, m, d; double lat, lon; long rise, set; } cases[] = {
        {2026, 6, 21, 39.74, -104.99, 1782041498, 1782095456},
        {2026, 12, 21, 39.74, -104.99, 1797862630, 1797896303},
        {2026, 3, 20, 51.5, -0.12, 1773986736, 1774030300},
        {2026, 11, 13, 40.0, -105.0, 1794577332, 1794613610},
    };
    for (auto& c : cases) {
        DoorSchedule s = doorScheduleFor(c.y, c.m, c.d, c.lat, c.lon, 0, 0, 0);
        CHECK(s.valid);
        CHECK(labs((long)s.open - c.rise) <= 60);
        CHECK(labs((long)s.close - c.set) <= 60);
    }
    // Offsets and earliest-open clamp
    DoorSchedule base = doorScheduleFor(2026, 6, 21, 39.74, -104.99, 0, 0, 0);
    DoorSchedule off = doorScheduleFor(2026, 6, 21, 39.74, -104.99, 15, 30, 0);
    CHECK(off.open == base.open + 900 && off.close == base.close + 1800);
    DoorSchedule clamp = doorScheduleFor(2026, 6, 21, 39.74, -104.99, 0, 0, base.open + 3600);
    CHECK(clamp.open == base.open + 3600 && clamp.close == base.close);
    // Polar night (Tromso, December) keeps the door shut; midnight sun opens all day
    CHECK(!doorScheduleFor(2026, 12, 21, 69.65, 18.96, 0, 0, 0).valid);
    DoorSchedule midsummer = doorScheduleFor(2026, 6, 21, 69.65, 18.96, 0, 0, 0);
    CHECK(midsummer.valid && midsummer.close - midsummer.open == 86399);
    CHECK(daysFromCivil(1970, 1, 1) == 0 && daysFromCivil(2000, 3, 1) == 11017);
}

int main() {
    test_node_ids();
    test_parse_addressed();
    test_coop_state_roundtrip();
    test_ack_and_command();
    test_gate_packets_unchanged();
    test_sun_times();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all host tests passed");
    return 0;
}
