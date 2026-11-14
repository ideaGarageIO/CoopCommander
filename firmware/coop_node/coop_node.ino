/*
 * Coop Node - LoRa chicken coop door controller
 * Heltec WiFi LoRa 32 V3 on the coop node carrier board
 *
 * - Drives the door motor through an H-bridge module (IN1/IN2) and stops on
 *   the open / closed limit switches. A move that doesn't reach its switch in
 *   DOOR_TRAVEL_TIMEOUT_MS stops the motor and reports JAMMED.
 * - Auto mode opens at sunrise and closes after sunset (sun_times.h). The
 *   node has no WiFi or RTC: it takes the time from the house gateway's ACKs.
 * - Reads a DHT22 and the 12 V battery.
 * - The PRG button (or an external button on J11) opens / closes / stops.
 * - Talks to the house gateway (GateMonitor repo) with addressed
 *   "@<id>|..." packets (farm_protocol.h). The gateway publishes
 *   coop/<id>/state and forwards coop/<id>/command.
 *
 * Radio handling follows the gate node: callbacks only copy the packet,
 * loop() does the work, and every packet goes through txQueue.
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <Wire.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "HT_SSD1306Wire.h"
#include <DHT.h>                // "DHT sensor library" by Adafruit
#include "farm_protocol.h"
#include "sun_times.h"

#if __has_include("coop_config.h")
    #include "coop_config.h"
#else
    #warning "coop_config.h not found - building with coop_config_example.h defaults (mock door)"
    #include "coop_config_example.h"
#endif

// ---------------------------------------------------------------- pins
// Must match hardware/pcb/coop_node_carrier/scripts/design.py
#define PIN_LIMIT_OPEN    5     // J7-1, active LOW (dry contact to GND)
#define PIN_LIMIT_CLOSED  6     // J7-2, active LOW
#define PIN_MOTOR_IN1     7     // J3-3 -> H-bridge IN1
#define PIN_MOTOR_IN2     4     // J3-4 -> H-bridge IN2
#define PIN_AUX_RELAY     47    // J5 relay module (light / heater), held off
#define PIN_BATTERY_ADC   2     // R1/R2 divider
#define PIN_DHT_DATA      48    // J8-2
#define PIN_BTN_EXT       33    // J11 external push button, active LOW
#define PIN_BTN_PRG       0     // On-board PRG button, active LOW

// ---------------------------------------------------------------- timing
#define DEBOUNCE_MS             50
#define MOTOR_REVERSE_PAUSE_MS  300     // Motor off this long before reversing
#define SCHEDULE_CHECK_MS       1000
#define DISPLAY_TIMEOUT_MS      60000
#define DISPLAY_REFRESH_MS      250
#define LINK_LOST_MS            (COOP_HEARTBEAT_INTERVAL * 3)
#define DUPLICATE_CMD_WINDOW_MS (ACK_TIMEOUT * (RETRY_COUNT + 2))
#define MOCK_TRAVEL_MS          8000
#define MOCK_BATTERY_MV         12650
#define MOCK_TEMP_DC            180
#define MOCK_HUM_DPCT           600
#define CLOCK_RESYNC_DRIFT_S    2       // Re-set the clock if the gateway differs by more

#ifndef MOCK_DHT_ENABLED
#define MOCK_DHT_ENABLED MOCK_DOOR_ENABLED
#endif

// ---------------------------------------------------------------- state
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
static DHT dht(PIN_DHT_DATA, DHT22);
static Preferences prefs;

enum MotorDir : uint8_t { DIR_NONE, DIR_OPEN, DIR_CLOSE };

// Door
static uint8_t doorState = DOOR_UNKNOWN;
static uint8_t reportedDoor = 0xFF;          // Last door state queued for TX
static MotorDir motorDir = DIR_NONE;
static MotorDir pendingDir = DIR_NONE;       // Waiting out the reverse pause
static unsigned long motorStart = 0;
static unsigned long motorStoppedAt = 0;
static bool limitOpen = false, limitClosed = false;     // Debounced
static bool rawOpenPrev = false, rawClosedPrev = false;
static unsigned long rawOpenSince = 0, rawClosedSince = 0;

// Mock door position: 0 = closed, MOCK_FULL = open
#define MOCK_FULL 1000000L
static int32_t mockPos = 0;
static unsigned long mockLastStep = 0;

// Mode and schedule
static uint8_t mode = COOP_MODE_AUTO;
static uint8_t reportedMode = 0xFF;
static bool clockValid = false;
static uint32_t nextOpenT = 0, nextCloseT = 0;
static unsigned long lastScheduleCheck = 0;

// Sensors
static uint16_t batteryMv = 0;
static int16_t tempDc = ENV_TEMP_INVALID;
static uint16_t humDpct = ENV_HUM_INVALID;
static uint8_t dhtFailures = 0;
static unsigned long lastDhtRead = 0;

// Buttons
struct Button {
    uint8_t pin;
    bool pressed;           // Debounced
    bool rawPrev;
    unsigned long rawSince;
};
static Button buttons[] = {{PIN_BTN_PRG, false, false, 0}, {PIN_BTN_EXT, false, false, 0}};

// Link / messaging
static uint16_t sequenceNum = 0;
static uint16_t pendingAckSeq = 0;           // Status we want ACKed (0 = none)
static unsigned long pendingAckSentAt = 0;
static uint8_t statusRetries = 0;
static bool statusDirty = false;
static unsigned long lastHeartbeat = 0;
static unsigned long lastInfo = 0;
static bool infoDue = true;
static unsigned long lastAckTime = 0;
static bool everLinked = false;
static uint16_t lastCmdSeq = 0;
static unsigned long lastCmdTime = 0;

// Radio
static volatile bool lora_idle = true;
static TxQueue txQueue;
char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];
static bool rxReady = false;
static int16_t rxRssi = 0;
static int8_t rxSnr = 0;
static unsigned long lastRxTime = 0;
static RadioEvents_t RadioEvents;

// Display
static bool displayEnabled = true;
static unsigned long displayOnSince = 0;
static const char* flashText = nullptr;      // Short message ("OPENING", ...)
static unsigned long flashUntil = 0;

// ---------------------------------------------------------------- declarations
void initLoRa();
void initGPIO();
void motorDrive(MotorDir dir);
void requestMove(MotorDir dir, const char* why);
void stopDoor(const char* why);
void serviceDoor(unsigned long now);
void readLimits(unsigned long now);
void updateMock(unsigned long now);
void serviceButtons(unsigned long now);
void serviceSchedule(unsigned long now);
void recomputeSchedule(uint32_t now);
bool desiredOpenNow(uint32_t now);
void enforceSchedule(const char* why);
void setMode(uint8_t newMode);
uint16_t readBatteryVoltage();
void readEnvironment();
void queueState(char type);
void queueInfo();
void serviceStatusRetry(unsigned long now);
void serviceTx();
void handleRxPacket();
void processCommand(uint8_t cmd);
void setClock(uint32_t unixTime);
uint32_t unixNow();
bool linkOk();
void wakeDisplay();
void flash(const char* text);
void updateDisplay();
void checkDisplayTimeout();
void OnTxDone();
void OnTxTimeout();
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

void VextON() {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
}

void VextOFF() {
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH);
}

// ---------------------------------------------------------------- setup / loop
void setup() {
    // Motor off before anything else, so a reset never leaves it running
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);

    Serial.begin(115200);
    delay(300);
    Serial.println("\n=============================");
    Serial.printf("   COOP NODE %s - Starting\n", COOP_ID);
    Serial.println("=============================");
    if (!validNodeId(COOP_ID)) {
        Serial.println("!!! COOP_ID is not a valid id - fix coop_config.h");
    }

    setenv("TZ", COOP_TZ, 1);
    tzset();

    prefs.begin("coop", false);
    mode = prefs.getUChar("mode", COOP_MODE_AUTO) == COOP_MODE_MANUAL ? COOP_MODE_MANUAL : COOP_MODE_AUTO;

    // Random start so a reboot doesn't reuse sequence numbers the gateway just saw
    sequenceNum = (uint16_t)(esp_random() & 0x7FFF);

    VextON();
    delay(100);
    display.init();
    display.setContrast(255);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "COOP NODE");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 20, COOP_ID);
    display.drawString(0, 32, "Initializing...");
    display.display();

    initGPIO();
    if (!MOCK_DHT_ENABLED) dht.begin();
    initLoRa();

    unsigned long now = millis();
    // Seed the debouncers with the current switch positions
    readLimits(now);
    rawOpenSince = rawClosedSince = 0;
    readLimits(now + DEBOUNCE_MS);
    serviceDoor(now);

    batteryMv = readBatteryVoltage();
    readEnvironment();

    Serial.printf("Door: %s, mode: %s%s%s\n", coopDoorToString(doorState),
        mode == COOP_MODE_AUTO ? "auto" : "manual",
        MOCK_DOOR_ENABLED ? " [MOCK DOOR]" : "", MOCK_DHT_ENABLED ? " [MOCK DHT]" : "");
    Serial.println("Waiting for the gateway's time before the schedule runs");

    displayOnSince = millis();
    statusDirty = true;
    lastHeartbeat = millis();
}

void loop() {
    unsigned long now = millis();

    Radio.IrqProcess();
    if (rxReady) {
        rxReady = false;
        handleRxPacket();
    }

    if (MOCK_DOOR_ENABLED) updateMock(now);
    readLimits(now);
    serviceDoor(now);
    serviceButtons(now);
    serviceSchedule(now);

    if (now - lastDhtRead >= DHT_READ_INTERVAL) {
        readEnvironment();
        batteryMv = readBatteryVoltage();
    }

    if (doorState != reportedDoor || mode != reportedMode) {
        Serial.printf("State: door %s, mode %s\n", coopDoorToString(doorState),
            mode == COOP_MODE_AUTO ? "auto" : "manual");
        reportedDoor = doorState;
        reportedMode = mode;
        statusDirty = true;
        statusRetries = 0;      // A new state gets a fresh set of retries
    }

    if (infoDue || now - lastInfo >= COOP_INFO_INTERVAL) {
        queueInfo();
    }
    if (statusDirty) {
        queueState(MSG_STATUS);
    } else if (now - lastHeartbeat >= COOP_HEARTBEAT_INTERVAL) {
        queueState(MSG_HEARTBEAT);
    }
    serviceStatusRetry(now);
    serviceTx();

    checkDisplayTimeout();
    if (displayEnabled) updateDisplay();

    delay(5);
}

// ---------------------------------------------------------------- hardware
void initGPIO() {
    pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
    pinMode(PIN_LIMIT_CLOSED, INPUT_PULLUP);
    pinMode(PIN_BTN_EXT, INPUT_PULLUP);
    pinMode(PIN_BTN_PRG, INPUT_PULLUP);
    pinMode(PIN_AUX_RELAY, OUTPUT);
    digitalWrite(PIN_AUX_RELAY, LOW);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
}

void initLoRa() {
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxDone = OnRxDone;

    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
    Radio.Rx(0);
    lora_idle = true;
    Serial.println("LoRa initialized - continuous RX");
}

void motorDrive(MotorDir dir) {
    bool forward = (dir == DIR_OPEN) != MOTOR_REVERSED;
    if (dir == DIR_NONE) {
        digitalWrite(PIN_MOTOR_IN1, LOW);   // Both low = coast
        digitalWrite(PIN_MOTOR_IN2, LOW);
    } else {
        digitalWrite(PIN_MOTOR_IN1, forward ? HIGH : LOW);
        digitalWrite(PIN_MOTOR_IN2, forward ? LOW : HIGH);
    }
}

// Debounced limit switches (mock mode derives them from mockPos).
void readLimits(unsigned long now) {
    bool rawOpen, rawClosed;
    if (MOCK_DOOR_ENABLED) {
        rawOpen = mockPos >= MOCK_FULL;
        rawClosed = mockPos <= 0;
    } else {
        rawOpen = digitalRead(PIN_LIMIT_OPEN) == LOW;
        rawClosed = digitalRead(PIN_LIMIT_CLOSED) == LOW;
    }
    if (rawOpen != rawOpenPrev) { rawOpenPrev = rawOpen; rawOpenSince = now; }
    if (rawClosed != rawClosedPrev) { rawClosedPrev = rawClosed; rawClosedSince = now; }
    if (now - rawOpenSince >= DEBOUNCE_MS) limitOpen = rawOpen;
    if (now - rawClosedSince >= DEBOUNCE_MS) limitClosed = rawClosed;
}

void updateMock(unsigned long now) {
    unsigned long dt = now - mockLastStep;
    mockLastStep = now;
    if (dt > 100) dt = 100;
    int32_t step = (int32_t)(dt * MOCK_FULL / MOCK_TRAVEL_MS);
    if (motorDir == DIR_OPEN) mockPos = mockPos + step > MOCK_FULL ? MOCK_FULL : mockPos + step;
    if (motorDir == DIR_CLOSE) mockPos = mockPos - step < 0 ? 0 : mockPos - step;
}

// ---------------------------------------------------------------- door logic
void requestMove(MotorDir dir, const char* why) {
    if (dir == DIR_NONE) return;
    if (dir == motorDir) return;
    if (dir == DIR_OPEN && limitOpen && motorDir == DIR_NONE) {
        doorState = DOOR_OPEN;
        return;
    }
    if (dir == DIR_CLOSE && limitClosed && motorDir == DIR_NONE) {
        doorState = DOOR_CLOSED;
        return;
    }
    Serial.printf("Door %s (%s)\n", dir == DIR_OPEN ? "OPEN" : "CLOSE", why);
    if (motorDir != DIR_NONE) {
        // Reversing: stop first and let the motor spin down
        motorDrive(DIR_NONE);
        motorDir = DIR_NONE;
        motorStoppedAt = millis();
    }
    pendingDir = dir;
    flash(dir == DIR_OPEN ? "OPENING" : "CLOSING");
}

void stopDoor(const char* why) {
    pendingDir = DIR_NONE;
    if (motorDir == DIR_NONE) return;
    motorDrive(DIR_NONE);
    motorDir = DIR_NONE;
    motorStoppedAt = millis();
    Serial.printf("Door stopped (%s)\n", why);
}

void serviceDoor(unsigned long now) {
    if (pendingDir != DIR_NONE && motorDir == DIR_NONE &&
        now - motorStoppedAt >= MOTOR_REVERSE_PAUSE_MS) {
        motorDir = pendingDir;
        pendingDir = DIR_NONE;
        motorStart = now;
        mockLastStep = now;
        motorDrive(motorDir);
        doorState = motorDir == DIR_OPEN ? DOOR_OPENING : DOOR_CLOSING;
    }

    if (motorDir == DIR_OPEN || motorDir == DIR_CLOSE) {
        // Stop on the raw (undebounced) switch: overrunning is worse than a
        // stop on a noise spike, and the board's RC filter removes most noise.
        bool reached = motorDir == DIR_OPEN ? rawOpenPrev : rawClosedPrev;
        if (reached) {
            uint8_t endState = motorDir == DIR_OPEN ? DOOR_OPEN : DOOR_CLOSED;
            stopDoor("limit switch");
            doorState = endState;
            return;
        }
        if (now - motorStart >= DOOR_TRAVEL_TIMEOUT_MS) {
            stopDoor("travel timeout");
            doorState = DOOR_JAMMED;
            flash("JAMMED");
            Serial.println("!!! Door JAMMED - limit switch not reached");
        }
        return;
    }
    if (pendingDir != DIR_NONE) return;     // In the reverse pause

    // Idle: the switches decide. A jam stays latched until a switch closes.
    uint8_t fromLimits;
    if (limitOpen && limitClosed) fromLimits = DOOR_JAMMED;   // Wiring fault: both made
    else if (limitOpen) fromLimits = DOOR_OPEN;
    else if (limitClosed) fromLimits = DOOR_CLOSED;
    else fromLimits = DOOR_UNKNOWN;                           // Stopped mid-travel
    if (doorState == DOOR_JAMMED && fromLimits == DOOR_UNKNOWN) return;
    doorState = fromLimits;
}

// Toggle: moving -> stop, open -> close, anything else -> open.
void serviceButtons(unsigned long now) {
    for (Button& b : buttons) {
        bool raw = digitalRead(b.pin) == LOW;
        if (raw != b.rawPrev) {
            b.rawPrev = raw;
            b.rawSince = now;
        }
        if (now - b.rawSince < DEBOUNCE_MS || raw == b.pressed) continue;
        b.pressed = raw;
        if (!raw) continue;     // Act on press, not release

        wakeDisplay();
        if (motorDir != DIR_NONE || pendingDir != DIR_NONE) {
            stopDoor("button");
            flash("STOPPED");
        } else if (doorState == DOOR_OPEN) {
            requestMove(DIR_CLOSE, "button");
        } else {
            requestMove(DIR_OPEN, "button");
        }
    }
}

// ---------------------------------------------------------------- schedule
uint32_t unixNow() {
    time_t t = time(nullptr);
    return t >= (time_t)MIN_VALID_UNIX_TIME ? (uint32_t)t : 0;
}

static DoorSchedule scheduleForLocalDay(uint32_t anyTimeThatDay) {
    time_t t = (time_t)anyTimeThatDay;
    struct tm lt;
    localtime_r(&t, &lt);
    uint32_t earliest = 0;
    if (EARLIEST_OPEN_MINUTE > 0) {
        struct tm m = lt;
        m.tm_hour = EARLIEST_OPEN_MINUTE / 60;
        m.tm_min = EARLIEST_OPEN_MINUTE % 60;
        m.tm_sec = 0;
        m.tm_isdst = -1;
        earliest = (uint32_t)mktime(&m);
    }
    return doorScheduleFor(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                           COOP_LATITUDE, COOP_LONGITUDE,
                           OPEN_OFFSET_MIN, CLOSE_OFFSET_MIN, earliest);
}

// Next open / close strictly after now (today or tomorrow, local time).
void recomputeSchedule(uint32_t now) {
    nextOpenT = nextCloseT = 0;
    for (int d = 0; d <= 2 && (nextOpenT == 0 || nextCloseT == 0); d++) {
        DoorSchedule s = scheduleForLocalDay(now + d * 86400UL);
        if (!s.valid) continue;
        if (nextOpenT == 0 && s.open > now) nextOpenT = s.open;
        if (nextCloseT == 0 && s.close > now) nextCloseT = s.close;
    }
}

bool desiredOpenNow(uint32_t now) {
    DoorSchedule s = scheduleForLocalDay(now);
    return s.valid && now >= s.open && now < s.close;
}

// Put the door where the schedule says it should be right now (used when the
// clock first syncs and when switching to auto), then follow the edges.
void enforceSchedule(const char* why) {
    uint32_t now = unixNow();
    if (!now || mode != COOP_MODE_AUTO) return;
    bool wantOpen = desiredOpenNow(now);
    Serial.printf("Schedule (%s): door should be %s\n", why, wantOpen ? "OPEN" : "CLOSED");
    requestMove(wantOpen ? DIR_OPEN : DIR_CLOSE, why);
    recomputeSchedule(now);
}

void serviceSchedule(unsigned long now) {
    if (now - lastScheduleCheck < SCHEDULE_CHECK_MS) return;
    lastScheduleCheck = now;
    uint32_t t = unixNow();
    if (!t) return;

    if (nextOpenT == 0 && nextCloseT == 0) recomputeSchedule(t);
    if (mode != COOP_MODE_AUTO) return;

    if (nextOpenT && t >= nextOpenT) {
        requestMove(DIR_OPEN, "sunrise schedule");
        recomputeSchedule(t);
        statusDirty = true;
    }
    if (nextCloseT && t >= nextCloseT) {
        requestMove(DIR_CLOSE, "sunset schedule");
        recomputeSchedule(t);
        statusDirty = true;
    }
}

void setMode(uint8_t newMode) {
    if (newMode == mode) return;
    mode = newMode;
    prefs.putUChar("mode", mode);
    Serial.printf("Mode -> %s\n", mode == COOP_MODE_AUTO ? "auto" : "manual");
    flash(mode == COOP_MODE_AUTO ? "AUTO" : "MANUAL");
    if (mode == COOP_MODE_AUTO) enforceSchedule("auto mode");
}

void setClock(uint32_t unixTime) {
    if (unixTime < MIN_VALID_UNIX_TIME) return;
    uint32_t now = unixNow();
    long drift = (long)unixTime - (long)now;
    if (now && labs(drift) <= CLOCK_RESYNC_DRIFT_S) return;

    struct timeval tv = {(time_t)unixTime, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("Clock set from gateway (drift %lds)\n", now ? drift : 0L);
    recomputeSchedule(unixTime);
    if (!clockValid) {
        clockValid = true;
        enforceSchedule("clock synced");
    }
    statusDirty = true;
}

// ---------------------------------------------------------------- sensors
uint16_t readBatteryVoltage() {
    if (MOCK_DOOR_ENABLED) return MOCK_BATTERY_MV;
    uint32_t total = 0;
    for (int i = 0; i < 8; i++) total += analogReadMilliVolts(PIN_BATTERY_ADC);
    return (uint16_t)(total / 8 * BATTERY_DIVIDER_RATIO);
}

// Same policy as the gate node: keep the last good value through a few
// failed reads, then drop it so stale data isn't reported.
void readEnvironment() {
    lastDhtRead = millis();
    if (MOCK_DHT_ENABLED) {
        tempDc = MOCK_TEMP_DC;
        humDpct = MOCK_HUM_DPCT;
        return;
    }
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t) || t < -40.0 || t > 80.0 || h < 0.0 || h > 100.0) {
        if (dhtFailures < 255) dhtFailures++;
        if (dhtFailures >= DHT_MAX_FAILURES && envValid(tempDc, humDpct)) {
            tempDc = ENV_TEMP_INVALID;
            humDpct = ENV_HUM_INVALID;
            Serial.println("DHT22 data dropped - check sensor wiring on J8");
        }
        return;
    }
    dhtFailures = 0;
    tempDc = (int16_t)lroundf(t * 10.0f);
    humDpct = (uint16_t)lroundf(h * 10.0f);
}

// ---------------------------------------------------------------- messaging
void queueState(char type) {
    char msg[BUFFER_SIZE];
    uint16_t seq = nextSequence(sequenceNum);
    uint32_t nOpen = mode == COOP_MODE_AUTO ? nextOpenT : 0;
    uint32_t nClose = mode == COOP_MODE_AUTO ? nextCloseT : 0;
    formatCoopState(msg, COOP_ID, type, seq, doorState, mode, batteryMv, tempDc, humDpct, nOpen, nClose);
    if (!txQueue.push(msg)) return;     // Full: statusDirty stays set, retry next loop
    sequenceNum = seq;
    lastHeartbeat = millis();
    if (type == MSG_STATUS) statusDirty = false;
    pendingAckSeq = seq;
    pendingAckSentAt = millis();
}

void queueInfo() {
    char msg[BUFFER_SIZE];
    uint16_t seq = nextSequence(sequenceNum);
    formatCoopInfo(msg, COOP_ID, seq, COOP_NAME);
    if (!txQueue.push(msg)) return;
    sequenceNum = seq;
    infoDue = false;
    lastInfo = millis();
}

// An un-ACKed status is sent again, up to RETRY_COUNT times per state.
// (A missed heartbeat ACK also triggers a resend; the next state is the same.)
void serviceStatusRetry(unsigned long now) {
    if (pendingAckSeq == 0 || now - pendingAckSentAt < ACK_TIMEOUT) return;
    pendingAckSeq = 0;
    if (statusRetries < RETRY_COUNT) {
        statusRetries++;
        Serial.printf("No ACK - resending status (%u/%d)\n", statusRetries, RETRY_COUNT);
        statusDirty = true;
    }
}

void serviceTx() {
    if (!lora_idle || txQueue.empty()) return;
    if (millis() - lastRxTime < TX_TURNAROUND_MS) return;
    txQueue.pop(txpacket);
    Serial.printf("TX: %s\n", txpacket);
    lora_idle = false;
    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
}

void handleRxPacket() {
    char* id;
    char type;
    char* rest;
    if (!parseAddressed(rxpacket, &id, &type, &rest)) return;  // Gate traffic etc.
    if (strcmp(id, COOP_ID) != 0) return;                       // Another coop's

    Serial.printf("RX [RSSI:%d SNR:%d]: %c|%s\n", rxRssi, rxSnr, type, rest);

    if (type == MSG_ACK) {
        uint16_t seq = 0;
        unsigned long t = 0;
        int n = sscanf(rest, "%hu|%lu", &seq, &t);
        if (n < 1) return;
        if (seq == pendingAckSeq && seq != 0) {
            pendingAckSeq = 0;
            statusRetries = 0;
        }
        lastAckTime = millis();
        everLinked = true;
        if (n == 2) setClock((uint32_t)t);
        return;
    }

    if (type == MSG_COMMAND) {
        unsigned int cmd = 0;
        uint16_t seq = 0;
        if (sscanf(rest, "%u|%hu", &cmd, &seq) != 2 || seq == 0) return;

        char ack[BUFFER_SIZE];
        snprintf(ack, sizeof(ack), "@%s|A|%u", COOP_ID, seq);
        txQueue.push(ack, true);        // Always ACK; a retry means ours was lost

        unsigned long now = millis();
        if (seq == lastCmdSeq && now - lastCmdTime < DUPLICATE_CMD_WINDOW_MS) return;
        lastCmdSeq = seq;
        lastCmdTime = now;
        processCommand((uint8_t)cmd);
    }
}

void processCommand(uint8_t cmd) {
    Serial.printf("Command: %s\n", coopCommandToString(cmd));
    wakeDisplay();
    switch (cmd) {
        case CMD_OPEN:   requestMove(DIR_OPEN, "command"); break;
        case CMD_CLOSE:  requestMove(DIR_CLOSE, "command"); break;
        case CMD_STOP:   stopDoor("command"); flash("STOPPED"); break;
        case CMD_AUTO:   setMode(COOP_MODE_AUTO); break;
        case CMD_MANUAL: setMode(COOP_MODE_MANUAL); break;
        case CMD_STATUS: break;
        default:
            Serial.printf("Unknown command %u ignored\n", cmd);
            return;
    }
    statusDirty = true;
}

bool linkOk() {
    return everLinked && millis() - lastAckTime < LINK_LOST_MS;
}

// ---------------------------------------------------------------- display
void wakeDisplay() {
    displayOnSince = millis();
    if (displayEnabled) return;
    VextON();
    delay(50);
    display.displayOn();
    display.init();
    display.setContrast(255);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    displayEnabled = true;
}

void flash(const char* text) {
    wakeDisplay();
    flashText = text;
    flashUntil = millis() + 3000;
}

void checkDisplayTimeout() {
    if (!displayEnabled) return;
    // Stay on while the door moves or the link isn't up yet
    if (motorDir != DIR_NONE || pendingDir != DIR_NONE || !linkOk()) {
        displayOnSince = millis();
        return;
    }
    if (millis() - displayOnSince < DISPLAY_TIMEOUT_MS) return;
    display.clear();
    display.display();
    display.displayOff();
    VextOFF();
    displayEnabled = false;
}

static void formatLocalHm(char* out, size_t len, uint32_t t) {
    if (!t) {
        snprintf(out, len, "--:--");
        return;
    }
    time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    strftime(out, len, "%H:%M", &lt);
}

void updateDisplay() {
    static unsigned long lastRefresh = 0;
    unsigned long now = millis();
    if (now - lastRefresh < DISPLAY_REFRESH_MS) return;
    lastRefresh = now;

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    if (flashText != nullptr && (long)(flashUntil - now) > 0) {
        display.setFont(ArialMT_Plain_16);
        display.drawString(0, 0, COOP_NAME);
        display.setFont(ArialMT_Plain_24);
        display.drawString(0, 24, flashText);
        display.display();
        return;
    }
    flashText = nullptr;

    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, coopDoorToString(doorState));
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, MOCK_DOOR_ENABLED ? "MOCK" : (mode == COOP_MODE_AUTO ? "AUTO" : "MANUAL"));
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    char line[40];
    if (envValid(tempDc, humDpct)) {
        snprintf(line, sizeof(line), "%.1fC  %u%%  %.2fV", tempDc / 10.0, (humDpct + 5) / 10, batteryMv / 1000.0);
    } else {
        snprintf(line, sizeof(line), "DHT --  %.2fV", batteryMv / 1000.0);
    }
    display.drawString(0, 20, line);

    char openStr[8], closeStr[8];
    formatLocalHm(openStr, sizeof(openStr), mode == COOP_MODE_AUTO ? nextOpenT : 0);
    formatLocalHm(closeStr, sizeof(closeStr), mode == COOP_MODE_AUTO ? nextCloseT : 0);
    if (!clockValid) {
        snprintf(line, sizeof(line), "Clock: waiting for gateway");
    } else {
        snprintf(line, sizeof(line), "Open %s  Close %s", openStr, closeStr);
    }
    display.drawString(0, 32, line);

    if (linkOk()) {
        snprintf(line, sizeof(line), "Link OK  RSSI %d", rxRssi);
    } else if (everLinked) {
        snprintf(line, sizeof(line), "Link LOST (no ACK)");
    } else {
        snprintf(line, sizeof(line), "Link: waiting...");
    }
    display.drawString(0, 44, line);
    display.drawString(0, 54, COOP_ID);
    display.display();
}

// ---------------------------------------------------------------- radio callbacks
void OnTxDone() {
    lora_idle = true;
    Radio.Rx(0);
}

void OnTxTimeout() {
    Serial.println("TX Timeout!");
    lora_idle = true;
    Radio.Rx(0);
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    copyRxPayload(rxpacket, payload, size);
    rxRssi = rssi;
    rxSnr = snr;
    lastRxTime = millis();
    rxReady = true;
}
