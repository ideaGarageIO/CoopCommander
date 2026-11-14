# Coop power system: solar + battery

Each coop runs off grid: a solar panel charges a 12 V battery through a charge
controller, and the controller's **LOAD** output feeds the carrier board. The
battery carries the node overnight and through cloudy spells, and supplies the
door motor's current peaks.

```
 20 W panel ──MC4──► 10 A PWM charge controller ◄──5 A fuse── 12 V 10 Ah LiFePO4
                          │ LOAD (low-voltage disconnect)
                          ▼
                   carrier J6 (12V+ / GND)
                     ├─ F1 0.5 A ─ D1 ─ Mini360 5 V ─ Heltec          (logic)
                     └─ F2 3 A ─ D6 ─ J10 ─ DRV8871 ─ door motor      (motor)
```

## Budget

| Load | Current @ 12 V | Time per day | Ah / day |
|---|---|---|---|
| Coop node, display off (Heltec in RX + buck losses) | ~25 mA (estimate) | 24 h | 0.60 |
| Display on (button presses, commands) | +10 mA | ~1 h | 0.01 |
| Door motor, open + close | ~1.5 A | 2 x 10 s | 0.01 |
| **Total** | | | **~0.62 Ah (~7.4 Wh)** |

The node itself is almost the whole load; the motor is a large peak but only
for seconds. Measure the real standby current with a meter in series with J6
and update the budget in the BOM sheet ("Coop Monitor" tab, power budget block).

## Battery: 12 V 10 Ah LiFePO4

- 10 Ah x 0.9 usable ÷ 0.62 Ah/day ≈ **14 days** with no sun at all.
- Plenty of current for the motor: a 10 A BMS easily covers a 1.5 A running /
  3 A stall motor.
- **Cold:** LiFePO4 must not be charged below 0 °C. Buy one whose BMS blocks
  charging below freezing (check the listing). It still *discharges* to about
  -20 °C, so the coop keeps running on cold nights and the battery recharges
  once the day warms up; the 14-day reserve covers a cold snap.
- For very cold coops, a 12 V 12 Ah **AGM** battery is the alternative: it
  charges below freezing, but use only half its capacity (~10 days of reserve).
  Set the charge controller to AGM.

## Panel: 20 W 12 V

20 W x 3 winter sun hours x 0.65 system efficiency ≈ 39 Wh/day against a
~7.4 Wh/day load, so about 31 Wh/day of surplus refills the battery after a
cloudy spell. Point it south (northern hemisphere), tilted about latitude +
15° for winter, and keep it clear of snow.

## Charge controller: 10 A PWM with LOAD output

- Must have a **LiFePO4 (or user) battery profile**, **LOAD terminals** and an
  **adjustable low-voltage disconnect**. PWM is fine at 20 W; MPPT adds little
  at this size.
- Wire the carrier's J6 to **LOAD**, not straight to the battery. The
  controller's low-voltage disconnect then protects the battery from being
  flattened. Set it to about 11.5 V for LiFePO4 (11.8 V for AGM), reconnect at
  about 12.8 V.
- If the LOAD output cuts off, the door stays where it is (the worm gear
  self-locks) and the coop shows offline on the dashboard: a visible warning
  rather than a dead battery.

## Fusing and wiring

- 5 A blade fuse at the battery positive, as close to the battery as possible.
- 16 AWG for battery and load runs under ~3 m; the motor current (≤3 A) is
  protected again on the board by F2.
- Connect in the controller's order: battery first, then panel, then load.

## Reading the battery voltage

LiFePO4 holds about 13.0-13.3 V for most of its capacity and only drops near
empty, so the voltage on the dashboard says little until the battery is
nearly flat. The dashboard's low-battery alert (below 11.9 V) therefore fires
late: below ~12 V a LiFePO4 is close to empty, so treat it as "less than a
day left" and check the panel (snow, shade, a blown fuse). With an AGM battery the voltage tracks
charge much more gradually (about 12.7 V full, 12.0 V half).
