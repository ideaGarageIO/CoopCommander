/*
 * Farm Protocol - Shared LoRa Message Protocol
 *
 * Common definitions for every LoRa node on the farm and the house gateway
 * that bridges them to MQTT:
 *   - the gate node (GateMonitor repo, firmware/gate_node)
 *   - chicken coop nodes (CoopMonitor repo, firmware/coop_node)
 *   - the house gateway (GateMonitor repo, firmware/house_gateway)
 *
 * SOURCE OF TRUTH: GateMonitor/firmware/lib/farm_protocol/farm_protocol.h.
 * Every sketch folder carries an identical copy (Arduino only compiles files
 * in the sketch folder). Edit the source of truth, then run
 * scripts/sync_protocol.sh in each repo.
 */

#ifndef FARM_PROTOCOL_H
#define FARM_PROTOCOL_H

#include <Arduino.h>

// LoRa Configuration (915MHz US ISM band)
#define RF_FREQUENCY        915000000
#define TX_OUTPUT_POWER     14          // dBm (max 20)
#define LORA_BANDWIDTH      0           // 125 kHz
#define LORA_SPREADING_FACTOR 9         // SF9 for better range
#define LORA_CODINGRATE     1           // 4/5
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define BUFFER_SIZE         128     // Longest packet is a coop status (~70 bytes)

// Message Types
#define MSG_STATUS      'S'     // Gate status update
#define MSG_HEARTBEAT   'H'     // Periodic keepalive
#define MSG_COMMAND     'C'     // Control command
#define MSG_ACK         'A'     // Acknowledgment
#define MSG_ERROR       'E'     // Error report
#define MSG_INFO        'I'     // Node name/info (addressed nodes)

// Gate States
#define STATE_UNKNOWN   0
#define STATE_CLOSED    1
#define STATE_OPEN      2
#define STATE_MOVING    3
#define STATE_ERROR     4

// Commands
#define CMD_OPEN        1
#define CMD_CLOSE       2
#define CMD_STOP        3
#define CMD_STATUS      4       // Request status

// Timing (milliseconds)
#define HEARTBEAT_INTERVAL  30000   // 30 seconds
#define ACK_TIMEOUT         5000    // 5 seconds
#define RETRY_COUNT         3
#define DEBOUNCE_TIME       50      // 50ms for limit switches
// Wait this long after receiving a packet before transmitting (ACKs included).
// The SX1262 only returns to RX after TX when loop() next calls
// Radio.IrqProcess(), which can be tens of ms away (OLED refresh, delays,
// ADC/DHT reads). A reply sent sooner can start while the peer is still deaf
// and lose its preamble (8 symbols = 33 ms at SF9/125 kHz), so the ACK is
// missed and the link looks lost.
#define TX_TURNAROUND_MS    100

// DHT22 temperature / humidity sensor
#define DHT_READ_INTERVAL   10000   // ms between reads (sensor minimum is 2 s)
#define DHT_MAX_FAILURES    3       // Consecutive bad reads before data is dropped

// Battery thresholds (millivolts)
#define BATTERY_FULL        13800   // 13.8V fully charged
#define BATTERY_NOMINAL     12600   // 12.6V nominal
#define BATTERY_LOW         11500   // 11.5V low warning
#define BATTERY_CRITICAL    11000   // 11.0V critical

// Node identifiers
#define NODE_GATE       0x01
#define NODE_GATEWAY    0x02

// Message structure
typedef struct {
    uint8_t type;           // Message type (S, H, C, A, E)
    uint8_t node_id;        // Source node
    uint16_t sequence;      // Sequence number for ACK matching
    uint8_t state;          // Gate state
    uint16_t battery_mv;    // Battery voltage in millivolts
    uint8_t command;        // Command (for CMD messages)
    int16_t rssi;           // RSSI of last received message
    int8_t snr;             // SNR of last received message
} GateMessage;

// Helper functions
inline const char* stateToString(uint8_t state) {
    switch (state) {
        case STATE_CLOSED:  return "CLOSED";
        case STATE_OPEN:    return "OPEN";
        case STATE_MOVING:  return "MOVING";
        case STATE_ERROR:   return "ERROR";
        default:            return "UNKNOWN";
    }
}

inline const char* commandToString(uint8_t cmd) {
    switch (cmd) {
        case CMD_OPEN:   return "OPEN";
        case CMD_CLOSE:  return "CLOSE";
        case CMD_STOP:   return "STOP";
        case CMD_STATUS: return "STATUS";
        default:         return "UNKNOWN";
    }
}

