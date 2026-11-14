# Installing a coop node

Prerequisites: the home server is running (Server repo) and the house
gateway (GateMonitor) is flashed and online. Check that the dashboard's
**Gateway** pill is green.

## 1. Configure and flash on the bench

```bash
cp config/coops/coop-1.h config/coops/<id>.h      # edit id, name, lat/lon, TZ, offsets
scripts/build.sh coop_node --coop config/coops/<id>.h --upload /dev/ttyUSB0
scripts/build.sh coop_node --monitor /dev/ttyUSB0
```

The serial log should show `Clock set from gateway` within a minute or two, if
the gateway is in radio range of the bench. The coop then appears on the
dashboard's Coops tab and in Home Assistant.

To try the whole system before the door is built, flash with a config that
has `MOCK_DOOR_ENABLED true`, then switch it off before installing.

## 2. Wire the coop

| Carrier | Wire to |
|---|---|
| J6 12V+/GND | Solar charge controller **LOAD** output (battery and panel on the controller; see [docs/power.md](../docs/power.md)) |
| J10 VM/GND | H-bridge module power input |
| J3 (JST-XH 4) | H-bridge module GND / IN1 / IN2 (3V3 only if the module needs it) |
| H-bridge outputs | Door motor |
| J7 OPEN / CLOSED / GND | Limit switches: each one closes to GND at its end of travel |
| J11 BTN / GND | Optional push button by the coop door |
| J8 + / DATA / - | DHT22 |

Antenna: mount the 915 MHz antenna vertically, outside any metal enclosure,
with line of sight towards the house if possible.

## 3. Commission

1. Power up **with the motor unplugged from the H-bridge**. Check the display
   shows the right door state as you close each limit switch by hand.
2. Reconnect the motor. Press the button: the door should **open**. If it
   moves the wrong way, set `MOTOR_REVERSED true` and reflash (or swap the
   motor wires).
3. Time a full open and close. Set `DOOR_TRAVEL_TIMEOUT_MS` to about 1.5x the
   longer one and reflash.
4. Block the door partway (carefully) and command a close: after the timeout
   the motor must stop and the dashboard must show **JAMMED**.
5. Check the next open / close times on the display and the dashboard match
   sunrise / sunset plus your offsets.
6. Leave it in **AUTO**.

## 4. Server

Once real coops exist, set `SIMULATE_DEVICES=false` in the server's `.env`
(Server repo, `deploy/`) and restart it, so the simulated coops disappear.

## Troubleshooting

| Symptom | Check |
|---|---|
| Display says `Clock: waiting for gateway` | Gateway powered and online? Radio range? Gateway's heartbeat shows `"time_synced": true`? |
| `Link LOST (no ACK)` | Range / antenna; serial log on the gateway shows `RX: @<id>|…`? |
| Coop never appears on the dashboard | `COOP_ID` valid (`[a-z0-9][a-z0-9_-]*`)? Gateway serial log shows `New coop slot`? More than 8 coops? |
| JAMMED every time | Travel timeout too short, or a limit switch not closing to GND |
| Door twitches on reboot | The H-bridge has no pull-downs and R10/R11 aren't fitted |
