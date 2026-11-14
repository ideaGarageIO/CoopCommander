# Coop node: how it works

## Data path

```
coop_node ──LoRa──► house_gateway ──MQTT──► Mosquitto ──► Server (dashboard, history, HA discovery)
   ▲  "@coop-1|S|…"     │ coop/coop-1/state                         │
   │                    │                                           ▼
   └──"@coop-1|A|seq|time" (ACK + time)      Home Assistant ◄── discovery
   └──"@coop-1|C|cmd|seq" ◄── coop/coop-1/command ◄── dashboard / HA
```

The coop node never touches WiFi or MQTT. The house gateway (GateMonitor)
learns each coop from its packets, publishes `coop/<id>/state` and forwards
`coop/<id>/command`. Packet formats: GateMonitor's
`firmware/lib/farm_protocol/README.md`. Topic contract: Server's
`docs/mqtt-topics.md`.

## Door state machine

```
              OPEN cmd / button / sunrise
   CLOSED ──────────────────────────────► OPENING ──open switch──► OPEN
     ▲                                       │
     │                                       └─ timeout ─► JAMMED
     │      CLOSE cmd / button / sunset
     └──closed switch── CLOSING ◄────────────────────────── OPEN
                          └─ timeout ─► JAMMED

   STOP (or button while moving) ─► motor off ─► UNKNOWN (neither switch made)
   Both switches made at once ─► JAMMED (wiring fault)
```

- The motor stops on the **raw** switch reading (no debounce delay): an early
  stop on noise is safer than driving past the end. Reported states use a
  50 ms debounce.
- `JAMMED` latches until a new command, or until a limit switch closes (for
  example someone moves the door by hand).
- Reversing direction always stops the motor for 300 ms first.
- At reset the H-bridge inputs are held low by R10/R11 on the board and driven
  low as the first thing in `setup()`, so the motor can't twitch while the
  ESP32 boots.

## Schedule

For each local calendar day:

```
open  = max(sunrise + OPEN_OFFSET_MIN, EARLIEST_OPEN_MINUTE local)
close = sunset + CLOSE_OFFSET_MIN
```

Sunrise/sunset use the NOAA approximation (`sun_times.h`, the same maths as
the Server's `sun.py`, cross-checked by the host tests). Polar night keeps
the door shut all day; midnight sun keeps it open all day.

Auto mode acts on **edges**: at the open time it opens, at the close time it
closes. It then works out the next open and close (`next_open` /
`next_close` on the dashboard). That means a manual OPEN at night stays open
until the next close time. On first clock sync after boot, and whenever the
mode switches to AUTO, the node moves the door to where the schedule says it
should be right now.

## Time

The node has no RTC and no WiFi. The gateway syncs over NTP and puts the
Unix time in every ACK (`@<id>|A|<seq>|<time>`, 0 until it has synced). The
node sets its clock from the first valid ACK and again whenever it drifts by
more than 2 s. Time zone handling (for `EARLIEST_OPEN_MINUTE` and the
display) uses the POSIX `COOP_TZ` string, so DST is automatic.

Until the clock is set, the door does nothing on its own: commands and the
button still work, and the dashboard shows no next open / close.

## Link reliability

- Every node waits `TX_TURNAROUND_MS` (100 ms) after receiving before it
  transmits, so the other side is back in RX.
- A status (state change) that isn't ACKed within 5 s is sent again, up to 3
  times. Heartbeats go out every 60 s.
- Commands from the gateway are retried 3x until the node ACKs them. The node
  always ACKs, even a duplicate, but runs a given command sequence number only
  once.
- The gateway marks a coop offline after 200 s of silence.

## Power

The node listens continuously (no deep sleep) so commands arrive within a
second. Estimate about 20-30 mA from the 12 V battery with the display off
(ESP32-S3 + SX1262 in RX through the buck converter; measure your build), plus
the motor while it runs. That is roughly 0.5-0.75 Ah a day, so size the
battery and panel for several cloudy days.

## What it doesn't do (yet)

- No motor-current sensing: a jam is only detected by the travel timeout.
  Choose a door mechanism that can't crush a bird (gravity-closing string
  doors are the common choice) and keep the timeout tight.
- The AUX relay (J5) is wired but firmware holds it off. A light/heater
  command would need a new MQTT payload and server support.
- No battery-low protection: the door keeps working down to brown-out,
  because closing at night matters more than the battery.
