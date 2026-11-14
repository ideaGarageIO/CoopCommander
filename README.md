# Coop Monitor

Solar-powered LoRa controller for a chicken coop. It opens the pop door at
sunrise, closes it after sunset, watches temperature, humidity and the
battery, and puts all of it on the farm dashboard and in Home Assistant on
your phone. Built on a Heltec WiFi LoRa 32 V3 plugged into a custom carrier
board, like the gate node.

## Features

### Door control
| Function | What it does |
|---|---|
| **Automatic open / close** | Opens at sunrise and closes after sunset, every day, computed on the node from the coop's latitude / longitude (no internet needed at the coop) |
| **Schedule offsets** | Open N minutes after sunrise, close N minutes after sunset (default +30 min, so late birds get in) |
| **Earliest open time** | Never opens before a set local time (default 06:00), so the flock isn't let out at dawn in midsummer |
| **Auto / Manual mode** | Manual pauses the schedule; the choice survives reboots and power loss |
| **Remote commands** | `OPEN`, `CLOSE`, `STOP`, `AUTO`, `MANUAL` from the dashboard's Coops tab or Home Assistant |
| **Local push button** | On-board PRG button or a weatherproof button on the coop: open → close, and stop while moving |
| **Limit switches** | Motor stops the moment the open or closed switch makes contact |
| **Jam detection** | If a switch isn't reached within the travel timeout, the motor stops and the door reports **JAMMED** (dashboard alert, "door jammed" sensor in Home Assistant) |
| **Safe motor handling** | Pull-downs and firmware keep the motor off during resets; 300 ms pause before every reversal; separate 3 A fuse for the motor |

### Monitoring
| Reading | Shown as |
|---|---|
| Door state | `OPEN`, `CLOSED`, `OPENING`, `CLOSING`, `JAMMED`, `UNKNOWN` |
| Temperature / humidity | DHT22, every 10 s; live charts on the dashboard |
| Battery voltage | 12 V battery through the board's divider |
| Next open / next close | The schedule's upcoming times, plus last opened / last closed |
| Link | Radio signal (RSSI) and online / offline (offline after ~3 min of silence) |
| Local display | OLED shows state, mode, readings and next times; sleeps after 60 s, wakes on a button press or command |

### Dashboard and Home Assistant (no setup needed)
A coop appears automatically the first time it transmits: a **Coops** tile
on the dashboard with controls, charts and an activity log, and in Home
Assistant a door cover, an auto-schedule switch, a door-jammed sensor, and
temperature / humidity / battery / next open / next close sensors. The
dashboard raises alerts for a jammed door, a door still open after dark, low
battery and a coop going offline.

### Power: solar + battery
Every coop runs off grid. A 20 W panel charges a 12 V 10 Ah LiFePO4 battery
through a 10 A charge controller, whose LOAD output feeds the board. The
battery runs the node all night and supplies the door motor; with ~0.6 Ah/day
of load it rides through about **two weeks with no sun**. Sizing, cold-weather
notes and wiring: [docs/power.md](docs/power.md).

### Bench / mock mode
With the default config the node simulates the door (8 s travel) and the
sensor, so you can test the whole system (gateway, dashboard, Home Assistant)
with just a Heltec board on USB.

## How it fits the farm system

| Repo | Role |
|---|---|
| **CoopMonitor** (this repo) | Coop node firmware, carrier PCB, BOM |
| **GateMonitor** | Gate node and the **house gateway**, which bridges every LoRa node (gate and coops) to MQTT and gives the coops the time |
| **Server** | Dashboard, history, Home Assistant integration, home-server deployment |

```
 Coop (solar + 12 V battery)                     House                     Home server
┌──────────────────────────────┐           ┌────────────────┐        ┌──────────────────────┐
│ Heltec V3 on coop carrier    │ LoRa 915  │ House gateway  │  MQTT  │ Mosquitto            │
│  H-bridge -> door motor      │◄─────────►│ (GateMonitor)  │◄──────►│ Farm dashboard       │
│  open/closed limit switches  │   MHz     │ WiFi + NTP     │ coop/  │ Home Assistant       │
│  DHT22, battery, button      │           │ time -> coops  │ <id>/… │  (phone control)     │
└──────────────────────────────┘           └────────────────┘        └──────────────────────┘
```

One gateway serves up to 8 coops. Nothing coop-specific is configured on the
gateway or the server.

## Hardware and BOM

- **Carrier board**: `hardware/pcb/coop_node_carrier`, 110 x 75 mm, 2-layer,
  all through-hole, generated from `scripts/design.py`. See its
  [README](hardware/pcb/coop_node_carrier/README.md) for the pin map,
  connectors and the checks to do before ordering.
- **BOM**: `python3 hardware/pcb/coop_node_carrier/scripts/gen_bom.py` writes
  - `bom_amazon.csv`: carrier board parts (sheet tab *Coop Node Carrier PCB*)
  - `bom_system.csv`: Heltec, antenna, sensors, door motor and driver,
    switches, enclosure, solar panel, battery, charge controller, fusing
    (sheet tab *Coop Monitor*)

  Both match the "Farm IOT Project BOMS" Google Sheet, which also has a power
  budget calculator.

## Getting started

Bench test (bare Heltec, mock door):

```bash
scripts/build.sh coop_node --upload /dev/ttyUSB0
scripts/build.sh coop_node --monitor /dev/ttyUSB0
```

With the house gateway running, `coop-1` shows up on the dashboard within a
minute; OPEN / CLOSE from the dashboard move the simulated door.

Real coop:

1. Copy `config/coops/coop-1.h` to `config/coops/<id>.h` and set the id,
   name, latitude / longitude, time zone, offsets and door travel timeout.
2. `scripts/build.sh coop_node --coop config/coops/<id>.h --upload /dev/ttyUSB0`
3. Wire the solar system, motor and switches, then commission the door:
   [deploy/README.md](deploy/README.md).
4. On the server, set `SIMULATE_DEVICES=false` once real coops exist.

## Repo layout

```
CoopMonitor/
├── firmware/coop_node/     Sketch, sunrise maths, config template, protocol copy
├── firmware/test/          PC unit tests (protocol + schedule)
├── hardware/pcb/coop_node_carrier/   KiCad project, generators, BOM CSVs
├── config/coops/           One config file per coop
├── scripts/                build.sh (arduino-cli in Docker), sync_protocol.sh, host tests
├── deploy/                 Installing and commissioning a coop
├── schematics/             Reference material
└── docs/                   architecture.md (how it works), power.md (solar sizing)
```

## Development

```bash
scripts/test_firmware_host.sh       # protocol + sunrise/sunset unit tests (g++)
scripts/build.sh coop_node          # compile check
scripts/sync_protocol.sh --check    # farm_protocol.h matches GateMonitor's copy
```

The LoRa protocol lives in GateMonitor (`firmware/lib/farm_protocol`); change
it there, then run `scripts/sync_protocol.sh` in both repos.

## Limitations

- No motor-current sensing: jams are caught by the travel timeout, so use a
  gravity-closing door that can't crush a bird.
- The AUX relay output (light / heater) is wired on the board but not yet
  controllable from the dashboard.
- No battery-low lockout: closing the door at night matters more than the
  battery; the charge controller's low-voltage disconnect protects it.

## License

MIT License - See [LICENSE](LICENSE)