// Simple message formatting (pipe-delimited for easy parsing)
// STATUS:    S|<state>|<battery_mv>|<seq>[|<temp_dC>|<humidity_dPct>]
// HEARTBEAT: H|<state>|<battery_mv>|<seq>[|<temp_dC>|<humidity_dPct>]
// COMMAND:   C|<cmd>|<seq>
// ACK:       A|<seq>
//
// The environment fields are optional and are left off when there is no valid
// DHT22 reading. Temperature is in tenths of a degree C (signed, -400..800).
// Humidity is in tenths of a percent (0..1000). A gateway that predates these
// fields parses the first three and ignores the rest, so old and new firmware
// interoperate in both directions.

// Environment reading carried in status/heartbeat packets
#define ENV_TEMP_INVALID    INT16_MIN
#define ENV_HUM_INVALID     0xFFFF

inline bool envValid(int16_t temp_dc, uint16_t hum_dpct) {
    return temp_dc != ENV_TEMP_INVALID && hum_dpct != ENV_HUM_INVALID;
}

inline int formatStateMessage(char* buf, char type, uint8_t state, uint16_t battery_mv,
                              uint16_t seq, int16_t temp_dc, uint16_t hum_dpct) {
    if (envValid(temp_dc, hum_dpct)) {
        return snprintf(buf, BUFFER_SIZE, "%c|%d|%u|%u|%d|%u",
                        type, state, battery_mv, seq, temp_dc, hum_dpct);
    }
    return snprintf(buf, BUFFER_SIZE, "%c|%d|%u|%u", type, state, battery_mv, seq);
}

inline int formatStatusMessage(char* buf, uint8_t state, uint16_t battery_mv, uint16_t seq,
                               int16_t temp_dc = ENV_TEMP_INVALID,
                               uint16_t hum_dpct = ENV_HUM_INVALID) {
    return formatStateMessage(buf, MSG_STATUS, state, battery_mv, seq, temp_dc, hum_dpct);
}

inline int formatHeartbeatMessage(char* buf, uint8_t state, uint16_t battery_mv, uint16_t seq,
                                  int16_t temp_dc = ENV_TEMP_INVALID,
                                  uint16_t hum_dpct = ENV_HUM_INVALID) {
    return formatStateMessage(buf, MSG_HEARTBEAT, state, battery_mv, seq, temp_dc, hum_dpct);
}

inline int formatCommandMessage(char* buf, uint8_t cmd, uint16_t seq) {
    return sprintf(buf, "C|%d|%u", cmd, seq);
}

inline int formatAckMessage(char* buf, uint16_t seq) {
    return sprintf(buf, "A|%u", seq);
}

// Sequence numbers skip 0, which both sketches use to mean "nothing pending".
inline uint16_t nextSequence(uint16_t seq) {
    seq++;
    return seq == 0 ? 1 : seq;
}

// Copy a received LoRa payload into a BUFFER_SIZE buffer, truncating oversized
// packets (the SX1262 can deliver up to 255 bytes) and NUL-terminating.
// Returns the number of bytes kept.
inline uint16_t copyRxPayload(char* dst, const uint8_t* payload, uint16_t size) {
    uint16_t n = size < (BUFFER_SIZE - 1) ? size : (BUFFER_SIZE - 1);
    memcpy(dst, payload, n);
    dst[n] = '\0';
    return n;
}

// ===========================================================================
// Addressed device packets (coop nodes and any future node type)
// ===========================================================================
// The gate predates addressing and keeps the bare "S|..." format above. Every
// other node puts its id in front of each packet, so nodes ignore traffic
// meant for someone else and the gateway knows which MQTT topic to use:
//
//   node -> gw   @<id>|S|<seq>|<door>|<mode>|<batt_mv>|<temp_dc>|<hum_dpct>|<next_open>|<next_close>
//   node -> gw   @<id>|H|...                     same fields, periodic heartbeat
//   node -> gw   @<id>|I|<seq>|<name>            display name (boot + every 10 min)
//   gw -> node   @<id>|A|<seq>|<unix_time>       ACK; carries the gateway's NTP time
//                                                (0 while the gateway has no time yet)
//   gw -> node   @<id>|C|<cmd>|<seq>             command (CMD_*)
//   node -> gw   @<id>|A|<seq>                   command ACK
//
// <id> matches [a-z0-9][a-z0-9_-]{0,31} and becomes the MQTT topic level
// (coop/<id>/state). temp_dc / hum_dpct use ENV_*_INVALID when there is no
// sensor reading; next_open / next_close are Unix times, 0 = not scheduled.
// The gate node ignores every packet that starts with '@'.

#define ADDR_PREFIX         '@'
#define NODE_ID_MAX         32

// Coop door states (published as the strings in coopDoorToString)
#define DOOR_UNKNOWN        0
#define DOOR_OPEN           1
#define DOOR_CLOSED         2
#define DOOR_OPENING        3
#define DOOR_CLOSING        4
#define DOOR_JAMMED         5

#define COOP_MODE_MANUAL    0
#define COOP_MODE_AUTO      1

