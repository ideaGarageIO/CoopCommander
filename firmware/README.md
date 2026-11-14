# Coop Node Firmware

Arduino sketch for the Heltec WiFi LoRa 32 V3 on the coop node carrier board.

```
firmware/
├── coop_node/
│   ├── coop_node.ino          # door state machine, schedule, radio, display
│   ├── sun_times.h            # sunrise/sunset (NOAA, same maths as the Server's sun.py)
│   ├── coop_config_example.h  # defaults; coop_config.h (gitignored) overrides them
│   └── farm_protocol.h        # copy of GateMonitor/firmware/lib/farm_protocol
└── test/
    └── test_host.cpp          # PC unit tests for farm_protocol.h and sun_times.h
```

## Build and flash

```bash
scripts/build.sh coop_node                                   # compile (mock defaults)
scripts/build.sh coop_node --coop config/coops/coop-1.h      # compile with a coop's config
scripts/build.sh coop_node --coop config/coops/coop-1.h --upload /dev/ttyUSB0
scripts/build.sh coop_node --monitor /dev/ttyUSB0            # serial log, 115200
```

`build.sh` runs arduino-cli in Docker (esp32 core 3.0.7, Heltec ESP32
Dev-Boards, Adafruit DHT). FQBN `esp32:esp32:heltec_wifi_lora_32_V3`. In the
Arduino IDE: install **esp32 by Espressif**, board **Heltec WiFi LoRa 32(V3)**,
and the libraries **Heltec ESP32 Dev-Boards** and **DHT sensor library**.
Heltec's old 0.0.9 board package does not compile it.

`--coop <file>` copies that file to `coop_node/coop_config.h`. Without a
`coop_config.h` the sketch builds from `coop_config_example.h` (mock door,
id `coop-1`) and prints a compiler warning saying so.

## Pins

Must match `hardware/pcb/coop_node_carrier/scripts/design.py`.

| GPIO | Net | Connector | Notes |
|---|---|---|---|
| 5 | LIM_OPEN | J7-1 | Active LOW, 10k pull-up + 1k/100 nF filter on the board |
| 6 | LIM_CLOSED | J7-2 | Active LOW |
| 7 | MOTOR_IN1 | J3-3 | H-bridge IN1, 10k pull-down |
| 4 | MOTOR_IN2 | J3-4 | H-bridge IN2, 10k pull-down |
| 47 | AUX_RELAY | J5-3 | Relay module (light / heater), firmware holds it off |
| 2 | BATT_ADC | R1/R2 | Vbatt = Vadc x 5.545 |
| 48 | DHT_DATA | J8-2 | 10k pull-up R7 |
| 33 | BTN_EXT | J11-1 | External button, active LOW |
| 0 | PRG button | on Heltec | Same action as the external button |
| 34 | EXP_IO34 | J9-2 | Spare |

Motor: IN1 high / IN2 low = open (set `MOTOR_REVERSED` to swap), both low =
coast. The motor stops for 300 ms before reversing.

## Behaviour

See [../docs/architecture.md](../docs/architecture.md) for the door state
machine, the schedule rules and how time sync works. In short:

- Status is sent on every change (retried up to 3x until ACKed) and as a
  heartbeat every 60 s; the name goes out at boot and every 10 min.
- Every gateway ACK carries the time. The schedule starts on the first one
  and re-syncs the clock if it drifts by more than 2 s.
- Auto mode acts on the scheduled edges (open time, close time). When the
  clock first syncs, or when you switch to AUTO, the door is moved to where
  the schedule says it should be right now.
- A manual OPEN/CLOSE in auto mode holds until the next scheduled edge.

## Tests

```bash
scripts/test_firmware_host.sh
```

Covers node-id validation, addressed-packet parsing, every packet format
round-tripping through the gateway's parser, the unchanged gate packets, and
sunrise/sunset against the Server's `sun.py` (including offsets, the
earliest-open clamp, polar night and midnight sun).