// Commands beyond CMD_OPEN..CMD_STATUS (coop only)
#define CMD_AUTO            5       // Follow the sunrise/sunset schedule
#define CMD_MANUAL          6       // Pause the schedule

// Coop timing
#define COOP_HEARTBEAT_INTERVAL 60000   // ms between coop heartbeats
#define COOP_OFFLINE_MS     (COOP_HEARTBEAT_INTERVAL * 3 + 20000)  // gateway marks a coop offline
#define COOP_INFO_INTERVAL  600000      // ms between name (I) packets

// Unix times before this are treated as "clock not set"
#define MIN_VALID_UNIX_TIME 1700000000UL

inline const char* coopDoorToString(uint8_t door) {
    switch (door) {
        case DOOR_OPEN:    return "OPEN";
        case DOOR_CLOSED:  return "CLOSED";
        case DOOR_OPENING: return "OPENING";
        case DOOR_CLOSING: return "CLOSING";
        case DOOR_JAMMED:  return "JAMMED";
        default:           return "UNKNOWN";
    }
}

inline const char* coopCommandToString(uint8_t cmd) {
    switch (cmd) {
        case CMD_AUTO:   return "AUTO";
        case CMD_MANUAL: return "MANUAL";
        default:         return commandToString(cmd);
    }
}

inline bool validNodeId(const char* id) {
    size_t n = strlen(id);
    if (n == 0 || n >= NODE_ID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '_' || c == '-'));
        if (!ok) return false;
    }
    return true;
}

// Splits "@<id>|<type>|<rest>" in place. On success id points at the node id,
// *type is the message type and rest at the fields after "<type>|" (may be "").
// Returns false for anything that is not a well-formed addressed packet.
inline bool parseAddressed(char* pkt, char** id, char* type, char** rest) {
    if (pkt[0] != ADDR_PREFIX) return false;
    char* bar = strchr(pkt + 1, '|');
    if (bar == nullptr || bar[1] == '\0' || (bar[2] != '|' && bar[2] != '\0')) return false;
    *bar = '\0';
    if (!validNodeId(pkt + 1)) return false;
    *id = pkt + 1;
    *type = bar[1];
    *rest = bar[2] == '|' ? bar + 3 : bar + 2;
    return true;
}

inline int formatCoopState(char* buf, const char* id, char type, uint16_t seq, uint8_t door,
                           uint8_t mode, uint16_t battery_mv, int16_t temp_dc, uint16_t hum_dpct,
                           uint32_t next_open, uint32_t next_close) {
    return snprintf(buf, BUFFER_SIZE, "@%s|%c|%u|%u|%u|%u|%d|%u|%lu|%lu", id, type, seq, door, mode,
                    battery_mv, temp_dc, hum_dpct, (unsigned long)next_open, (unsigned long)next_close);
}

inline int formatCoopInfo(char* buf, const char* id, uint16_t seq, const char* name) {
    return snprintf(buf, BUFFER_SIZE, "@%s|I|%u|%s", id, seq, name);
}

inline int formatAddressedAck(char* buf, const char* id, uint16_t seq, uint32_t unix_time) {
    return snprintf(buf, BUFFER_SIZE, "@%s|A|%u|%lu", id, seq, (unsigned long)unix_time);
}

inline int formatAddressedCommand(char* buf, const char* id, uint8_t cmd, uint16_t seq) {
    return snprintf(buf, BUFFER_SIZE, "@%s|C|%u|%u", id, cmd, seq);
}

// Small FIFO of outgoing LoRa packets. Callers push from anywhere (including
// radio callbacks); the main loop pops one packet whenever the radio is idle,
// so a new transmission never cuts off one already in progress.
#define TX_QUEUE_LEN 4

struct TxQueue {
    char items[TX_QUEUE_LEN][BUFFER_SIZE];
    uint8_t head = 0;
    uint8_t count = 0;

    bool empty() const { return count == 0; }
    bool full() const { return count == TX_QUEUE_LEN; }

    // Adds a packet. Urgent packets (ACKs) jump to the front so the sender
    // sees them before its ACK timeout. Returns false if the queue is full.
    bool push(const char* msg, bool urgent = false) {
        if (full()) return false;
        uint8_t slot;
        if (urgent) {
            head = (head + TX_QUEUE_LEN - 1) % TX_QUEUE_LEN;
            slot = head;
        } else {
            slot = (head + count) % TX_QUEUE_LEN;
        }
        strncpy(items[slot], msg, BUFFER_SIZE - 1);
        items[slot][BUFFER_SIZE - 1] = '\0';
        count++;
        return true;
    }

    // Copies the oldest packet into out and removes it. Returns false if empty.
    bool pop(char* out) {
        if (empty()) return false;
        strcpy(out, items[head]);
        head = (head + 1) % TX_QUEUE_LEN;
        count--;
        return true;
    }
};

#endif // FARM_PROTOCOL_H
